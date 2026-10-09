#include <tessera/fonts/font_profile_serialization.hpp>
#include "font_profile_detail.hpp"
#include "../../src/ui/json_detail.hpp"
#include <cmath>
#include <limits>
#include <utility>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;
struct Failure { Diagnostic diagnostic; };

[[noreturn]] void fail(std::string code, std::string path, std::string message) {
    throw Failure{{std::move(code), Severity::error, std::move(path), std::move(message), {}}};
}
template<class T>
const T& as(const JsonValue& value, const std::string& path, std::string_view expected) {
    if (const auto* result = std::get_if<T>(&value.value)) return *result;
    fail("schema_type", path, "Expected " + std::string(expected) + ".");
}
const Object& object(const JsonValue& value, const std::string& path,
                     std::initializer_list<std::string_view> fields) {
    const auto& result = as<Object>(value, path, "an object");
    for (const auto& [key, child] : result) {
        (void)child;
        bool found = false;
        for (auto field : fields) if (key == field) found = true;
        if (!found) fail("unknown_field", path + "/" + detail::pointer_token(key), "Remove this unknown font-profile field.");
    }
    for (auto field : fields)
        if (!result.contains(field)) fail("missing_field", path + "/" + std::string(field), "Supply this required field.");
    return result;
}
const Array& array(const JsonValue& value, const std::string& path) {
    const auto& result = as<Array>(value, path, "an array");
    if (result.size() > max_font_profile_entries) fail("out_of_range", path, "Expected at most 64 entries.");
    return result;
}
std::uint32_t integer(const JsonValue& value, const std::string& path) {
    const auto result = as<double>(value, path, "an unsigned 32-bit integer");
    if (result < 0 || result > std::numeric_limits<std::uint32_t>::max() || std::floor(result) != result)
        fail("schema_type", path, "Expected an unsigned 32-bit integer.");
    return static_cast<std::uint32_t>(result);
}
const std::string& string(const JsonValue& value, const std::string& path) {
    return as<std::string>(value, path, "a string");
}
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::vector<std::byte> decode(const JsonValue& value, const std::string& path, std::size_t& total) {
    const auto& hex = string(value, path);
    if (hex.size() % 2 != 0) fail("invalid_font_bytes", path, "Expected pairs of hexadecimal digits without separators.");
    const auto size = hex.size() / 2;
    if (size > max_font_profile_bytes - total) fail("out_of_range", path, "Total font asset bytes exceed 256 MiB.");
    // Reject malformed text before allocating decoded storage.
    for (const auto c : hex)
        if (nibble(c) < 0) fail("invalid_font_bytes", path, "Expected pairs of hexadecimal digits without separators.");
    total += size;
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::byte>(nibble(hex[2 * i]) * 16 + nibble(hex[2 * i + 1]));
    return bytes;
}
std::string encode(const std::vector<std::byte>& bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto b = std::to_integer<unsigned>(bytes[i]);
        result[2 * i] = hex[b >> 4];
        result[2 * i + 1] = hex[b & 15];
    }
    return result;
}
JsonValue number(std::uint32_t value) { return JsonValue{static_cast<double>(value)}; }

FontProfile read(const JsonValue& value) {
    const auto& root = object(value, "", {"version", "assets", "faces", "fallbacks", "families", "aliases"});
    FontProfile result;
    result.version = integer(root.at("version"), "/version");
    if (result.version != font_profile_version) fail("unsupported_version", "/version", "Expected font profile version 1.");
    std::size_t total = 0;
    const auto& assets = array(root.at("assets"), "/assets");
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto path = "/assets/" + std::to_string(i);
        const auto& asset = object(assets[i], path, {"id", "version", "bytes"});
        result.assets.push_back({string(asset.at("id"), path + "/id"), string(asset.at("version"), path + "/version"),
                                 decode(asset.at("bytes"), path + "/bytes", total)});
    }
    const auto& faces = array(root.at("faces"), "/faces");
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto path = "/faces/" + std::to_string(i);
        const auto& face = object(faces[i], path, {"font", "asset", "face_index"});
        result.faces.push_back({{integer(face.at("font"), path + "/font")}, string(face.at("asset"), path + "/asset"),
                                integer(face.at("face_index"), path + "/face_index")});
    }
    const auto& fallbacks = array(root.at("fallbacks"), "/fallbacks");
    for (std::size_t i = 0; i < fallbacks.size(); ++i) {
        const auto path = "/fallbacks/" + std::to_string(i);
        const auto& stack = object(fallbacks[i], path, {"font", "faces"});
        FontProfileFallback entry{{integer(stack.at("font"), path + "/font")}, {}};
        const auto& ids = array(stack.at("faces"), path + "/faces");
        for (std::size_t j = 0; j < ids.size(); ++j)
            entry.faces.push_back({integer(ids[j], path + "/faces/" + std::to_string(j))});
        result.fallbacks.push_back(std::move(entry));
    }
    const auto& families = array(root.at("families"), "/families");
    for (std::size_t i = 0; i < families.size(); ++i) {
        const auto path = "/families/" + std::to_string(i);
        const auto& family = object(families[i], path, {"name", "faces"});
        FontProfileFamily entry{string(family.at("name"), path + "/name"), {}};
        const auto& weighted = array(family.at("faces"), path + "/faces");
        for (std::size_t j = 0; j < weighted.size(); ++j) {
            const auto location = path + "/faces/" + std::to_string(j);
            const auto& face = object(weighted[j], location, {"font", "weight"});
            const auto weight = integer(face.at("weight"), location + "/weight");
            if (weight < 1 || weight > 1000) fail("out_of_range", location + "/weight", "Expected a declared weight in [1, 1000].");
            entry.faces.push_back({{integer(face.at("font"), location + "/font")}, static_cast<std::uint16_t>(weight)});
        }
        result.families.push_back(std::move(entry));
    }
    const auto& aliases = array(root.at("aliases"), "/aliases");
    for (std::size_t i = 0; i < aliases.size(); ++i) {
        const auto path = "/aliases/" + std::to_string(i);
        const auto& alias = object(aliases[i], path, {"font", "name", "families"});
        FontProfileAlias entry{{integer(alias.at("font"), path + "/font")}, string(alias.at("name"), path + "/name"), {}};
        const auto& names = array(alias.at("families"), path + "/families");
        for (std::size_t j = 0; j < names.size(); ++j)
            entry.families.push_back(string(names[j], path + "/families/" + std::to_string(j)));
        result.aliases.push_back(std::move(entry));
    }
    return result;
}

JsonValue write(const FontProfile& profile) {
    Array assets, faces, fallbacks, families, aliases;
    for (const auto& a : profile.assets)
        assets.push_back(JsonValue{Object{{"id", JsonValue{a.id}}, {"version", JsonValue{a.version}}, {"bytes", JsonValue{encode(a.bytes)}}}});
    for (const auto& f : profile.faces)
        faces.push_back(JsonValue{Object{{"font", number(f.font.value)}, {"asset", JsonValue{f.asset}}, {"face_index", number(f.face_index)}}});
    for (const auto& s : profile.fallbacks) {
        Array ids;
        for (const auto font : s.faces) ids.push_back(number(font.value));
        fallbacks.push_back(JsonValue{Object{{"font", number(s.font.value)}, {"faces", JsonValue{std::move(ids)}}}});
    }
    for (const auto& f : profile.families) {
        Array weighted;
        for (const auto& face : f.faces)
            weighted.push_back(JsonValue{Object{{"font", number(face.font.value)}, {"weight", number(face.weight)}}});
        families.push_back(JsonValue{Object{{"name", JsonValue{f.name}}, {"faces", JsonValue{std::move(weighted)}}}});
    }
    for (const auto& a : profile.aliases) {
        Array names;
        for (const auto& name : a.families) names.push_back(JsonValue{name});
        aliases.push_back(JsonValue{Object{{"font", number(a.font.value)}, {"name", JsonValue{a.name}}, {"families", JsonValue{std::move(names)}}}});
    }
    return JsonValue{Object{{"version", number(profile.version)}, {"assets", JsonValue{std::move(assets)}},
        {"faces", JsonValue{std::move(faces)}}, {"fallbacks", JsonValue{std::move(fallbacks)}},
        {"families", JsonValue{std::move(families)}}, {"aliases", JsonValue{std::move(aliases)}}}};
}
} // namespace

Result<FontProfile> load_font_profile(std::string_view source) {
    auto json = detail::parse_json(source, max_serialized_font_profile_bytes, max_font_profile_json_values);
    if (!json) return {{}, std::move(json.diagnostics)};
    try {
        auto profile = read(json.value->root);
        auto service = detail::build_profile_shaper(profile);
        if (!service) {
            detail::annotate_json(service.diagnostics, *json.value);
            return {{}, std::move(service.diagnostics)};
        }
        return {std::move(profile), {}};
    } catch (Failure& failure) {
        std::vector<Diagnostic> errors{std::move(failure.diagnostic)};
        detail::annotate_json(errors, *json.value);
        return {{}, std::move(errors)};
    }
}

Result<std::string> save_font_profile(const FontProfile& profile) {
    auto service = detail::build_profile_shaper(profile);
    if (!service) return {{}, std::move(service.diagnostics)};
    auto output = detail::write_json_value(write(profile));
    output += '\n';
    if (output.size() > max_serialized_font_profile_bytes)
        return {{}, {{"size_limit", Severity::error, "", "Canonical font profile exceeds the serialized byte limit.", {}}}};
    return {std::move(output), {}};
}
} // namespace tessera
