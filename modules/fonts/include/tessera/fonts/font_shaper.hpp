#pragma once

#include <tessera/text/text.hpp>
#include <cstddef>
#include <memory>
#include <vector>

namespace tessera {

// Real-font TextShaper over host-supplied font bytes. Font library types stay inside this
// optional module; the host owns font discovery and asset loading.
//
// One face per FontId: weight is validated but selects no face, variable fonts use their
// default instance. Constrained LTR lines wrap at shaped cluster boundaries (no word/kinsoku
// rules); an oversized cluster stays intact. A FontId may name an ordered fallback stack of
// other FontIds; clusters its face cannot map are shaped again with the next face. Each
// LF-separated line is shaped as one segment with guessed direction/script and the fixed
// language "und"; bidirectional reordering and script itemization are not performed.
class FontShaper final : public TextShaper {
public:
    FontShaper();
    ~FontShaper() override;
    FontShaper(FontShaper&&) noexcept;
    FontShaper& operator=(FontShaper&&) noexcept;

    // Registers or replaces the face for `font` (FontId 0 is the default font) from TTF/OTF
    // bytes, or one face of a TTC/OTC collection. Rejected data leaves the previous face in
    // place. Replacing a face changes geometry; the host must lay out and paint again.
    std::vector<Diagnostic> set_face(FontId font, std::vector<std::byte> data, std::uint32_t face_index = 0);
    bool has_face(FontId font) const;
    // Sets the faces tried, in order, for clusters the face of `font` cannot map; an empty list
    // clears the stack. Every FontId needs a registered face and may appear once, excluding
    // `font` itself. Stacks are not transitive. Rejected stacks leave the previous one in place.
    std::vector<Diagnostic> set_fallback(FontId font, std::vector<FontId> fallbacks);

    Result<TextMetrics> measure(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;
    Result<GlyphRun> shape(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tessera
