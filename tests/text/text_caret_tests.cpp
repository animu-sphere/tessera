#include <tessera/text/text_caret.hpp>
#include "../check.hpp"
#include <iostream>
#include <string>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
template<class T> T take(Result<T> result) {
    check(static_cast<bool>(result), "Caret layout rejected valid text");
    return std::move(*result.value);
}
using Stops = std::vector<TextCaretStop>;
TextStyle style20() {
    TextStyle style;
    style.size = 20; // Placeholder advance 10, line height 25.
    return style;
}

void hard_lines() {
    PlaceholderTextShaper shaper;
    const auto layout = take(layout_text_carets(shaper, "ab\n\n日本", style20()));
    check(layout.line_height == 25 && layout.lines.size() == 3, "LF lines differ");
    check(layout.lines[0] == TextCaretLine{0, 2, true, 0, Stops{{0, 0}, {1, 10}, {2, 20}}}, "First line stops differ");
    check(layout.lines[1] == TextCaretLine{3, 3, true, 25, Stops{{3, 0}}}, "An empty line keeps one stop");
    check(layout.lines[2] == TextCaretLine{4, 10, true, 50, Stops{{4, 0}, {7, 10}, {10, 20}}},
          "UTF-8 stops follow cluster offsets");
    check(layout.line_of(2) == 0 && layout.line_of(3) == 1 && layout.line_of(4) == 2 && layout.line_of(99) == 2,
          "The LF offset ends its own line");
    check(layout.caret(2) == Rect{{20, 0}, {1, 25}} && layout.caret(3) == Rect{{0, 25}, {1, 25}} &&
              layout.caret(10) == Rect{{20, 50}, {1, 25}},
          "Caret rectangles follow lines and stops");
    check(layout.caret(5) == layout.caret(4), "An offset inside a scalar presents at its start");
    check(layout.offset_at({14, 1}) == 1 && layout.offset_at({16, 1}) == 2 && layout.offset_at({15, 1}) == 1 &&
              layout.offset_at({99, 30}) == 3 && layout.offset_at({-5, 60}) == 4 && layout.offset_at({99, -10}) == 2 &&
              layout.offset_at({99, 1000}) == 10,
          "Point lookup clamps lines and takes the nearest, earlier stop");
    check(layout.line_end(0) == 2 && layout.line_end(1) == 3, "Hard lines end at their LF");

    const auto empty = take(layout_text_carets(shaper, "", style20()));
    check(empty.lines == std::vector<TextCaretLine>{{0, 0, true, 0, Stops{{0, 0}}}} &&
              empty.caret(0) == Rect{{0, 0}, {1, 25}},
          "Empty text keeps one caret line");
    const auto trailing = take(layout_text_carets(shaper, "a\n", style20()));
    check(trailing.lines.size() == 2 && trailing.lines[1] == TextCaretLine{2, 2, true, 25, Stops{{2, 0}}} &&
              trailing.caret(2).origin == Point{0, 25},
          "A trailing LF opens an empty last line");
}

void wrapped_lines() {
    PlaceholderTextShaper shaper;
    const auto layout = take(layout_text_carets(shaper, "abcd\nef", style20(), {20.f}));
    check(layout.lines.size() == 3, "Wrapped rows differ");
    check(layout.lines[0] == TextCaretLine{0, 2, false, 0, Stops{{0, 0}, {1, 10}}} &&
              layout.lines[1] == TextCaretLine{2, 4, true, 25, Stops{{2, 0}, {3, 10}, {4, 20}}} &&
              layout.lines[2] == TextCaretLine{5, 7, true, 50, Stops{{5, 0}, {6, 10}, {7, 20}}},
          "A soft line ends at the next row's first stop");
    check(layout.line_of(1) == 0 && layout.line_of(2) == 1 && layout.caret(2).origin == Point{0, 25},
          "A wrap boundary presents at the start of the following line");
    check(layout.line_end(0) == 1 && layout.line_end(1) == 4, "A soft line ends before its wrap boundary");
    check(layout.offset_at(0, 99) == 1 && layout.offset_at(1, 99) == 4 && layout.offset_at(7, 0) == 5,
          "Line lookup excludes a soft end and clamps the line index");
}

// A shaper with one two-scalar cluster, standing in for a ligature.
class LigatureShaper final : public TextShaper {
public:
    Result<TextMetrics> measure(std::string_view text, const TextStyle&, const TextConstraints&) override {
        ++measures;
        return {TextMetrics{{static_cast<float>(text.size()) * 7, 10}, 8, 10, 1}, {}};
    }
    Result<GlyphRun> shape(std::string_view text, const TextStyle& style, const TextConstraints&) override {
        if (text != "fix") return {std::nullopt, {{"unexpected", Severity::error, "/text", "Unexpected text"}}};
        return {GlyphRun{style.font, style.size, {{1, 0, {0, 8}, {}}, {2, 2, {12, 8}, {}}}, {{19, 10}, 8, 10, 1}}, {}};
    }
    int measures = 0;
};

void clusters_and_failures() {
    LigatureShaper ligature;
    const auto layout = take(layout_text_carets(ligature, "fix", {}));
    check(layout.lines == std::vector<TextCaretLine>{{0, 3, true, 0, Stops{{0, 0}, {2, 12}, {3, 21}}}} &&
              ligature.measures == 1,
          "Stops use cluster starts and one measured line end");
    check(layout.caret(1).origin.x == 0, "An offset inside a cluster presents at the cluster start");
    check(has(layout_text_carets(ligature, "other", {}).diagnostics, "unexpected", "/text"),
          "Shaping diagnostics pass through");
    PlaceholderTextShaper shaper;
    check(has(layout_text_carets(shaper, "\xff", {}).diagnostics, "invalid_utf8", "/text") &&
              has(layout_text_carets(shaper, "a", {}, {-1.f}).diagnostics, "out_of_range", "/constraints/max_width"),
          "Shaper input validation applies");
}
} // namespace

int main() {
    try {
        hard_lines();
        wrapped_lines();
        clusters_and_failures();
        std::cout << "Text caret line, stop, wrap and lookup checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
