#include <tessera/input/pointer.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <array>
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;

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
using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255}, yellow{255, 255, 0, 255};
constexpr Color red_fill{1, 0, 0, 1}, green_fill{0, 1, 0, 1}, blue_fill{0, 0, 1, 1}, yellow_fill{1, 1, 0, 1};

struct Fixture {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    Size viewport{40, 40};
    LayoutResult layout;
    CapturedImage image;
    explicit Fixture(UiNode root) {
        UiDocument document;
        document.root = std::move(root);
        tree = take(UiTree::create(document));
        styles.resize(tree->size());
        styles[0].display = Display::stack;
        styles[0].width = Dimension::points(viewport.width);
        styles[0].height = Dimension::points(viewport.height);
    }
    // Lays out, paints and captures the current styles.
    void run() {
        layout = take(compute_layout({tree.get(), styles, viewport, &text}));
        const auto paint = take(build_paint_list({tree.get(), styles, &layout, &text}));
        CpuFrameCapture frames;
        image = take(frames.capture({0, {40, 40}, 1, {40, 40}, &paint}));
    }
    Pixel pixel(unsigned x, unsigned y) const {
        const auto at = (std::size_t(y) * image.extent.width + x) * 4;
        return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
    }
    std::optional<std::uint32_t> hit(Point point) const {
        const auto result = take(hit_test({tree.get(), styles, &layout}, point));
        return result.target ? std::optional{result.target->index} : std::nullopt;
    }
    // Pixel and hit agree on the topmost node at a point.
    bool top(Point point, Pixel color, std::uint32_t node) const {
        return pixel(unsigned(point.x), unsigned(point.y)) == color && hit(point) == node;
    }
};

void sibling_order() {
    Fixture f(box({box(), box(), box()}));
    f.styles[1].background = red_fill;
    f.styles[2].background = green_fill;
    f.styles[3].background = blue_fill;
    f.run();
    const auto flat = f.layout;
    check(f.top({20, 20}, blue, 3), "Equal z_index keeps later siblings on top");

    f.styles[1].z_index = 1;
    f.run();
    check(f.layout == flat, "z_index must not change layout geometry or box order");
    check(f.top({20, 20}, red, 1), "A raised earlier sibling must paint and hit above later ones");

    f.styles[1].z_index = 0;
    f.styles[3].z_index = -1;
    f.run();
    check(f.top({20, 20}, green, 2), "A lowered later sibling must paint and hit below earlier ones");

    f.styles[1].z_index = f.styles[2].z_index = std::numeric_limits<std::int32_t>::max();
    f.styles[3].z_index = std::numeric_limits<std::int32_t>::min();
    f.run();
    check(f.top({20, 20}, green, 2), "Equal raised siblings keep tree order across the full integer range");
}

void subtree_scope_clip_and_visibility() {
    // Root(stack) -> A(red, z 1){A1 green 10x10 z 1, A2 yellow 10x10}, B(blue){B1 yellow z 100}
    Fixture f(box({box({box(), box()}), box({box()})}));
    auto& a = f.styles[1];
    a.background = red_fill;
    a.z_index = 1;
    a.display = Display::stack;
    for (const auto index : {2u, 3u}) {
        f.styles[index].width = f.styles[index].height = Dimension::points(10);
        f.styles[index].stack_horizontal = f.styles[index].stack_vertical = Align::start;
    }
    f.styles[2].background = green_fill;
    f.styles[2].z_index = 1;
    f.styles[3].background = yellow_fill;
    f.styles[4].background = blue_fill;
    f.styles[4].display = Display::stack;
    f.styles[5].background = yellow_fill;
    f.styles[5].z_index = 100;
    f.run();
    check(f.top({20, 20}, red, 1), "A raised sibling's subtree must stay above every box of a lower sibling");
    check(f.top({5, 5}, green, 2), "z_index must order children within their own parent");

    // A clipped, hidden raised child still respects clips and local visibility.
    a.overflow = Overflow::clip;
    a.width = a.height = Dimension::points(20);
    a.stack_horizontal = a.stack_vertical = Align::start;
    f.styles[2].position = Position::absolute;
    f.styles[2].left = 15.f;
    f.styles[2].top = 15.f;
    f.run();
    check(f.top({17, 17}, green, 2) && f.top({25, 25}, yellow, 5),
          "A raised child's overflow must stay clipped by its ancestors");
    f.styles[2].visibility = Visibility::hidden;
    f.run();
    check(f.top({17, 17}, red, 1), "A hidden raised child must neither paint nor hit");
    f.styles[1].display = Display::none;
    f.run();
    check(f.top({20, 20}, yellow, 5), "Non-displayed raised subtrees must leave the next layer on top");
}

void root_layers_stay_last() {
    // Root -> content(red, z max), modal(blue, z min)
    UiNode modal = box();
    modal.id = "modal";
    modal.properties["modal"] = true;
    Fixture f(box({box(), std::move(modal)}));
    f.styles[0].display = Display::flex;
    f.styles[1].height = Dimension::points(40);
    f.styles[1].background = red_fill;
    f.styles[1].z_index = std::numeric_limits<std::int32_t>::max();
    f.styles[2].background = blue_fill;
    f.styles[2].z_index = std::numeric_limits<std::int32_t>::min();
    f.run();
    check(f.top({20, 20}, blue, 2), "Root layers must ignore z_index and stay above ordinary root content");
}

void observation_order() {
    // Root(stack) -> first(red, z 1), second(green)
    Fixture f(box({box(), box()}));
    f.styles[1].background = red_fill;
    f.styles[1].z_index = 1;
    f.styles[2].background = green_fill;
    f.run();
    const auto snapshot = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    check(take(resolve_target(snapshot, PointTarget{{20, 20}})).index == 1,
          "Inspection point targets must select the topmost painted element");
}
} // namespace

int main() {
    try {
        sibling_order();
        subtree_scope_clip_and_visibility();
        root_layers_stay_last();
        observation_order();
        std::cout << "z_index sibling order, subtree scope, clips, root layers and observation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
