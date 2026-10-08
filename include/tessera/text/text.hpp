#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/ui/document.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace tessera {

// Host-assigned logical font identity. Value 0 selects the host default font.
struct FontId {
    std::uint32_t value = 0;
    bool operator==(const FontId&) const = default;
};

struct TextStyle {
    FontId font;
    float size = 16;                  // Logical units per em; finite and positive.
    std::optional<float> line_height; // Logical units; absent selects the shaper's default.
    std::uint16_t weight = 400;       // 1-1000.
    bool operator==(const TextStyle&) const = default;
};

struct TextMetrics {
    Size size;              // Widest line advance by lines * line_height.
    float baseline = 0;     // First-line baseline below the top of the text box.
    float line_height = 0;
    std::uint32_t lines = 0;
    bool operator==(const TextMetrics&) const = default;
};

struct TextConstraints {
    // Absent preserves LF-only lines. Finite, non-negative logical width enables wrapping.
    // An indivisible unit under the shaper's wrapping policy may overflow, including at width zero.
    std::optional<float> max_width;
    bool operator==(const TextConstraints&) const = default;
};

struct Glyph {
    std::uint32_t id = 0;      // Font-specific glyph index.
    std::uint32_t cluster = 0; // UTF-8 byte offset of the source cluster.
    Point position;            // Baseline pen position relative to the text box top left.
    FontId font;               // Font whose face `id` indexes; differs from the run's font only by fallback.
    bool operator==(const Glyph&) const = default;
};

struct GlyphRun {
    FontId font; // Requested font (TextStyle::font).
    float size = 0;
    std::vector<Glyph> glyphs;
    TextMetrics metrics; // Equal to measure() for the same text, style, and constraints.
    bool operator==(const GlyphRun&) const = default;
};

// Borrowed by layout and paint for one call; implementations may cache internally.
// Results are owned values. Implementation (font library) types never cross this boundary.
class TextShaper {
public:
    virtual ~TextShaper() = default;
    virtual Result<TextMetrics> measure(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) = 0;
    virtual Result<GlyphRun> shape(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) = 0;

protected:
    TextShaper() = default;
    TextShaper(const TextShaper&) = default;
    TextShaper& operator=(const TextShaper&) = default;
};

std::vector<Diagnostic> validate(const TextStyle&, std::string_view path = "");
// Input checks shared by every shaper: style errors under /style, width errors under
// /constraints, then text_too_long (above 4 GiB) or invalid_utf8 at /text.
std::vector<Diagnostic> validate_text_input(std::string_view utf8, const TextStyle&, const TextConstraints& = {});

// Deterministic metrics without font data, for geometry tests and placeholder Text.
// Every Unicode scalar advances 0.5 em; LF starts a new line; constrained lines wrap by scalar.
// The default line height is 1.25 em, with a 0.8 em ascent centered in each line.
// Glyph IDs are Unicode scalar values.
class PlaceholderTextShaper final : public TextShaper {
public:
    static constexpr float advance_em = 0.5f;
    static constexpr float line_height_em = 1.25f;
    static constexpr float ascent_em = 0.8f;
    Result<TextMetrics> measure(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;
    Result<GlyphRun> shape(std::string_view utf8, const TextStyle&, const TextConstraints& = {}) override;
};

} // namespace tessera
