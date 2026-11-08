#pragma once

// Application-local fixed-cell text for the Inspector. It keeps the placeholder
// shaper's 0.5 em cell, line height and wrapping, but gives wide East Asian
// scalars two cells so Japanese names and IME preedit stay legible. This is a
// presentation policy for the reference application, not Unicode East Asian
// Width conformance, shaping, fallback or grapheme segmentation.
#include <tessera/text/text.hpp>
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace inspector_text {
// Common wide/fullwidth blocks: Hangul Jamo, CJK punctuation through
// compatibility, Hangul syllables, compatibility ideographs, fullwidth forms,
// pictographic emoji and the supplementary ideographic planes.
inline unsigned cells(std::uint32_t scalar) {
    constexpr std::uint32_t ranges[][2] = {
        {0x1100, 0x115f}, {0x2e80, 0x303e},   {0x3041, 0x33ff},   {0x3400, 0x4dbf},   {0x4e00, 0x9fff},
        {0xa000, 0xa4cf}, {0xac00, 0xd7a3},   {0xf900, 0xfaff},   {0xfe30, 0xfe4f},   {0xff00, 0xff60},
        {0xffe0, 0xffe6}, {0x1f300, 0x1f64f}, {0x1f900, 0x1f9ff}, {0x20000, 0x3fffd},
    };
    for (const auto& range : ranges)
        if (scalar >= range[0] && scalar <= range[1]) return 2;
    return 1;
}

class CellTextShaper final : public tessera::TextShaper {
public:
    tessera::Result<tessera::TextMetrics> measure(std::string_view utf8, const tessera::TextStyle& style,
                                                  const tessera::TextConstraints& constraints = {}) override {
        auto shaped = shape(utf8, style, constraints);
        if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
        return {shaped.value->metrics, {}};
    }
    tessera::Result<tessera::GlyphRun> shape(std::string_view utf8, const tessera::TextStyle& style,
                                             const tessera::TextConstraints& constraints = {}) override {
        using tessera::PlaceholderTextShaper;
        if (auto errors = tessera::validate_text_input(utf8, style, constraints); !errors.empty())
            return {std::nullopt, std::move(errors)};
        tessera::GlyphRun run{style.font, style.size, {}, {}};
        const float line = style.line_height.value_or(style.size * PlaceholderTextShaper::line_height_em);
        const float advance = style.size * PlaceholderTextShaper::advance_em;
        run.metrics.line_height = line;
        run.metrics.baseline = (line - style.size) / 2 + style.size * PlaceholderTextShaper::ascent_em;
        std::uint32_t lines = 1;
        std::size_t column = 0, widest = 0;
        for (std::size_t i = 0; i < utf8.size();) {
            const auto start = static_cast<std::uint32_t>(i);
            const auto scalar = decode(utf8, i);
            if (scalar == '\n') {
                widest = std::max(widest, column);
                column = 0;
                ++lines;
                continue;
            }
            const auto width = cells(scalar);
            if (constraints.max_width && column > 0 &&
                static_cast<float>(column + width) * advance > *constraints.max_width) {
                widest = std::max(widest, column);
                column = 0;
                ++lines;
            }
            run.glyphs.push_back(
                {scalar,
                 start,
                 {static_cast<float>(column) * advance, run.metrics.baseline + static_cast<float>(lines - 1) * line},
                 style.font});
            column += width;
        }
        widest = std::max(widest, column);
        run.metrics.size = {static_cast<float>(widest) * advance, static_cast<float>(lines) * line};
        run.metrics.lines = lines;
        return {std::move(run), {}};
    }

private:
    // Input is valid UTF-8 after validate_text_input.
    static std::uint32_t decode(std::string_view text, std::size_t& i) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) return first;
        const unsigned count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : 1;
        std::uint32_t scalar = first & (0x3fu >> count);
        for (unsigned n = 0; n < count; ++n) scalar = (scalar << 6) | (static_cast<unsigned char>(text[i++]) & 0x3fu);
        return scalar;
    }
};
} // namespace inspector_text
