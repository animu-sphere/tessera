#include <tessera/fonts/font_profile.hpp>
#include "font_profile_detail.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace tessera {
namespace {

std::vector<Diagnostic> failure(std::string code, std::string path, std::string message) {
    return {{std::move(code), Severity::error, std::move(path), std::move(message), {}}};
}

std::vector<Diagnostic> identifier(std::string_view name, const std::string& path) {
    if (name.empty() || name.size() > 256 ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        return failure("invalid_font_name", path, "Expected 1..256 UTF-8 bytes without ASCII controls.");
    auto errors = validate_text_input(name, {});
    for (auto& error : errors) error.path = path;
    return errors;
}

std::vector<Diagnostic> located(std::vector<Diagnostic> errors, const std::string& prefix) {
    for (auto& error : errors) error.path = prefix + error.path;
    return errors;
}

std::vector<Diagnostic> validate_profile(const FontProfile& profile) {
    if (profile.version != font_profile_version)
        return failure("unsupported_version", "/version", "Expected font profile version 1.");
    const std::pair<std::size_t, const char*> counts[] = {
        {profile.assets.size(), "/assets"}, {profile.faces.size(), "/faces"},
        {profile.fallbacks.size(), "/fallbacks"}, {profile.families.size(), "/families"},
        {profile.aliases.size(), "/aliases"}};
    for (const auto& [count, path] : counts)
        if (count > max_font_profile_entries)
            return failure("out_of_range", path, "Expected at most 64 entries.");
    if (profile.assets.empty() || profile.faces.empty())
        return failure("out_of_range", profile.assets.empty() ? "/assets" : "/faces", "Expected at least one entry.");

    std::map<std::string, std::size_t, std::less<>> assets;
    std::size_t total = 0;
    for (std::size_t i = 0; i < profile.assets.size(); ++i) {
        const auto& asset = profile.assets[i];
        const auto path = "/assets/" + std::to_string(i);
        for (const auto& [name, field] : {std::pair{std::string_view(asset.id), "/id"},
                                        std::pair{std::string_view(asset.version), "/version"}}) {
            auto errors = identifier(name, path + field);
            if (!errors.empty()) return errors;
        }
        if (!assets.emplace(asset.id, i).second)
            return failure("duplicate_font_asset", path + "/id", "Asset IDs must be unique.");
        if (asset.bytes.empty())
            return failure("invalid_font", path + "/bytes", "Supply explicit non-empty font bytes.");
        if (asset.bytes.size() > max_font_profile_bytes - total)
            return failure("out_of_range", path + "/bytes", "Total font asset bytes exceed 256 MiB.");
        total += asset.bytes.size();
    }
    total = 0;
    std::set<std::uint32_t> faces;
    for (std::size_t i = 0; i < profile.faces.size(); ++i) {
        const auto& face = profile.faces[i];
        const auto path = "/faces/" + std::to_string(i);
        if (!faces.insert(face.font.value).second)
            return failure("duplicate_font", path + "/font", "Concrete face IDs must be unique.");
        auto errors = identifier(face.asset, path + "/asset");
        if (!errors.empty()) return errors;
        const auto asset = assets.find(face.asset);
        if (asset == assets.end())
            return failure("unknown_font_asset", path + "/asset", "Reference an asset declared in this profile.");
        const auto size = profile.assets[asset->second].bytes.size();
        if (size > max_font_profile_bytes - total)
            return failure("out_of_range", path + "/asset", "Expanded face bytes exceed 256 MiB.");
        total += size;
    }
    std::set<std::uint32_t> fallbacks;
    for (std::size_t i = 0; i < profile.fallbacks.size(); ++i) {
        const auto path = "/fallbacks/" + std::to_string(i);
        if (!fallbacks.insert(profile.fallbacks[i].font.value).second)
            return failure("duplicate_font", path + "/font", "Declare a fallback stack only once per face.");
        if (profile.fallbacks[i].faces.size() > max_font_profile_entries)
            return failure("out_of_range", path + "/fallback", "Expected at most 64 fallback faces.");
    }
    std::set<std::string, std::less<>> families;
    for (std::size_t i = 0; i < profile.families.size(); ++i)
        if (!families.insert(profile.families[i].name).second)
            return failure("duplicate_font_family", "/families/" + std::to_string(i) + "/name", "Family names must be unique.");
    std::set<std::uint32_t> aliases;
    std::set<std::string, std::less<>> names;
    for (std::size_t i = 0; i < profile.aliases.size(); ++i) {
        const auto& alias = profile.aliases[i];
        const auto path = "/aliases/" + std::to_string(i);
        if (!aliases.insert(alias.font.value).second || !names.insert(alias.name).second)
            return failure("font_alias_conflict", path, "Alias IDs and names must each be unique.");
    }
    return {};
}

} // namespace

ProfileFontShaper::ProfileFontShaper(FontProfile profile, FontShaper shaper)
    : profile_(std::move(profile)), shaper_(std::move(shaper)) {}

Result<FontShaper> detail::build_profile_shaper(const FontProfile& profile) {
    auto errors = validate_profile(profile);
    if (!errors.empty()) return {{}, std::move(errors)};
    FontShaper shaper;
    // Register by dependency order, irrespective of the order of declarations.
    for (std::size_t i = 0; i < profile.faces.size(); ++i) {
        const auto& face = profile.faces[i];
        const auto asset = std::find_if(profile.assets.begin(), profile.assets.end(),
                                        [&](const auto& candidate) { return candidate.id == face.asset; });
        errors = shaper.set_face(face.font, asset->bytes, face.face_index);
        // Registration's /font denotes the bytes/index pair; locate their declaration.
        for (auto& error : errors) error.path = "/faces/" + std::to_string(i);
        if (!errors.empty()) return {{}, std::move(errors)};
    }
    for (std::size_t i = 0; i < profile.fallbacks.size(); ++i) {
        const auto& stack = profile.fallbacks[i];
        errors = located(shaper.set_fallback(stack.font, stack.faces), "/fallbacks/" + std::to_string(i));
        for (auto& error : errors) {
            const auto field = error.path.find("/fallback/");
            if (field != std::string::npos) error.path.replace(field, 10, "/faces/");
        }
        if (!errors.empty()) return {{}, std::move(errors)};
    }
    for (std::size_t i = 0; i < profile.families.size(); ++i) {
        const auto& family = profile.families[i];
        errors = located(shaper.set_family(family.name, family.faces), "/families/" + std::to_string(i));
        if (!errors.empty()) return {{}, std::move(errors)};
    }
    for (std::size_t i = 0; i < profile.aliases.size(); ++i) {
        const auto& alias = profile.aliases[i];
        errors = located(shaper.set_alias(alias.font, alias.name, alias.families), "/aliases/" + std::to_string(i));
        if (!errors.empty()) return {{}, std::move(errors)};
    }
    return {std::move(shaper), {}};
}

Result<ProfileFontShaper> ProfileFontShaper::create(FontProfile profile) {
    auto shaper = detail::build_profile_shaper(profile);
    if (!shaper) return {{}, std::move(shaper.diagnostics)};
    return {ProfileFontShaper(std::move(profile), std::move(*shaper.value)), {}};
}

std::optional<FontId> ProfileFontShaper::find_alias(std::string_view name) const { return shaper_.find_alias(name); }
std::uint64_t ProfileFontShaper::face_revision(FontId font) const { return shaper_.face_revision(font); }
Result<GlyphBitmap> ProfileFontShaper::rasterize(const GlyphRasterRequest& request) { return shaper_.rasterize(request); }
Result<TextMetrics> ProfileFontShaper::measure(std::string_view text, const TextStyle& style, const TextConstraints& constraints) {
    return shaper_.measure(text, style, constraints);
}
Result<GlyphRun> ProfileFontShaper::shape(std::string_view text, const TextStyle& style, const TextConstraints& constraints) {
    return shaper_.shape(text, style, constraints);
}

} // namespace tessera
