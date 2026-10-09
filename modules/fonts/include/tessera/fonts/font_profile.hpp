#pragma once

#include <tessera/fonts/font_shaper.hpp>

namespace tessera {

// Owned in-process inputs, not a serialized replay or snapshot format. Version 1 uses
// FontShaper's fixed shaping, menu wrapping, default variation and grayscale policies.
inline constexpr std::uint32_t font_profile_version = 1;
inline constexpr std::size_t max_font_profile_entries = 64;
inline constexpr std::size_t max_font_profile_bytes = 256 * 1024 * 1024;

struct FontProfileAsset {
    std::string id;      // Host fixture identity, not a path or provider lookup.
    std::string version; // Host-declared asset version; bytes are the authoritative input.
    std::vector<std::byte> bytes;
    bool operator==(const FontProfileAsset&) const = default;
};
struct FontProfileFace {
    FontId font;
    std::string asset;
    std::uint32_t face_index = 0;
    bool operator==(const FontProfileFace&) const = default;
};
struct FontProfileFallback {
    FontId font;
    std::vector<FontId> faces;
    bool operator==(const FontProfileFallback&) const = default;
};
struct FontProfileFamily {
    std::string name;
    std::vector<FontFamilyFace> faces;
    bool operator==(const FontProfileFamily&) const = default;
};
struct FontProfileAlias {
    FontId font;
    std::string name;
    std::vector<std::string> families;
    bool operator==(const FontProfileAlias&) const = default;
};
struct FontProfile {
    std::uint32_t version = font_profile_version;
    std::vector<FontProfileAsset> assets;
    std::vector<FontProfileFace> faces;
    std::vector<FontProfileFallback> fallbacks;
    std::vector<FontProfileFamily> families;
    std::vector<FontProfileAlias> aliases;
    bool operator==(const FontProfile&) const = default;
};

// A sealed service for replay/capture. Host loads explicit bytes before create();
// creation validates all registrations into a fresh shaper and publishes no partial
// service. Keep it alive and at a stable address while sessions/caches borrow it.
// Single-threaded and non-reentrant, as FontShaper. A moved-from service cannot be used.
class ProfileFontShaper final : public TextShaper, public GlyphRasterizer {
public:
    static Result<ProfileFontShaper> create(FontProfile);
    ProfileFontShaper(ProfileFontShaper&&) noexcept = default;
    ProfileFontShaper& operator=(ProfileFontShaper&&) noexcept = default;

    const FontProfile& profile() const noexcept { return profile_; }
    std::optional<FontId> find_alias(std::string_view name) const;
    std::uint64_t face_revision(FontId) const override;
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) override;
    Result<TextMetrics> measure(std::string_view, const TextStyle&, const TextConstraints& = {}) override;
    Result<GlyphRun> shape(std::string_view, const TextStyle&, const TextConstraints& = {}) override;

private:
    ProfileFontShaper(FontProfile, FontShaper);
    FontProfile profile_;
    FontShaper shaper_;
};

} // namespace tessera
