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
    }
    Result<LayoutResult> compute(Size viewport) {
        return compute_layout({tree.get(), styles, viewport, &text, offsets});
    }
    LayoutResult layout(Size viewport) {
        auto result = take(compute(viewport));
        check(result == take(compute(viewport)), "Absolute layout must be deterministic");
        return result;
    }
};
void absolute(ResolvedStyle& style, std::optional<float> left, std::optional<float> top, std::optional<float> right,
              std::optional<float> bottom) {
    style.position = Position::absolute;
    style.left = left;
    style.top = top;
    style.right = right;
    style.bottom = bottom;
}

void offsets_and_flow() {
    Fixture f(box({box(), box(), box()}));
    auto& parent = f.styles[0];
    parent.width = Dimension::points(200);
    parent.height = Dimension::points(100);
    parent.border = {2, 2, 2, 2};
    parent.padding = {10, 10, 10, 10};
    parent.gap = 4;
    f.styles[1].height = f.styles[3].height = Dimension::points(20);
    auto& chip = f.styles[2];
    chip.width = Dimension::points(30);
    chip.height = Dimension::points(12);
    absolute(chip, 5.f, -6.f, {}, {});
    auto result = f.layout({300, 300});
    check(result.boxes.size() == 4 && result.boxes[2].node.index == 2 && result.boxes[2].parent == 0 &&
              result.boxes[1].border_box == Rect{{12, 12}, {176, 20}} &&
              result.boxes[3].border_box == Rect{{12, 36}, {176, 20}} &&
              result.boxes[2].border_box == Rect{{7, -4}, {30, 12}},
          "Absolute children must leave flow and gaps, keep preorder, and use padding-box offsets past the edge");
    absolute(chip, {}, {}, 8.f, 4.f);
    result = f.layout({300, 300});
    check(result.boxes[2].border_box == Rect{{160, 82}, {30, 12}}, "Trailing offsets must place from the far edges");
    chip.width = {};
    chip.margin = {0, 1, 0, 3};
    absolute(chip, 5.f, {}, 8.f, {});
    result = f.layout({300, 300});
    check(result.boxes[2].border_box == Rect{{10, 2}, {179, 12}},
          "Both offsets on an automatic axis must fill the space between them less margins");
    chip.width = Dimension::points(30);
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.origin.x == 10 && result.boxes[2].border_box.size.width == 30,
          "A fixed size with both offsets must keep the leading offset");
    absolute(chip, {}, {}, {}, {});
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.origin == Point{5, 2}, "No offsets must start at the leading padding edge");
    chip.width = {};
    chip.max_width = 20;
    absolute(chip, 5.f, 0.f, 8.f, {});
    result = f.layout({300, 300});
    check(result.boxes[2].border_box.size.width == 20, "Filled absolute sizes must respect limits");
}

void natural_size_and_intrinsic_exclusion() {
    Fixture f(box({label("ABCDEFGHIJKL")}));
    f.styles[0].width = Dimension::points(200);
    f.styles[0].height = Dimension::points(100);
    f.styles[0].border = {2, 2, 2, 2};
    f.styles[1].text.size = 20;
    absolute(f.styles[1], 150.f, {}, {}, {});
    auto result = f.layout({300, 300});
    check(result.boxes[1].border_box == Rect{{152, 2}, {46, 75}},
          "Automatic width must be the natural width bounded by the remaining room, with height measured there");
    absolute(f.styles[1], 0.f, {}, {}, {});
    result = f.layout({300, 300});
    check(result.boxes[1].border_box == Rect{{2, 2}, {120, 25}}, "Fitting text must keep its natural size");

    Fixture outer(box({box({box(), box()}), box({box(), box()})}));
    outer.styles[0].align = Align::start;
    outer.styles[2].height = Dimension::points(20);
    outer.styles[3].width = outer.styles[3].height = Dimension::points(500);
    absolute(outer.styles[3], 0.f, 0.f, {}, {});
    auto& grid = outer.styles[4];
    grid.display = Display::grid;
    grid.grid_columns = {GridTrack{}};
    grid.grid_rows = {GridTrack{}};
    absolute(outer.styles[5], {}, {}, 0.f, 0.f);
    outer.styles[5].width = outer.styles[5].height = Dimension::points(4);
    outer.styles[6].width = outer.styles[6].height = Dimension::points(10);
    result = outer.layout({300, 300});
    check(result.boxes[1].border_box.size == Size{0, 20} && result.boxes[3].border_box.size == Size{500, 500},
          "Absolute children must not contribute to automatic parent sizes");
    check(result.boxes[4].border_box == Rect{{0, 20}, {10, 10}} &&
              result.boxes[6].border_box == Rect{{0, 20}, {10, 10}} &&
              result.boxes[5].border_box == Rect{{6, 26}, {4, 4}},
          "Absolute Grid children must take no cell or capacity");
}

std::array<std::uint8_t, 4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto at = (std::size_t(y) * image.extent.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}

void scroll_clip_paint_and_hits() {
    Fixture scroll(box({box(), box()}));
    scroll.styles[0].width = Dimension::points(100);
    scroll.styles[0].height = Dimension::points(50);
    scroll.styles[0].overflow = Overflow::scroll;
    scroll.styles[1].height = Dimension::points(10);
    scroll.styles[2].width = Dimension::points(10);
    scroll.styles[2].height = Dimension::points(30);
    absolute(scroll.styles[2], 0.f, 80.f, {}, {});
    scroll.offsets.resize(scroll.styles.size(), {1000, 1000});
    auto result = scroll.layout({100, 50});
    check(result.boxes[0].scroll == ScrollGeometry{{100, 110}, {0, 60}} &&
              result.boxes[2].border_box == Rect{{0, 20}, {10, 30}},
          "Absolute children must extend a scroll parent's extent and move with its offset");

    Fixture f(box({box(), box({box()})}));
    f.styles[0].width = f.styles[0].height = Dimension::points(40);
    f.styles[1].height = Dimension::points(40);
    f.styles[1].background = {1, 0, 0, 1};
    auto& card = f.styles[2];
    card.width = card.height = Dimension::points(20);
    card.overflow = Overflow::clip;
    absolute(card, 10.f, 10.f, {}, {});
    auto& chip = f.styles[3];
    chip.width = chip.height = Dimension::points(20);
    chip.background = {0, 1, 0, 1};
    absolute(chip, 5.f, 5.f, {}, {});
    result = f.layout({40, 40});
    check(result.boxes[3].border_box == Rect{{15, 15}, {20, 20}} && result.boxes[3].clip == Rect{{10, 10}, {20, 20}},
          "Nested absolute boxes must use their own parent and inherit ordinary ancestor clips");
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &result, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {40, 40}, 1, {40, 40}, &paint}));
    const auto hit = take(hit_test({f.tree.get(), f.styles, &result}, {20, 20}));
    check(pixel(image, 20, 20) == std::array<std::uint8_t, 4>{0, 255, 0, 255} && hit.target && hit.target->index == 3,
          "A later absolute sibling must paint and hit above earlier flow content");
    const auto clipped = take(hit_test({f.tree.get(), f.styles, &result}, {32, 32}));
    check(pixel(image, 32, 32) == std::array<std::uint8_t, 4>{255, 0, 0, 255} && clipped.target &&
              clipped.target->index == 1,
          "Clipped absolute overflow must neither paint nor hit");
}

void display_and_rejection() {
    Fixture f(box({box(), box()}));
    f.styles[1].width = Dimension::points(10);
    f.styles[1].height = Dimension::points(10);
    absolute(f.styles[1], 4.f, 4.f, {}, {});
    f.styles[1].visibility = Visibility::hidden;
    auto result = f.layout({100, 100});
    check(!result.boxes[1].visible && result.boxes[1].border_box == Rect{{4, 4}, {10, 10}} &&
              result.boxes[2].border_box.origin == Point{0, 0},
          "Hidden absolute children must keep geometry without affecting flow");
    f.styles[1].display = Display::none;
    check(f.layout({100, 100}).boxes.size() == 2, "Non-displayed absolute children must produce no boxes");
    f.styles[1].display = Display::flex;
    f.styles[1].left = std::nanf("");
    f.styles[1].bottom = std::numeric_limits<float>::infinity();
    f.styles[2].position = static_cast<Position>(7);
    auto rejected = f.compute({100, 100});
    check(!rejected && has(rejected.diagnostics, "invalid_number", "/styles/1/left") &&
              has(rejected.diagnostics, "invalid_number", "/styles/1/bottom") &&
              has(rejected.diagnostics, "unknown_value", "/styles/2/position"),
          "Non-finite offsets and unknown positions need located diagnostics");
    f.styles[1].left = std::numeric_limits<float>::max();
    f.styles[1].bottom = {};
    f.styles[2].position = Position::flow;
    f.styles[1].margin.left = std::numeric_limits<float>::max();
    check(has(f.compute({100, 100}).diagnostics, "non_finite_geometry", "/nodes/1"),
          "Absolute arithmetic overflow must reject the entire candidate");
}
} // namespace

int main() {
    try {
        offsets_and_flow();
        natural_size_and_intrinsic_exclusion();
        scroll_clip_paint_and_hits();
        display_and_rejection();
        std::cout
            << "Absolute offsets, flow exclusion, natural sizing, scroll/clip, CPU pixels and rejection passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
