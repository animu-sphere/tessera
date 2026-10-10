#include <tessera/ui/serialization.hpp>
#include "json_detail.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tessera::detail {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        unsigned count;
        std::uint32_t scalar;
        std::uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; scalar = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; scalar = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; scalar = first & 0x07; minimum = 0x10000; }
        else return false;
        if (text.size() - i < count) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    }
    return true;
}

std::string pointer_token(std::string_view value) {
    std::string result;
    for (char c : value) {
        if (c == '~') result += "~0";
        else if (c == '/') result += "~1";
        else result += c;
    }
    return result;
}
} // namespace tessera::detail

namespace tessera {
namespace {
struct Failure { Diagnostic diagnostic; };

class Parser {
public:
    explicit Parser(std::string_view source, std::size_t value_limit = std::numeric_limits<std::size_t>::max())
        : source_(source), value_limit_(value_limit) {}
    JsonValue parse() {
        auto result = value("", 0);
        whitespace();
        if (position_ != source_.size()) fail("json_syntax", "", "Unexpected data after the JSON value.");
        return result;
    }
    std::map<std::string, std::size_t, std::less<>> offsets; // Value start by JSON pointer.
    std::map<std::string, std::size_t, std::less<>> ends;    // Value end (exclusive) by JSON pointer.
private:
    std::string_view source_;
    std::size_t position_ = 0;
    std::size_t value_limit_;
    std::size_t values_ = 0;
    [[noreturn]] void fail(std::string code, std::string path, std::string message) const {
        throw Failure{{std::move(code), Severity::error, std::move(path), std::move(message), position_}};
    }
    void whitespace() {
        while (position_ < source_.size() &&
               (source_[position_] == ' ' || source_[position_] == '\t' ||
                source_[position_] == '\r' || source_[position_] == '\n')) ++position_;
    }
    bool take(char expected) {
        if (position_ < source_.size() && source_[position_] == expected) { ++position_; return true; }
        return false;
    }
    void expect(char expected, const std::string& path) {
        whitespace();
        if (!take(expected)) fail("json_syntax", path, std::string("Expected '") + expected + "'.");
    }
    std::uint32_t hex4(const std::string& path) {
        std::uint32_t scalar = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ == source_.size()) fail("json_syntax", path, "Incomplete Unicode escape.");
            const char c = source_[position_++];
            scalar <<= 4;
            if (c >= '0' && c <= '9') scalar |= c - '0';
            else if (c >= 'a' && c <= 'f') scalar |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') scalar |= c - 'A' + 10;
            else fail("json_syntax", path, "Unicode escape requires four hexadecimal digits.");
        }
        return scalar;
    }
    static void append_utf8(std::string& output, std::uint32_t scalar) {
        if (scalar <= 0x7f) output += static_cast<char>(scalar);
        else if (scalar <= 0x7ff) {
            output += static_cast<char>(0xc0 | (scalar >> 6));
            output += static_cast<char>(0x80 | (scalar & 0x3f));
        } else if (scalar <= 0xffff) {
            output += static_cast<char>(0xe0 | (scalar >> 12));
            output += static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
            output += static_cast<char>(0x80 | (scalar & 0x3f));
        } else {
            output += static_cast<char>(0xf0 | (scalar >> 18));
            output += static_cast<char>(0x80 | ((scalar >> 12) & 0x3f));
            output += static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
            output += static_cast<char>(0x80 | (scalar & 0x3f));
        }
    }
    std::string string(const std::string& path) {
        expect('"', path);
        std::string result;
        while (position_ < source_.size()) {
            const char c = source_[position_++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 0x20) fail("json_syntax", path, "Escape control characters in strings.");
            if (c != '\\') { result += c; continue; }
            if (position_ == source_.size()) fail("json_syntax", path, "Incomplete string escape.");
            switch (source_[position_++]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                auto scalar = hex4(path);
                if (scalar >= 0xd800 && scalar <= 0xdbff) {
                    if (!take('\\') || !take('u')) fail("invalid_unicode", path, "High surrogate requires a low surrogate escape.");
                    const auto low = hex4(path);
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid_unicode", path, "Expected a low surrogate.");
                    scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
                } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
                    fail("invalid_unicode", path, "Low surrogate requires a preceding high surrogate.");
                }
                append_utf8(result, scalar);
                break;
            }
            default: fail("json_syntax", path, "Unknown string escape.");
            }
        }
        fail("json_syntax", path, "Unterminated string.");
    }
    JsonValue number(const std::string& path) {
        const auto start = position_;
        take('-');
        auto digit = [&] { return position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9'; };
        if (!take('0')) {
            if (!digit()) fail("json_syntax", path, "Expected a JSON number.");
            while (digit()) ++position_;
        }
        if (take('.')) {
            if (!digit()) fail("json_syntax", path, "Fraction requires digits after the decimal point.");
            while (digit()) ++position_;
        }
        if (take('e') || take('E')) {
            if (!take('+')) take('-');
            if (!digit()) fail("json_syntax", path, "Exponent requires digits.");
            while (digit()) ++position_;
        }
        double result = 0;
        const auto begin = source_.data() + start;
        const auto end = source_.data() + position_;
        const auto converted = std::from_chars(begin, end, result, std::chars_format::general);
        if (converted.ec != std::errc{} || converted.ptr != end || !std::isfinite(result))
            fail("invalid_number", path, "Number must be representable as a finite binary64 value.");
        return JsonValue{result};
    }
    JsonValue value(const std::string& path, std::size_t depth) {
        auto result = value_at(path, depth);
        ends[path] = position_;
        return result;
    }
    JsonValue value_at(const std::string& path, std::size_t depth) {
        if (values_ == value_limit_) fail("json_value_limit", path, "JSON exceeds this format's value limit.");
        ++values_;
        if (depth > max_json_depth) fail("depth_limit", path, "JSON exceeds the nesting limit (256).");
        whitespace();
        offsets[path] = position_;
        if (position_ == source_.size()) fail("json_syntax", path, "Expected a JSON value.");
        if (source_[position_] == '"') return JsonValue{string(path)};
        if (take('{')) {
            JsonValue::Object result;
            whitespace();
            if (take('}')) return JsonValue{std::move(result)};
            for (;;) {
                const auto name = string(path);
                const auto child_path = path + "/" + detail::pointer_token(name);
                if (result.contains(name)) fail("duplicate_member", child_path, "Duplicate JSON object member; remove one definition.");
                expect(':', child_path);
                result.emplace(name, value(child_path, depth + 1));
                whitespace();
                if (take('}')) break;
                expect(',', path);
            }
            return JsonValue{std::move(result)};
        }
        if (take('[')) {
            JsonValue::Array result;
            whitespace();
            if (take(']')) return JsonValue{std::move(result)};
            for (;;) {
                result.push_back(value(path + "/" + std::to_string(result.size()), depth + 1));
                whitespace();
                if (take(']')) break;
                expect(',', path);
            }
            return JsonValue{std::move(result)};
        }
        if (source_.substr(position_, 4) == "null") { position_ += 4; return {}; }
        if (source_.substr(position_, 4) == "true") { position_ += 4; return JsonValue{true}; }
        if (source_.substr(position_, 5) == "false") { position_ += 5; return JsonValue{false}; }
        return number(path);
    }
};

[[noreturn]] void schema_error(std::string code, const std::string& path, std::string message) {
    throw Failure{{std::move(code), Severity::error, path, std::move(message), {}}};
}

template<class T>
const T& as(const JsonValue& value, const std::string& path, std::string_view name) {
    if (const auto* result = std::get_if<T>(&value.value)) return *result;
    schema_error("schema_type", path, "Expected " + std::string(name) + ".");
}

void fields(const JsonValue::Object& object, std::initializer_list<std::string_view> allowed, const std::string& path) {
    for (const auto& [key, value] : object) {
        (void)value;
        bool known = false;
        for (const auto name : allowed) if (key == name) known = true;
        if (!known) schema_error("unknown_field", path + "/" + detail::pointer_token(key),
                                "Unknown field; store editor data in namespaced 'extensions'.");
    }
}

const JsonValue& required(const JsonValue::Object& object, const std::string& key, const std::string& path) {
    const auto found = object.find(key);
    if (found == object.end()) schema_error("missing_field", path + "/" + key, "Required field is missing.");
    return found->second;
}

UiNode read_node(const JsonValue& value, const std::string& path, std::size_t depth, std::size_t& count) {
    if (depth > max_document_depth) schema_error("depth_limit", path, "Document exceeds the node nesting limit (64).");
    if (++count > max_document_nodes) schema_error("node_limit", path, "Document exceeds the node limit (10000).");
    const auto& object = as<JsonValue::Object>(value, path, "a node object");
    fields(object, {"type", "id", "classes", "properties", "events", "extensions", "children"}, path);
    UiNode result;
    const auto& type = as<std::string>(required(object, "type", path), path + "/type", "a node kind string");
    if (type == "Box") result.kind = NodeKind::box;
    else if (type == "Text") result.kind = NodeKind::text;
    else schema_error("unknown_node_kind", path + "/type", "Supported node kinds are Box and Text.");
    if (const auto found = object.find("id"); found != object.end())
        result.id = as<std::string>(found->second, path + "/id", "an author-ID string");
    if (const auto found = object.find("classes"); found != object.end()) {
        const auto& array = as<JsonValue::Array>(found->second, path + "/classes", "an array");
        for (std::size_t i = 0; i < array.size(); ++i)
            result.classes.push_back(as<std::string>(array[i], path + "/classes/" + std::to_string(i), "a class string"));
    }
    if (const auto found = object.find("properties"); found != object.end()) {
        for (const auto& [name, property] : as<JsonValue::Object>(found->second, path + "/properties", "an object")) {
            const auto location = path + "/properties/" + detail::pointer_token(name);
            if (const auto* boolean = std::get_if<bool>(&property.value)) result.properties.emplace(name, *boolean);
            else if (const auto* number = std::get_if<double>(&property.value)) result.properties.emplace(name, *number);
            else if (const auto* text = std::get_if<std::string>(&property.value)) result.properties.emplace(name, *text);
            else if (const auto* reference = std::get_if<JsonValue::Object>(&property.value)) {
                fields(*reference, {"ref"}, location);
                result.properties.emplace(name, NodeReference{as<std::string>(required(*reference, "ref", location), location + "/ref", "an author-ID string")});
            } else schema_error("property_type", location, "Expected a boolean, number, string, or {\"ref\":\"id\"}.");
        }
    }
    if (const auto found = object.find("events"); found != object.end()) {
        for (const auto& [name, action] : as<JsonValue::Object>(found->second, path + "/events", "an object"))
            result.events.emplace(name, as<std::string>(action, path + "/events/" + detail::pointer_token(name), "a host-action string"));
    }
    if (const auto found = object.find("extensions"); found != object.end())
        result.extensions = as<JsonValue::Object>(found->second, path + "/extensions", "an extension object");
    if (const auto found = object.find("children"); found != object.end()) {
        const auto& array = as<JsonValue::Array>(found->second, path + "/children", "an array");
        for (std::size_t i = 0; i < array.size(); ++i)
            result.children.push_back(read_node(array[i], path + "/children/" + std::to_string(i), depth + 1, count));
    }
    return result;
}

JsonValue node_json(const UiNode& node) {
    JsonValue::Object result;
    result["type"] = JsonValue{std::string(node.kind == NodeKind::box ? "Box" : "Text")};
    if (node.id) result["id"] = JsonValue{*node.id};
    if (!node.classes.empty()) {
        JsonValue::Array values;
        for (const auto& name : node.classes) values.push_back(JsonValue{name});
        result["classes"] = JsonValue{std::move(values)};
    }
    if (!node.properties.empty()) {
        JsonValue::Object values;
        for (const auto& [name, property] : node.properties) {
            values[name] = std::visit([](const auto& value) -> JsonValue {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, NodeReference>)
                    return JsonValue{JsonValue::Object{{"ref", JsonValue{value.id}}}};
                else return JsonValue{value};
            }, property);
        }
        result["properties"] = JsonValue{std::move(values)};
    }
    if (!node.events.empty()) {
        JsonValue::Object values;
        for (const auto& [name, action] : node.events) values[name] = JsonValue{action};
        result["events"] = JsonValue{std::move(values)};
    }
    if (!node.extensions.empty()) result["extensions"] = JsonValue{node.extensions};
    if (!node.children.empty()) {
        JsonValue::Array values;
        for (const auto& child : node.children) values.push_back(node_json(child));
        result["children"] = JsonValue{std::move(values)};
    }
    return JsonValue{std::move(result)};
}

UiDocument read_document(const JsonValue& json, const std::string& path) {
    const auto& object = as<JsonValue::Object>(json, path, "a document object");
    fields(object, {"version", "root", "extensions"}, path);
    const auto version = as<double>(required(object, "version", path), path + "/version", "a version integer");
    if (version < 0 || version > std::numeric_limits<std::uint32_t>::max() || std::floor(version) != version)
        schema_error("schema_type", path + "/version", "Version must be an unsigned 32-bit integer.");
    if (version != 1)
        schema_error("unsupported_version", path + "/version", "Only document version 1 is supported; migrate explicitly.");
    UiDocument document;
    std::size_t count = 0;
    document.root = read_node(required(object, "root", path), path + "/root", 1, count);
    if (const auto found = object.find("extensions"); found != object.end())
        document.extensions = as<JsonValue::Object>(found->second, path + "/extensions", "an extension object");
    return document;
}

JsonValue document_json(const UiDocument& document) {
    JsonValue::Object object{{"root", node_json(document.root)}, {"version", JsonValue{static_cast<double>(document.version)}}};
    if (!document.extensions.empty()) object["extensions"] = JsonValue{document.extensions};
    return JsonValue{std::move(object)};
}

void write_string(std::string& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output += '"';
    for (const auto c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { output += '\\'; output += c; }
        else if (byte < 0x20) {
            output += "\\u00";
            output += hex[byte >> 4]; output += hex[byte & 0xf];
        } else output += c;
    }
    output += '"';
}

void write_json(std::string& output, const JsonValue& value) {
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
        else if constexpr (std::is_same_v<T, bool>) output += data ? "true" : "false";
        else if constexpr (std::is_same_v<T, double>) {
            if (data == 0) { output += '0'; return; }
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), data, std::chars_format::general);
            if (result.ec != std::errc{}) throw std::logic_error("Finite binary64 formatting failed");
            output.append(buffer, result.ptr);
        } else if constexpr (std::is_same_v<T, std::string>) write_string(output, data);
        else if constexpr (std::is_same_v<T, JsonValue::Array>) {
            output += '[';
            bool first = true;
            for (const auto& child : data) {
                if (!first) output += ',';
                first = false;
                write_json(output, child);
            }
            output += ']';
        } else {
            output += '{';
            bool first = true;
            for (const auto& [key, child] : data) {
                if (!first) output += ',';
                first = false;
                write_string(output, key);
                output += ':';
                write_json(output, child);
            }
            output += '}';
        }
    }, value.value);
}

void annotate(std::vector<Diagnostic>& diagnostics, const Parser& parser) {
    for (auto& diagnostic : diagnostics) {
        auto location = diagnostic.path;
        for (;;) {
            const auto found = parser.offsets.find(location);
            if (found != parser.offsets.end()) { diagnostic.byte_offset = found->second; break; }
            if (location.empty()) break;
            location.resize(location.rfind('/'));
        }
    }
}
void map_sources(const UiNode& node, const std::string& path, const Parser& parser, std::vector<NodeSource>& out) {
    NodeSource source{path, {parser.offsets.at(path), parser.ends.at(path)}, {}};
    for (const auto& [name, property] : node.properties) {
        (void)property;
        const auto location = path + "/properties/" + detail::pointer_token(name);
        source.properties.emplace(name, SourceSpan{parser.offsets.at(location), parser.ends.at(location)});
    }
    out.push_back(std::move(source));
    for (std::size_t i = 0; i < node.children.size(); ++i)
        map_sources(node.children[i], path + "/children/" + std::to_string(i), parser, out);
}

Result<UiDocument> load(std::string_view source, const ValidationContext& context, Parser& parser) {
    if (source.size() > max_serialized_bytes)
        return {std::nullopt, {{"size_limit", Severity::error, "", "Document exceeds the byte limit (4 MiB).", 0}}};
    if (!detail::valid_utf8(source))
        return {std::nullopt, {{"invalid_utf8", Severity::error, "", "JSON input must be valid UTF-8 without a BOM.", 0}}};
    try {
        auto document = read_document(parser.parse(), "");
        auto diagnostics = validate(document, context);
        annotate(diagnostics, parser);
        if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
        return {std::move(document), {}};
    } catch (Failure& failure) {
        std::vector<Diagnostic> diagnostics{std::move(failure.diagnostic)};
        if (!diagnostics.front().byte_offset) annotate(diagnostics, parser);
        return {std::nullopt, std::move(diagnostics)};
    }
}

} // namespace

namespace detail {
Result<ParsedJson> parse_json(std::string_view source, std::size_t byte_limit, std::size_t value_limit) {
    if (source.size() > byte_limit)
        return {std::nullopt, {{"size_limit", Severity::error, "", "JSON input exceeds this format's byte limit.", 0}}};
    if (!valid_utf8(source))
        return {std::nullopt, {{"invalid_utf8", Severity::error, "", "JSON input must be valid UTF-8 without a BOM.", 0}}};
    Parser parser(source, value_limit);
    try {
        auto root = parser.parse();
        return {ParsedJson{std::move(root), std::move(parser.offsets)}, {}};
    } catch (Failure& failure) {
        return {std::nullopt, {std::move(failure.diagnostic)}};
    }
}
void annotate_json(std::vector<Diagnostic>& diagnostics, const ParsedJson& json) {
    for (auto& diagnostic : diagnostics) {
        if (diagnostic.byte_offset) continue;
        auto location = diagnostic.path;
        for (;;) {
            const auto found = json.offsets.find(location);
            if (found != json.offsets.end()) { diagnostic.byte_offset = found->second; break; }
            if (location.empty()) break;
            location.resize(location.rfind('/'));
        }
    }
}
std::string write_json_value(const JsonValue& value) {
    std::string result;
    write_json(result, value);
    return result;
}
Result<UiDocument> read_document_json(const JsonValue& json, const std::string& path, const ValidationContext& context) {
    try {
        auto document = read_document(json, path);
        auto diagnostics = validate(document, context);
        if (diagnostics.empty()) return {std::move(document), {}};
        for (auto& diagnostic : diagnostics) diagnostic.path = path + diagnostic.path;
        return {std::nullopt, std::move(diagnostics)};
    } catch (Failure& failure) {
        return {std::nullopt, {std::move(failure.diagnostic)}};
    }
}
JsonValue write_document_json(const UiDocument& document) { return document_json(document); }
} // namespace detail

Result<UiDocument> load_document(std::string_view source, const ValidationContext& context) {
    Parser parser(source);
    return load(source, context, parser);
}

Result<SourcedDocument> load_document_with_sources(std::string_view source, std::string file,
                                                   const ValidationContext& context) {
    Parser parser(source);
    auto loaded = load(source, context, parser);
    if (!loaded) return {std::nullopt, std::move(loaded.diagnostics)};
    SourcedDocument result{std::move(*loaded.value), {std::move(file), {}}};
    map_sources(result.document.root, "/root", parser, result.sources.nodes);
    return {std::move(result), {}};
}

Result<std::string> save_document(const UiDocument& document, const ValidationContext& context) {
    auto diagnostics = validate(document, context);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
    std::string output;
    write_json(output, document_json(document));
    output += '\n';
    if (output.size() > max_serialized_bytes)
        return {std::nullopt, {{"size_limit", Severity::error, "", "Canonical document exceeds the byte limit (4 MiB).", {}}}};
    return {std::move(output), {}};
}
} // namespace tessera
