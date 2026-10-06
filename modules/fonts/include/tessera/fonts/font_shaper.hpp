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
// default instance, and there is no fallback or wrapping. Each LF-separated line is shaped as
// one segment with guessed direction/script and the fixed language "und"; bidirectional
// reordering and script itemization are not performed.
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

    Result<TextMetrics> measure(std::string_view utf8, const TextStyle&) override;
    Result<GlyphRun> shape(std::string_view utf8, const TextStyle&) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tessera
