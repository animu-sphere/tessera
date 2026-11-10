#include <tessera/text/text_caret.hpp>
#include <algorithm>
#include <cmath>

namespace tessera {

std::size_t TextCaretLayout::line_of(std::size_t offset) const {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (offset < line.begin) return i ? i - 1 : 0;
        if (offset < line.end || (offset == line.end && line.hard)) return i;
    }
    return lines.empty() ? 0 : lines.size() - 1;
}

Rect TextCaretLayout::caret(std::size_t offset) const {
    if (lines.empty()) return {{0, 0}, {1, line_height}};
    const auto& line = lines[line_of(offset)];
    float x = line.stops.empty() ? 0 : line.stops.front().x;
    for (const auto& stop : line.stops) {
        if (stop.offset > offset) break;
        x = stop.x;
    }
    return {{x, line.top}, {1, line_height}};
}

std::size_t TextCaretLayout::offset_at(std::size_t index, float x) const {
    if (lines.empty()) return 0;
    const auto& line = lines[std::min(index, lines.size() - 1)];
    std::size_t best = line.begin;
    float distance = INFINITY;
    for (const auto& stop : line.stops)
        if (const float d = std::abs(stop.x - x); d < distance) {
            distance = d;
            best = stop.offset;
        }
    return best;
}

std::size_t TextCaretLayout::offset_at(Point point) const {
    if (lines.empty() || !(line_height > 0) || !(point.y > 0)) return offset_at(0, point.x);
    const float row = std::floor(point.y / line_height);
    const auto last = lines.size() - 1;
    return offset_at(row >= static_cast<float>(last) ? last : static_cast<std::size_t>(row), point.x);
}

std::size_t TextCaretLayout::line_end(std::size_t index) const {
    if (lines.empty()) return 0;
    const auto& line = lines[std::min(index, lines.size() - 1)];
    return line.hard || line.stops.empty() ? line.end : line.stops.back().offset;
}

Result<TextCaretLayout> layout_text_carets(TextShaper& shaper, std::string_view utf8, const TextStyle& style,
                                           const TextConstraints& constraints) {
    auto shaped = shaper.shape(utf8, style, constraints);
    if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
    auto glyphs = shaped.value->glyphs;
    std::stable_sort(glyphs.begin(), glyphs.end(),
                     [](const Glyph& a, const Glyph& b) { return a.cluster < b.cluster; });
    TextCaretLayout layout;
    layout.line_height = shaped.value->metrics.line_height;
    std::size_t next = 0; // Next glyph in cluster order.
    float top = 0;
    // Closes `line` at `end`; a hard line's end stop follows its measured advance.
    const auto close = [&](TextCaretLine& line, std::size_t end, bool hard) -> std::vector<Diagnostic> {
        line.end = end;
        line.hard = hard;
        if (line.stops.empty() || line.stops.front().offset != line.begin)
            line.stops.insert(line.stops.begin(), {line.begin, 0});
        if (hard && end > line.begin) {
            auto measured = shaper.measure(utf8.substr(line.begin, end - line.begin), style);
            if (!measured) return std::move(measured.diagnostics);
            line.stops.push_back({end, line.stops.front().x + measured.value->size.width});
        }
        layout.lines.push_back(std::move(line));
        top += layout.line_height;
        return {};
    };
    for (std::size_t begin = 0;;) {
        const auto found = utf8.find('\n', begin);
        const auto end = found == std::string_view::npos ? utf8.size() : found;
        while (next < glyphs.size() && glyphs[next].cluster < begin) ++next; // Glyphs inside an LF, if any.
        TextCaretLine line{begin, begin, true, top, {}};
        std::optional<float> row;
        for (; next < glyphs.size() && glyphs[next].cluster < end; ++next) {
            const auto& glyph = glyphs[next];
            if (row && glyph.position.y != *row && glyph.cluster > line.begin) {
                if (auto errors = close(line, glyph.cluster, false); !errors.empty())
                    return {std::nullopt, std::move(errors)};
                line = {glyph.cluster, glyph.cluster, true, top, {}};
            }
            row = glyph.position.y;
            if (line.stops.empty() || line.stops.back().offset != glyph.cluster)
                line.stops.push_back({glyph.cluster, glyph.position.x});
            else
                line.stops.back().x = std::min(line.stops.back().x, glyph.position.x);
        }
        if (auto errors = close(line, end, true); !errors.empty()) return {std::nullopt, std::move(errors)};
        if (found == std::string_view::npos) break;
        begin = found + 1;
    }
    return {std::move(layout), {}};
}

} // namespace tessera
