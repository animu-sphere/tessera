#include <tessera/input/pointer.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <array>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message;
        for (const auto& error : result.diagnostics)
            message += error.code + " at " + error.path + ": " + error.message + "\n";
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
UiNode box(std::vector<UiNode> children = {}) {
    UiNode result;
    result.children = std::move(children);
    return result;
}
struct Fixture {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    std::vector<Point> offsets;
    explicit Fixture(UiNode root) {
        UiDocument document;
        document.root = std::move(root);
        tree = take(UiTree::create(document));
        styles.resize(tree->size());
    }
    Result<LayoutResult> compute(Size viewport) {
        return compute_layout({tree.get(), styles, viewport, &text, offsets});
    }
    LayoutResult layout(Size viewport) {
        auto result = take(compute(viewport));
        check(result == take(compute(viewport)), "Wrapped layout must be deterministic");
        return result;
    }
};
Rect rect(float x, float y, float width, float height) { return {{x, y}, {width, height}}; }

// Root column (0) holding a wrapping row (1) of four boxes (2-5) and a following sibling (6).
// The boxes are 30x10, 30x20, 30x10 and 50x5; three fit a 100-unit line with 4-unit gaps.
Fixture chips() {
    Fixture f(box({box({box(), box(), box(), box()}), box()}));
    auto& row = f.styles[1];
    row.width = Dimension::points(100);
    row.direction = FlexDirection::row;
    row.flex_wrap = FlexWrap::wrap;
    row.gap = 4;
    const std::array<Size, 4> sizes{Size{30, 10}, Size{30, 20}, Size{30, 10}, Size{50, 5}};
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        f.styles[2 + i].width = Dimension::points(sizes[i].width);
        f.styles[2 + i].height = Dimension::points(sizes[i].height);
    }
    f.styles[6].height = Dimension::points(10);
    return f;
}

void lines_and_placement() {
    auto f = chips();
    auto result = f.layout({300, 300});
    check(result.boxes[1].border_box == rect(0, 0, 100, 29) && result.boxes[2].border_box == rect(0, 0, 30, 10) &&
              result.boxes[3].border_box == rect(34, 0, 30, 20) && result.boxes[4].border_box == rect(68, 0, 30, 10) &&
              result.boxes[5].border_box == rect(0, 24, 50, 5),
          "Children must break into lines when the next margin box passes the content width");
    check(result.boxes[6].border_box == rect(0, 29, 300, 10),
          "An automatic wrapping row must be as tall as its lines and gaps, moving later siblings");
    f.styles[1].justify = Justify::center;
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.origin.x == 1 && result.boxes[4].border_box.origin.x == 69 &&
              result.boxes[5].border_box.origin.x == 25,
          "Justify must place each line's own remaining space");
    f.styles[1].justify = Justify::start;
    f.styles[1].align = Align::center;
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.origin.y == 5 && result.boxes[3].border_box.origin.y == 0 &&
              result.boxes[5].border_box.origin.y == 24,
          "Cross alignment must use each line's height, not the container's");
    f.styles[1].align = Align::stretch;
    f.styles[2].height = {};
    result = f.layout({300, 300});
    check(result.boxes[2].border_box == rect(0, 0, 30, 20) && result.boxes[5].border_box == rect(0, 24, 50, 5),
          "Stretch must fill the line's cross size without growing other lines");
    f.styles[2].grow = f.styles[5].grow = 1;
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.size.width == 32 && result.boxes[3].border_box.origin.x == 36 &&
              result.boxes[5].border_box == rect(0, 24, 100, 5),
          "Grow must distribute only the free space of the child's own line");
}

void cross_space_overflow_and_margins() {
    auto f = chips();
    f.styles[1].height = Dimension::points(49);
    f.styles[2].height = f.styles[5].height = {};
    f.styles[5].min_height = 5;
    auto result = f.layout({300, 300});
    check(result.boxes[2].border_box == rect(0, 0, 30, 30) && result.boxes[3].border_box == rect(34, 0, 30, 20) &&
              result.boxes[5].border_box == rect(0, 34, 50, 15),
          "Positive cross space must be shared equally among lines before stretching");

    Fixture wide(box({box({box(), box(), box()})}));
    auto& row = wide.styles[1];
    row.width = Dimension::points(100);
    row.direction = FlexDirection::row;
    row.flex_wrap = FlexWrap::wrap;
    row.justify = Justify::end;
    row.gap = 4;
    for (std::size_t i = 2; i < 5; ++i) wide.styles[i].height = Dimension::points(10);
    wide.styles[2].width = wide.styles[4].width = Dimension::points(30);
    wide.styles[3].width = Dimension::points(150);
    result = wide.layout({300, 300});
    check(result.boxes[1].border_box.size.height == 38 && result.boxes[2].border_box == rect(70, 0, 30, 10) &&
              result.boxes[3].border_box == rect(0, 14, 150, 10) && result.boxes[4].border_box == rect(70, 28, 30, 10),
          "An oversized child must take its own line and overflow safely from the leading edge");
    wide.styles[3].width = Dimension::points(30);
    wide.styles[2].margin.left = 10;
    row.justify = Justify::start;
    result = wide.layout({300, 300});
    check(result.boxes[2].border_box.origin.x == 10 && result.boxes[3].border_box.origin.x == 44 &&
              result.boxes[4].border_box == rect(0, 14, 30, 10),
          "Margins must count toward line breaking");
}

void intrinsic_scroll_and_ignored() {
    auto f = chips();
    f.styles[0].align = Align::start;
    f.styles[1].width = {};
    auto result = f.layout({300, 300});
    check(result.boxes[1].border_box == rect(0, 0, 152, 20) && result.boxes[5].border_box == rect(102, 0, 50, 5),
          "A non-stretched automatic width must keep the single-line natural width");

    f = chips();
    f.styles[1].height = Dimension::points(20);
    f.styles[1].overflow = Overflow::scroll;
    f.offsets.resize(f.styles.size(), {1000, 1000});
    result = f.layout({300, 300});
    check(result.boxes[1].scroll == ScrollGeometry{{100, 29}, {0, 9}} &&
              result.boxes[5].border_box == rect(0, 15, 50, 5),
          "Later lines must extend a scroll box's extent and move with its offset");

    f = chips();
    f.styles[0].display = Display::grid;
    f.styles[0].grid_columns = {GridTrack::points(100)};
    f.styles[0].grid_rows = {GridTrack{}, GridTrack{}};
    result = f.layout({300, 300});
    check(result.boxes[1].border_box == rect(0, 0, 100, 29) && result.boxes[6].border_box.origin.y == 29,
          "Grid rows must use the height wrapped at the assigned column width");

    f = chips();
    f.styles[3].display = Display::none;
    result = f.layout({300, 300});
    check(result.boxes.size() == 6 && result.boxes[1].border_box.size.height == 19 &&
              result.boxes[4].border_box == rect(0, 14, 50, 5),
          "Non-displayed children must take no line space or gap");
    f.styles[3].display = Display::flex;
    f.styles[3].visibility = Visibility::hidden;
    result = f.layout({300, 300});
    check(!result.boxes[3].visible && result.boxes[3].border_box == rect(34, 0, 30, 20) &&
              result.boxes[5].border_box.origin.y == 24,
          "Hidden children must keep their line geometry");

    f = chips();
    f.styles[1].direction = FlexDirection::column;
    auto wrapped = f.layout({300, 300});
    f.styles[1].flex_wrap = FlexWrap::no_wrap;
    check(wrapped == f.layout({300, 300}) && wrapped.boxes[1].border_box.size.height == 57,
          "Column containers must ignore flex_wrap");
    f.styles[1].direction = FlexDirection::row;
    f.styles[1].display = Display::stack;
    f.styles[1].flex_wrap = FlexWrap::wrap;
    wrapped = f.layout({300, 300});
    f.styles[1].flex_wrap = FlexWrap::no_wrap;
    check(wrapped == f.layout({300, 300}), "Stack containers must ignore flex_wrap");
}

std::array<std::uint8_t, 4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto at = (std::size_t(y) * image.extent.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}

void paint_hits_and_rejection() {
    auto f = chips();
    f.styles[1].background = {1, 0, 0, 1};
    f.styles[5].background = {0, 1, 0, 1};
    const auto result = f.layout({100, 40});
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &result, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {100, 40}, 1, {100, 40}, &paint}));
    const auto hit = take(hit_test({f.tree.get(), f.styles, &result}, {10, 26}));
    const auto beside = take(hit_test({f.tree.get(), f.styles, &result}, {60, 26}));
    check(pixel(image, 10, 26) == std::array<std::uint8_t, 4>{0, 255, 0, 255} && hit.target && hit.target->index == 5 &&
              pixel(image, 60, 26) == std::array<std::uint8_t, 4>{255, 0, 0, 255} && beside.target &&
              beside.target->index == 1,
          "Wrapped children must paint and hit at their second-line geometry");

    f.styles[1].flex_wrap = static_cast<FlexWrap>(9);
    const auto rejected = f.compute({100, 40});
    check(!rejected && has(rejected.diagnostics, "unknown_value", "/styles/1/flex_wrap"),
          "Unknown wrap values need a located diagnostic");
}
} // namespace

int main() {
    try {
        lines_and_placement();
        cross_space_overflow_and_margins();
        intrinsic_scroll_and_ignored();
        paint_hits_and_rejection();
        std::cout << "Flex wrapping lines, per-line flexing/alignment, cross space, intrinsic/Grid heights, "
                     "scrolling, CPU pixels/hits and rejection passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
