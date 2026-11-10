#pragma once

#include <tessera/text/text.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tessera {

// One caret position: a UTF-8 byte offset and its x relative to the text box left.
struct TextCaretStop {
    std::size_t offset = 0;
    float x = 0;
    bool operator==(const TextCaretStop&) const = default;
};

// One visual line of shaped text. [begin, end) are UTF-8 bytes excluding any LF. A hard
// line ends at an LF or the text end; a soft line ends where wrapping starts the next.
struct TextCaretLine {
    std::size_t begin = 0, end = 0;
    bool hard = true;
    float top = 0; // Relative to the text box top.
    // Increasing offsets: the line start, each later shaped cluster start and, for a hard
    // line, its end. A soft line's end is the next line's first stop.
    std::vector<TextCaretStop> stops;
    bool operator==(const TextCaretLine&) const = default;
};

// Logical caret geometry for one text box, derived from the shaped run that layout and
// paint use for the same text, style and constraints. Visual lines follow LF and the
// run's wrapped glyph rows; stop x values come from glyph pen positions, and a hard line's
// end from measuring the line. Caret stops sit on shaped cluster boundaries, so offsets
// inside a ligature or mark cluster present at the cluster start. Geometry assumes
// left-to-right lines; it does not reorder bidirectional text.
struct TextCaretLayout {
    std::vector<TextCaretLine> lines; // At least one line, including for empty text.
    float line_height = 0;
    bool operator==(const TextCaretLayout&) const = default;

    // The line presenting `offset`. A wrap boundary belongs to the following line; offsets
    // past the text clamp to the last line.
    std::size_t line_of(std::size_t offset) const;
    // A 1-unit-wide, line-height-tall rectangle at the last stop at or before `offset`.
    Rect caret(std::size_t offset) const;
    // The stop nearest `x` on `line`, clamped to the existing lines; ties take the earlier stop.
    std::size_t offset_at(std::size_t line, float x) const;
    // The stop nearest `point` on the line containing its y, clamped to the first and last line.
    std::size_t offset_at(Point point) const;
    // The last caret offset that still presents on `line`: its end for a hard line, or its last
    // stop for a soft line, whose end presents at the start of the next line.
    std::size_t line_end(std::size_t line) const;
};

// Shapes `utf8` once with the supplied constraints, then measures each hard line's text
// without constraints. Shaping and measurement diagnostics pass through unchanged.
Result<TextCaretLayout> layout_text_carets(TextShaper&, std::string_view utf8, const TextStyle&,
                                           const TextConstraints& = {});

} // namespace tessera
