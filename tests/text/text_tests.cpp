#include <tessera/text/text.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <string>

using tessera::test::check;
using tessera::test::has;

namespace {

void placeholder_metrics() {
    tessera::PlaceholderTextShaper shaper;
    tessera::TextStyle style;
    style.size = 20; // Advance 10, line height 25, baseline 2.5 + 16.
    const std::string text = "Ab\nメニュー";
    const auto run = shaper.shape(text, style);
    check(static_cast<bool>(run), "Valid text rejected");
    const tessera::TextMetrics expected{{40, 50}, 18.5f, 25, 2};
    check(run.value->metrics == expected, "Placeholder metrics differ");
    check(shaper.measure(text, style).value == expected, "Measurement and shaping disagree");
    check(run.value->glyphs.size() == 6, "Line feed must not produce a glyph");
    const auto& me = run.value->glyphs[2];
    check(me.id == 0x30e1 && me.cluster == 3 && me.position == tessera::Point{0, 43.5f}, "Second-line glyph differs");
    check(run.value->glyphs[3].cluster == 6 && run.value->glyphs[3].position.x == 10, "UTF-8 cluster offsets differ");

    const auto empty = shaper.measure("", style);
    check(empty.value == tessera::TextMetrics{{0, 25}, 18.5f, 25, 1}, "Empty text should be one empty line");
    style.line_height = 30.0f;
    check(shaper.measure("x", style).value->baseline == 21, "Explicit line height must center the ascent");
}

void failures() {
    tessera::PlaceholderTextShaper shaper;
    const auto invalid = shaper.shape("\xff", {});
    check(!invalid && has(invalid.diagnostics, "invalid_utf8", "/text"), "Invalid UTF-8 accepted");
    tessera::TextStyle style;
    style.size = 0;
    style.weight = 0;
    style.line_height = -1.0f;
    const auto errors = shaper.measure("x", style);
    check(!errors && has(errors.diagnostics, "out_of_range", "/style/size") &&
          has(errors.diagnostics, "out_of_range", "/style/weight") &&
          has(errors.diagnostics, "out_of_range", "/style/line_height"), "Invalid text style accepted");
}

void wrapping() {
    tessera::PlaceholderTextShaper shaper;
    tessera::TextStyle style;
    style.size = 20;
    const auto run = shaper.shape("Aメニュー\n\nB\n", style, {20.0f});
    check(run && run.value->metrics == tessera::TextMetrics{{20, 150}, 18.5f, 25, 6},
          "Wrapping and explicit empty lines differ");
    check(run.value->glyphs[2].cluster == 4 && run.value->glyphs[2].position == tessera::Point{0, 43.5f},
          "Wrapped scalar lost its source offset or baseline");
    check(shaper.measure("Aメニュー\n\nB\n", style, {20.0f}).value == run.value->metrics,
          "Constrained measure/shape disagree");
    const auto zero = shaper.shape("AB", style, {0.0f});
    check(zero && zero.value->metrics.lines == 2 && zero.value->metrics.size.width == 10,
          "Zero width must allow one overflowing scalar per line");
    check(shaper.measure("", style, {0.0f}).value->lines == 1, "Empty constrained text must keep one line");
    check(has(shaper.shape("x", style, {-1.0f}).diagnostics, "out_of_range", "/constraints/max_width"),
          "Negative width accepted");
    check(has(shaper.shape("x", style, {std::numeric_limits<float>::infinity()}).diagnostics,
              "invalid_number", "/constraints/max_width"), "Infinite width accepted");
    check(has(shaper.measure("x", style, {std::numeric_limits<float>::quiet_NaN()}).diagnostics,
              "invalid_number", "/constraints/max_width"), "NaN width accepted");
    style.size = 13.1f;
    const auto natural = shaper.shape("ABCDE", style);
    check(natural && shaper.shape("ABCDE", style, {natural.value->metrics.size.width}).value == natural.value,
          "Exact natural float width must not introduce a placeholder wrap");
}

} // namespace

int main() {
    try {
        placeholder_metrics();
        failures();
        wrapping();
        std::cout << "Placeholder text measurement/shaping agreement and failure checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
