#include <tessera/input/pointer.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <array>
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
        styles[0].display = Display::stack;
    }
    LayoutResult layout(Size viewport) {
        const LayoutInput input{tree.get(), styles, viewport, &text, offsets};
        auto result = take(compute_layout(input));
        check(result == take(compute_layout(input)), "Stack layout must be deterministic");
        return result;
    }
};

void placement_and_constraints() {
    Fixture f(box({box(), box(), box()}));
    auto& root = f.styles[0];
    root.border = {2, 2, 2, 2};
    root.padding = {8, 8, 8, 8};
    root.direction = FlexDirection::row;
    root.justify = Justify::space_between;
    root.align = Align::end;
    root.gap = 100;
    f.styles[1].grow = f.styles[1].shrink = 1;
    auto& edge = f.styles[2];
    edge.width = Dimension::points(40);
    edge.height = Dimension::points(20);
    edge.margin = {4, 6, 8, 10};
    edge.stack_horizontal = Align::end;
    edge.stack_vertical = Align::start;
    auto& center = f.styles[3];
    center.width = Dimension::points(20);
    center.height = Dimension::points(10);
    center.margin = {2, 4, 6, 8};
    center.stack_horizontal = center.stack_vertical = Align::center;
    auto result = f.layout({240, 140});
    check(result.boxes[1].border_box == Rect{{10, 10}, {220, 120}},
          "Automatic children must fill the same content box, ignoring Flex controls");
    check(result.boxes[2].border_box == Rect{{184, 14}, {40, 20}},
          "Edge placement must use the parent content box and child margins");
    check(result.boxes[3].border_box == Rect{{112, 63}, {20, 10}},
          "Each Stack child must align independently on both axes");

    center.width = center.height = {};
    center.max_width = 60;
    center.min_height = 30;
    center.max_height = 50;
    center.stack_horizontal = center.stack_vertical = Align::stretch;
    result = f.layout({240, 140});
    check(result.boxes[3].border_box == Rect{{18, 12}, {60, 50}},
          "Stretch must respect margins and min/max limits, with leading placement");
    center.width = Dimension::points(300);
    center.height = Dimension::points(200);
    center.max_width = center.max_height = std::numeric_limits<float>::infinity();
    center.stack_horizontal = Align::end;
    center.stack_vertical = Align::center;
    result = f.layout({240, 140});
    check(result.boxes[3].border_box == Rect{{18, 12}, {300, 200}},
          "Overflowing centered/end children must stay at the leading margin edge");
    center.width = Dimension::points(1);
    center.height = Dimension::points(1);
    center.min_height = 0;
    center.border = {2, 2, 2, 2};
    center.padding = {3, 3, 3, 3};
    result = f.layout({240, 140});
    check(result.boxes[3].border_box.size == Size{10, 10}, "Border and padding floor must still win");
}

void intrinsic_and_wrapped_height() {
    UiNode label;
    label.kind = NodeKind::text;
    label.properties["text"] = std::string("ABCDE");
    Fixture f(box({box({std::move(label), box()}), box()}));
    f.styles[0].display = Display::flex;
    f.styles[1].display = Display::stack;
    f.styles[1].max_width = 20;
    f.styles[1].gap = 100;
    f.styles[2].text.size = 20;
    f.styles[2].stack_vertical = Align::start;
    f.styles[3].width = Dimension::points(12);
    f.styles[3].height = Dimension::points(40);
    f.styles[3].stack_vertical = Align::end;
    f.styles[4].height = Dimension::points(8);
    auto result = f.layout({100, 200});
    check(result.boxes[1].border_box == Rect{{0, 0}, {20, 75}} &&
              result.boxes[2].border_box == Rect{{0, 0}, {20, 75}} &&
              result.boxes[3].border_box == Rect{{0, 35}, {12, 40}} && result.boxes[4].border_box.origin.y == 75,
          "Nested Stack height must use the largest width-remeasured child, not a sum or gap");
    f.styles[0].align = Align::start;
    f.styles[1].max_width = std::numeric_limits<float>::infinity();
    result = f.layout({100, 200});
    check(result.boxes[1].border_box.size == Size{50, 40},
          "Unstretched automatic Stack width/height must use maximum intrinsic extents");
}

void scroll_and_nested_clip() {
    Fixture f(box({box({box()}), box()}));
    f.styles[0].overflow = Overflow::scroll;
    f.styles[0].padding = {4, 4, 4, 4};
    f.styles[1].display = Display::stack;
    f.styles[1].overflow = Overflow::clip;
    f.styles[1].width = Dimension::points(80);
    f.styles[1].height = Dimension::points(100);
    f.styles[1].margin = {0, 4, 6, 0};
    f.styles[2].width = Dimension::points(120);
    f.styles[2].height = Dimension::points(120);
    f.styles[3].width = f.styles[3].height = Dimension::points(10);
    f.styles[3].stack_horizontal = f.styles[3].stack_vertical = Align::end;
    f.offsets.resize(f.styles.size(), {1000, 1000});
    const auto result = f.layout({80, 60});
    check(result.boxes[0].scroll == ScrollGeometry{{92, 114}, {12, 54}},
          "Stack scroll extent must cover overlapping margin boxes, not summed children");
    check(result.boxes[1].border_box == Rect{{-8, -50}, {80, 100}} &&
              result.boxes[2].border_box.origin == Point{-8, -50},
          "Nested Stack descendants must receive the ordinary ancestor scroll offset");
    check(result.boxes[1].clip == Rect{{0, 0}, {80, 60}} && result.boxes[2].clip == Rect{{0, 0}, {72, 50}},
          "Stack must retain intersected ancestor padding-box clips");
}

std::array<std::uint8_t, 4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto at = (std::size_t(y) * image.extent.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}
void paint_and_hit_order() {
    Fixture f(box({box({box()}), box()}));
    f.styles[1].background = {1, 0, 0, 1};
    f.styles[2].width = f.styles[2].height = Dimension::points(8);
    f.styles[2].background = {0, 1, 0, 1};
    auto& top = f.styles[3];
    top.width = top.height = Dimension::points(6);
    top.stack_horizontal = top.stack_vertical = Align::center;
    top.background = {0, 0, 1, 1};
    CpuFrameCapture frames;
    const auto observe = [&](std::uint32_t expected, std::array<std::uint8_t, 4> ink) {
        const auto layout = f.layout({12, 12});
        const auto paint = take(build_paint_list({f.tree.get(), f.styles, &layout, &f.text}));
        const auto image = take(frames.capture({0, {12, 12}, 1, {12, 12}, &paint}));
        check(pixel(image, 4, 4) == ink, "Later sibling subtrees must paint above earlier descendants");
        const auto hit = take(hit_test({f.tree.get(), f.styles, &layout}, {4, 4}));
        check(hit.target && hit.target->index == expected, "Topmost visible hit must agree with paint order");
        check(pixel(image, 1, 1) == std::array<std::uint8_t, 4>{0, 255, 0, 255},
              "Earlier descendants must remain visible outside the overlay");
        return layout;
    };
    observe(3, {0, 0, 255, 255});
    top.opacity = 0;
    observe(3, {0, 255, 0, 255});
    top.opacity = 1;
    top.visibility = Visibility::hidden;
    check(observe(2, {0, 255, 0, 255}).boxes.size() == 4, "Hidden overlays must retain their geometry");
    top.display = Display::none;
    check(observe(2, {0, 255, 0, 255}).boxes.size() == 3, "Display none overlays must leave no layout box");

    top.display = Display::flex;
    top.visibility = Visibility::visible;
    top.width = Dimension::points(20);
    top.height = Dimension::points(12);
    f.styles[0].width = f.styles[0].height = Dimension::points(12);
    for (const bool clipped : {false, true}) {
        f.styles[0].overflow = clipped ? Overflow::clip : Overflow::visible;
        const auto layout = f.layout({20, 20});
        const auto paint = take(build_paint_list({f.tree.get(), f.styles, &layout, &f.text}));
        const auto image = take(frames.capture({0, {20, 20}, 1, {20, 20}, &paint}));
        const auto hit = take(hit_test({f.tree.get(), f.styles, &layout}, {14, 5}));
        check(bool(hit.target) == !clipped && pixel(image, 14, 5)[3] == (clipped ? 0 : 255),
              "Overflowing Stack paint and hits must share the ordinary clip");
    }
}

void invalid_values() {
    Fixture f(box({box()}));
    f.styles[1].stack_horizontal = static_cast<Align>(9);
    f.styles[1].stack_vertical = static_cast<Align>(9);
    auto result = compute_layout({f.tree.get(), f.styles, {100, 100}, &f.text});
    check(!result && has(result.diagnostics, "unknown_value", "/styles/1/stack_horizontal") &&
              has(result.diagnostics, "unknown_value", "/styles/1/stack_vertical"),
          "Invalid Stack placement must report located errors");
    f.styles[1] = {};
    f.styles[0].padding.left = f.styles[0].padding.right = std::numeric_limits<float>::max();
    result = compute_layout({f.tree.get(), f.styles, {100, 100}, &f.text});
    check(!result && has(result.diagnostics, "non_finite_geometry", "/nodes/0"),
          "Stack geometry overflow must reject the entire candidate");
}
} // namespace

int main() {
    try {
        placement_and_constraints();
        intrinsic_and_wrapped_height();
        scroll_and_nested_clip();
        paint_and_hit_order();
        invalid_values();
        std::cout << "Stack placement, wrapping, scroll/clip, CPU pixels and hit order passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
