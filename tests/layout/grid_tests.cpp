#include <tessera/input/pointer.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

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
UiNode label(std::string text) {
    UiNode result;
    result.kind = NodeKind::text;
    result.properties["text"] = std::move(text);
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
        styles[0].display = Display::grid;
    }
    Result<LayoutResult> compute(Size viewport) {
        return compute_layout({tree.get(), styles, viewport, &text, offsets});
    }
    LayoutResult layout(Size viewport) {
        auto result = take(compute(viewport));
        check(result == take(compute(viewport)), "Grid layout must be deterministic");
        return result;
    }
};

void allocation_and_alignment() {
    Fixture f(box({box(), box(), box(), box(), box(), box()}));
    auto& grid = f.styles[0];
    grid.grid_columns = {GridTrack::points(40), GridTrack::fraction(1, 10, 30), GridTrack::fraction(2, 20)};
    grid.grid_rows = {GridTrack::points(30), GridTrack::fraction(1, 10)};
    grid.column_gap = 8;
    grid.row_gap = 6;
    grid.border = {2, 2, 2, 2};
    grid.padding = {10, 10, 10, 10};
    grid.direction = FlexDirection::row;
    grid.align = Align::end;
    grid.justify = Justify::space_between;
    grid.gap = 100;
    f.styles[1].grow = f.styles[1].shrink = 1;
    auto& aligned = f.styles[2];
    aligned.width = Dimension::points(10);
    aligned.height = Dimension::points(8);
    aligned.margin = {1, 2, 3, 4};
    aligned.grid_horizontal = Align::end;
    aligned.grid_vertical = Align::center;
    auto result = f.layout({200, 140});
    check(result.boxes[1].border_box == Rect{{12, 12}, {40, 30}} &&
              result.boxes[2].border_box == Rect{{78, 22}, {10, 8}} &&
              result.boxes[3].border_box == Rect{{98, 12}, {90, 30}} &&
              result.boxes[4].border_box == Rect{{12, 48}, {40, 80}},
          "Grid must share fixed/fractional tracks, redistribute caps, and use content-box cells");
    aligned.width = aligned.height = {};
    aligned.max_width = 16;
    aligned.min_height = 12;
    aligned.max_height = 18;
    aligned.grid_horizontal = aligned.grid_vertical = Align::stretch;
    result = f.layout({200, 140});
    check(result.boxes[2].border_box == Rect{{64, 13}, {16, 18}},
          "Stretched Grid children must respect margins and min/max limits");
    aligned.width = aligned.height = Dimension::points(100);
    aligned.max_width = aligned.max_height = std::numeric_limits<float>::infinity();
    aligned.grid_horizontal = aligned.grid_vertical = Align::end;
    result = f.layout({60, 50});
    check(result.boxes[2].border_box == Rect{{64, 13}, {100, 100}},
          "Track minima and child overflow must stay at the leading margin edge");
    aligned.width = aligned.height = Dimension::points(1);
    aligned.min_height = 0;
    aligned.border = {2, 2, 2, 2};
    aligned.padding = {3, 3, 3, 3};
    result = f.layout({200, 140});
    check(result.boxes[2].border_box.size == Size{10, 10}, "Border/padding floor must win in Grid cells");
}

void fractional_weights_and_content_tracks() {
    Fixture f(box({box(), box(), box()}));
    f.styles[0].grid_columns = {GridTrack::fraction(1), GridTrack::fraction(2), GridTrack::fraction(3)};
    f.styles[0].grid_rows = {GridTrack{}};
    auto result = f.layout({120, 40});
    check(result.boxes[1].border_box.size.width == 20 && result.boxes[2].border_box.size.width == 40 &&
              result.boxes[3].border_box.size.width == 60,
          "Fraction weights must distribute available space proportionally");
    f.styles[0].grid_columns = {GridTrack{}, GridTrack::points(30), GridTrack::fraction(1, 10, 20)};
    f.styles[1].width = Dimension::points(25);
    f.styles[1].height = Dimension::points(12);
    result = f.layout({120, 40});
    check(result.boxes[2].border_box == Rect{{25, 0}, {30, 12}} &&
              result.boxes[3].border_box == Rect{{55, 0}, {20, 12}},
          "Content tracks must use the largest contribution and capped tracks leave trailing space");
    f.styles[0].grid_columns[0].minimum = 35;
    f.styles[0].grid_columns[1].maximum = 15;
    result = f.layout({120, 40});
    check(result.boxes[2].border_box.origin.x == 35 && result.boxes[2].border_box.size.width == 15,
          "Content and fixed tracks must honor their min/max limits");
}

void nested_wrapped_rows() {
    Fixture f(box({box({label("ABCDEFGH"), label("ABCDEFGHIJKL"), box(), box()}), box()}));
    f.styles[0].display = Display::flex;
    auto& grid = f.styles[1];
    grid.display = Display::grid;
    grid.grid_columns = {GridTrack::points(40), GridTrack::fraction()};
    grid.grid_rows = {GridTrack{}, GridTrack{}};
    grid.column_gap = 4;
    grid.row_gap = 6;
    grid.max_width = 100;
    f.styles[2].text.size = f.styles[3].text.size = 20;
    f.styles[4].height = Dimension::points(10);
    f.styles[5].height = Dimension::points(20);
    f.styles[6].height = Dimension::points(8);
    auto result = f.layout({200, 200});
    check(result.boxes[1].border_box.size == Size{100, 101} && result.boxes[2].border_box == Rect{{0, 0}, {40, 75}} &&
              result.boxes[3].border_box == Rect{{44, 0}, {56, 75}} &&
              result.boxes[4].border_box.origin == Point{0, 81} && result.boxes[6].border_box.origin.y == 101,
          "Automatic rows must share the largest width-remeasured height and size the containing Flex");
    f.styles[0].align = Align::start;
    grid.max_width = std::numeric_limits<float>::infinity();
    result = f.layout({200, 200});
    check(result.boxes[1].border_box.size == Size{164, 76},
          "Unconstrained Grid must use natural track contributions before wrapping fixed columns");
    grid.grid_columns = {GridTrack::points(40), GridTrack::points(60)};
    grid.grid_rows = {GridTrack::points(16), GridTrack::points(20)};
    result = f.layout({200, 200});
    check(result.boxes[1].border_box.size == Size{104, 42},
          "Fixed rows must retain their size even when wrapped content overflows");
}

void shared_content_columns() {
    const auto columns = [](std::string first) {
        Fixture f(box({label(std::move(first)), box(), label("Kind"), box()}));
        f.styles[0].grid_columns = {GridTrack{.maximum = 80}, GridTrack::fraction()};
        f.styles[0].grid_rows = {GridTrack{}, GridTrack{}};
        f.styles[0].column_gap = 8;
        f.styles[1].text.size = f.styles[3].text.size = 20;
        return f.layout({200, 200});
    };
    auto result = columns("Display name");
    check(result.boxes[2].border_box.origin.x == 88 && result.boxes[4].border_box.origin.x == 88 &&
              result.boxes[1].border_box.size.height == 50 && result.boxes[3].border_box.size.height == 25,
          "Unequal labels must establish one capped content column across independently measured rows");
    result = columns("Name");
    check(result.boxes[2].border_box.origin.x == 48 && result.boxes[4].border_box.origin.x == 48 &&
              result.boxes[2].border_box.size.width == 152,
          "Shorter content must resize every shared column without application-side measurement or duplicated widths");
}

std::array<std::uint8_t, 4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto at = (std::size_t(y) * image.extent.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}
void scroll_clip_paint_and_hits() {
    Fixture f(box({box(), box({box()})}));
    f.styles[0].grid_columns = {GridTrack::points(20)};
    f.styles[0].grid_rows = {GridTrack::points(20), GridTrack::points(20)};
    f.styles[0].row_gap = 4;
    f.styles[0].overflow = Overflow::scroll;
    f.styles[1].background = {1, 0, 0, 1};
    f.styles[2].overflow = Overflow::clip;
    f.styles[3].width = f.styles[3].height = Dimension::points(30);
    f.styles[3].background = {0, 1, 0, 1};
    f.offsets.resize(f.styles.size(), {1000, 1000});
    const auto result = f.layout({16, 24});
    check(result.boxes[0].scroll == ScrollGeometry{{20, 44}, {4, 20}} &&
              result.boxes[1].border_box == Rect{{-4, -20}, {20, 20}} &&
              result.boxes[2].border_box == Rect{{-4, 4}, {20, 20}} && result.boxes[3].clip == Rect{{0, 4}, {16, 20}},
          "Grid scroll offsets and nested padding-box clips must use shared root coordinates");
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &result, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {24, 24}, 1, {24, 24}, &paint}));
    const auto hit = take(hit_test({f.tree.get(), f.styles, &result}, {1, 5}));
    check(pixel(image, 1, 5) == std::array<std::uint8_t, 4>{0, 255, 0, 255} && hit.target && hit.target->index == 3,
          "Grid CPU pixels and hits must agree inside scrolled cells");
    check(pixel(image, 1, 2)[3] == 0 && pixel(image, 17, 5)[3] == 0 &&
              !take(hit_test({f.tree.get(), f.styles, &result}, {17, 5})).target,
          "Grid gaps and clipped content must not paint or hit outside the viewport");
}

void display_and_rejection() {
    Fixture f(box({box(), box(), box()}));
    auto result = f.compute({100, 100});
    check(!result && has(result.diagnostics, "grid_tracks", "/styles/0/grid_columns") &&
              has(result.diagnostics, "grid_tracks", "/styles/0/grid_rows"),
          "Missing explicit tracks must reject");
    auto& grid = f.styles[0];
    grid.grid_columns = {GridTrack::points(20), GridTrack::points(30)};
    grid.grid_rows = {GridTrack::points(10)};
    result = f.compute({100, 100});
    check(!result && has(result.diagnostics, "grid_capacity", "/styles/0"),
          "Excess children must not create implicit tracks");
    f.styles[2].display = Display::none;
    f.styles[1].visibility = Visibility::hidden;
    auto layout = f.layout({100, 100});
    check(layout.boxes.size() == 3 && !layout.boxes[1].visible && layout.boxes[2].node.index == 3 &&
              layout.boxes[2].border_box == Rect{{20, 0}, {30, 10}},
          "Hidden cells retain space; non-displayed children compact row-major placement");
    grid.grid_columns[0] = GridTrack::fraction(0);
    grid.grid_rows[0].minimum = 20;
    grid.grid_rows[0].maximum = 10;
    grid.row_gap = -1;
    f.styles[3].grid_horizontal = static_cast<Align>(9);
    result = f.compute({100, 100});
    check(!result && has(result.diagnostics, "out_of_range", "/styles/0/grid_columns/0/value") &&
              has(result.diagnostics, "conflicting_constraints", "/styles/0/grid_rows/0/minimum") &&
              has(result.diagnostics, "out_of_range", "/styles/0/row_gap") &&
              has(result.diagnostics, "unknown_value", "/styles/3/grid_horizontal"),
          "Invalid tracks, limits, gaps and alignment need located diagnostics");
    grid.grid_columns.assign(max_grid_tracks + 1, GridTrack{});
    check(has(f.compute({100, 100}).diagnostics, "grid_track_bound", "/styles/0/grid_columns"),
          "Explicit track arrays must be bounded");
    grid.grid_columns = {GridTrack::fraction(std::numeric_limits<float>::infinity())};
    check(has(f.compute({100, 100}).diagnostics, "invalid_number", "/styles/0/grid_columns/0/value"),
          "Non-finite track weights must reject");
    grid = {};
    grid.display = Display::grid;
    const float huge = std::numeric_limits<float>::max();
    grid.grid_columns.assign(3, GridTrack::points(huge));
    grid.grid_rows = {GridTrack::points(10)};
    f.styles[2].display = Display::flex;
    f.styles[3].grid_horizontal = Align::stretch;
    check(has(f.compute({100, 100}).diagnostics, "non_finite_geometry", "/nodes/3"),
          "Grid arithmetic overflow must reject the entire candidate");
    grid.display = Display::none;
    check(f.layout({100, 100}).boxes.empty(), "A non-displayed Grid must produce no boxes");
}
} // namespace

int main() {
    try {
        allocation_and_alignment();
        fractional_weights_and_content_tracks();
        nested_wrapped_rows();
        shared_content_columns();
        scroll_clip_paint_and_hits();
        display_and_rejection();
        std::cout << "Grid allocation, wrapping, alignment, scroll/clip, CPU pixels and rejection passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
