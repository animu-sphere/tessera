#pragma once

#include <tessera/text/text.hpp>
#include <tessera/text/glyph_cache.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace tessera {

// Host-declared weight of a registered face; no metadata inference or synthetic weight.
struct FontFamilyFace {
    FontId font;
    std::uint16_t weight = 400;
    bool operator==(const FontFamilyFace&) const = default;
};

// Real-font shaping and grayscale rasterization over shared host-supplied font bytes.
// Font library types stay inside this optional module; the host owns font discovery and
// asset loading. Single-threaded, non-reentrant; mutate fonts only at update points.
//
// A FontId names either a concrete face or a logical alias to an ordered family stack.
// Aliases select the nearest declared weight in each family (lower weight wins ties).
// Concrete faces ignore weight; variable fonts use their default instance.
// Constrained LTR lines prefer spaces, hyphens and Japanese cluster
// boundaries under the bounded menu profile in docs/design/text.md. Punctuation/glue
// groups stay intact even when oversized; long words may split at clusters. A FontId may
// name an ordered fallback stack of other FontIds; clusters its face cannot map are shaped again with the next face. Each
// LF-separated line is shaped as one segment with guessed direction/script and the fixed
// language "und"; bidirectional reordering and script itemization are not performed.
class FontShaper final : public TextShaper, public GlyphRasterizer {
public:
    FontShaper();
    ~FontShaper() override;
    FontShaper(FontShaper&&) noexcept;
    FontShaper& operator=(FontShaper&&) noexcept;

    // Registers or replaces the face for `font` (FontId 0 is the default font) from scalable TTF/OTF
    // bytes, or one face of a TTC/OTC collection. Rejected data leaves the previous face in
    // place. Replacing a face changes geometry; the host must lay out and paint again.
    std::vector<Diagnostic> set_face(FontId font, std::vector<std::byte> data, std::uint32_t face_index = 0);
    bool has_face(FontId font) const;
    // Shared registration for shaping and grayscale rasterization. Successful set_face
    // changes this revision; failed replacement leaves both services and revision intact.
    std::uint64_t face_revision(FontId font) const override;
    // Default variation instance, unhinted outline without stem darkening, normal 8-bit coverage, baseline phase
    // zero. No embedded bitmap, LCD, color, synthetic weight, or shaping advances here.
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) override;
    // Sets the faces tried, in order, for clusters the face of `font` cannot map; an empty list
    // clears the stack. Every FontId needs a registered face and may appear once, excluding
    // `font` itself. Stacks are not transitive. Rejected stacks leave the previous one in place.
    std::vector<Diagnostic> set_fallback(FontId font, std::vector<FontId> fallbacks);

    // Registers/replaces a named family of 1..64 concrete faces, with distinct weights
    // in 1..1000. Names are case-sensitive UTF-8, 1..256 bytes, without ASCII controls.
    // Rejected updates preserve the previous family. Faces cannot be removed.
    std::vector<Diagnostic> set_family(std::string name, std::vector<FontFamilyFace> faces);
    // Registers/rebinds a stable logical FontId/name to 1..64 distinct registered families.
    // An alias cannot collide with a face, change its name, or share another alias's name.
    // Aliases use only their explicit family stack, not concrete-face fallback stacks;
    // repeated selected faces are tried once. Rejected updates preserve the old binding.
    std::vector<Diagnostic> set_alias(FontId font, std::string name, std::vector<std::string> families);
    // Resolves a logical name for host-built styles. No implicit defaults or filesystem access.
    std::optional<FontId> find_alias(std::string_view name) const;

    Result<TextMetrics> measure(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;
    Result<GlyphRun> shape(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tessera
