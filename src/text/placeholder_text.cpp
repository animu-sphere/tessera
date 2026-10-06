#include <tessera/text/text.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <limits>
#include <string>

namespace tessera {
namespace {
// Requires valid UTF-8.
std::uint32_t decode(std::string_view text, std::size_t& i) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) return first;
    const unsigned count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : 1;
    std::uint32_t scalar = first & (0x3fu >> count);
    for (unsigned n = 0; n < count; ++n) scalar = (scalar << 6) | (static_cast<unsigned char>(text[i++]) & 0x3fu);
    return scalar;
}
}

std::vector<Diagnostic> validate(const TextStyle& style, std::string_view base) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    const std::string path(base);
    check.positive(style.size, path + "/size");
    if (style.line_height) check.positive(*style.line_height, path + "/line_height");
    if (style.weight < 1 || style.weight > 1000)
        check.error("out_of_range", path + "/weight", "Expected a font weight in [1, 1000].");
    return errors;
}

std::vector<Diagnostic> validate_text_input(std::string_view utf8, const TextStyle& style) {
    auto errors = validate(style, "/style");
    if (utf8.size() > std::numeric_limits<std::uint32_t>::max())
        errors.push_back({"text_too_long", Severity::error, "/text", "Text exceeds 4 GiB of UTF-8.", {}});
    else if (!detail::valid_utf8(utf8))
        errors.push_back({"invalid_utf8", Severity::error, "/text", "Text must be valid UTF-8.", {}});
    return errors;
}

Result<GlyphRun> PlaceholderTextShaper::shape(std::string_view utf8, const TextStyle& style) {
    if (auto errors = validate_text_input(utf8, style); !errors.empty()) return {std::nullopt, std::move(errors)};

    GlyphRun run{style.font, style.size, {}, {}};
    const float line = style.line_height.value_or(style.size * line_height_em);
    const float advance = style.size * advance_em;
    run.metrics.line_height = line;
    run.metrics.baseline = (line - style.size) / 2 + style.size * ascent_em;
    std::uint32_t lines = 1;
    std::size_t column = 0;
    std::size_t widest = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto start = static_cast<std::uint32_t>(i);
        const auto scalar = decode(utf8, i);
        if (scalar == '\n') {
            widest = std::max(widest, column);
            column = 0;
            ++lines;
            continue;
        }
        run.glyphs.push_back({scalar, start, {static_cast<float>(column) * advance,
                                              run.metrics.baseline + static_cast<float>(lines - 1) * line},
                              style.font});
        ++column;
    }
    widest = std::max(widest, column);
    run.metrics.size = {static_cast<float>(widest) * advance, static_cast<float>(lines) * line};
    run.metrics.lines = lines;
    return {std::move(run), {}};
}

Result<TextMetrics> PlaceholderTextShaper::measure(std::string_view utf8, const TextStyle& style) {
    // Measurement is derived from shaping so the two can never disagree.
    auto shaped = shape(utf8, style);
    if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
    return {shaped.value->metrics, {}};
}

} // namespace tessera
