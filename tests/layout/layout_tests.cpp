#include <tessera/layout/layout_box.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using tessera::test::check;
using tessera::test::has;
using tessera::Dimension;
using tessera::Rect;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode node;
    node.children = std::move(children);
    return node;
}

tessera::UiNode text(std::string content) {
    tessera::UiNode node;
    node.kind = tessera::NodeKind::text;
    node.properties["text"] = std::move(content);
    return node;
}

// Styles are indexed by preorder node index, matching NodeHandle::index.
struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper placeholder;
    tessera::TextShaper* shaper = &placeholder;

    explicit Fixture(tessera::UiNode root) {
        tessera::UiDocument document;
        document.root = std::move(root);
        auto created = tessera::UiTree::create(document);
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
    }
    std::vector<tessera::Point> offsets; // Empty, or one requested scroll offset per node.

    tessera::Result<tessera::LayoutResult> run(tessera::Size viewport) {
        return tessera::compute_layout({tree.get(), styles, viewport, shaper, offsets});
    }
    tessera::LayoutResult layout(tessera::Size viewport) {
        auto result = run(viewport);
        check(static_cast<bool>(result), "Valid layout rejected");
        const auto repeated = run(viewport);
        check(repeated && *repeated.value == *result.value, "Repeated layout differs");
        return std::move(*result.value);
    }
};

Rect at(const tessera::LayoutResult& result, std::size_t index) { return result.boxes.at(index).border_box; }

void fixed_dimensions() {
    Fixture f(box());
    auto& root = f.styles[0];
    root.margin = {10, 20, 30, 40};
    check(at(f.layout({200, 100}), 0) == Rect{{40, 10}, {140, 60}}, "Automatic root must fill the viewport less margins");

    root.width = Dimension::points(50);
    root.min_width = 80;
    root.height = Dimension::points(5);
    root.padding = {4, 0, 4, 0};
    root.border = {1, 0, 1, 0};
    const auto result = f.layout({200, 100});
    check(at(result, 0) == Rect{{40, 10}, {80, 10}}, "Fixed size must clamp to min and the border/padding floor");
    check(result.boxes[0].node == f.tree->root() && result.boxes[0].parent == tessera::no_layout_parent,
          "Root identity differs");

    root.width = Dimension::points(500);
    root.max_width = 120;
    check(at(f.layout({200, 100}), 0).size.width == 120, "Fixed size must clamp to max");
}

void column_stack() {
    Fixture f(box({box(), box(), text("abcd")}));
    f.styles[0].padding = {10, 10, 10, 10};
    f.styles[0].gap = 5;
    f.styles[1].height = Dimension::points(20);
    f.styles[2].height = Dimension::points(10);
    f.styles[2].margin = {3, 4, 2, 6};
    const auto result = f.layout({100, 200});
    check(at(result, 1) == Rect{{10, 10}, {80, 20}}, "First child must stretch across the content box");
    check(at(result, 2) == Rect{{16, 38}, {70, 10}}, "Margins and gap must offset the second child");
    check(at(result, 3) == Rect{{10, 55}, {80, 20}}, "Placeholder text height must come from the shaper");
    for (std::size_t i = 1; i < 4; ++i) check(result.boxes[i].parent == 0, "Child parent index differs");
}

void row_with_nested_padding() {
    Fixture f(box({box({text("ab")}), box()}));
    auto& root = f.styles[0];
    root.direction = tessera::FlexDirection::row;
    root.align = tessera::Align::start;
    root.padding = {8, 8, 8, 8};
    root.border = {2, 2, 2, 2};
    root.gap = 4;
    f.styles[1].padding = {5, 5, 5, 5};
    f.styles[2].text.size = 8; // 4 units per scalar, 10-unit line.
    f.styles[3].width = Dimension::points(40);
    f.styles[3].height = Dimension::points(30);
    f.styles[3].margin.left = 6;
    const auto result = f.layout({300, 100});
    check(result.boxes[0].content_box() == Rect{{10, 10}, {280, 80}}, "Root content box differs");
    check(at(result, 1) == Rect{{10, 10}, {18, 20}}, "Automatic box must wrap padded text content");
    check(at(result, 2) == Rect{{15, 15}, {8, 10}}, "Nested text must sit inside its parent's padding");
    check(result.boxes[2].parent == 1, "Nested parent index differs");
    check(at(result, 3) == Rect{{38, 10}, {40, 30}}, "Row gap and margin offset differ");
}

void justify_and_align() {
    Fixture f(box({box(), box()}));
    auto& root = f.styles[0];
    root.direction = tessera::FlexDirection::row;
    root.gap = 10;
    f.styles[1].width = Dimension::points(20);
    f.styles[1].height = Dimension::points(10);
    f.styles[2].width = Dimension::points(30);
    f.styles[2].height = Dimension::points(20);
    const auto place = [&](tessera::Justify justify, tessera::Align align) {
        root.justify = justify;
        root.align = align;
        const auto result = f.layout({100, 50});
        return std::pair{at(result, 1).origin, at(result, 2).origin};
    };
    using J = tessera::Justify;
    using A = tessera::Align;
    using P = std::pair<tessera::Point, tessera::Point>;
    check(place(J::start, A::start) == P{{0, 0}, {30, 0}}, "Start placement differs");
    check(place(J::center, A::center) == P{{20, 20}, {50, 15}}, "Centered placement differs");
    check(place(J::end, A::end) == P{{40, 40}, {70, 30}}, "End placement differs");
    check(place(J::space_between, A::stretch) == P{{0, 0}, {70, 0}}, "Space-between placement differs");
    check(at(f.layout({100, 50}), 1).size.height == 10, "Fixed cross size must not stretch");

    Fixture single(box({box()}));
    single.styles[0].justify = J::space_between;
    single.styles[1].height = Dimension::points(10);
    check(at(single.layout({100, 50}), 1).origin.y == 0, "A single space-between child must start");
}

void grow_shrink_and_limits() {
    Fixture f(box({box(), box(), box()}));
    f.styles[0].direction = tessera::FlexDirection::row;
    f.styles[1].grow = 1;
    f.styles[2].grow = 3;
    f.styles[3].width = Dimension::points(20);
    auto result = f.layout({200, 10});
    check(at(result, 1).size.width == 45 && at(result, 2) == Rect{{45, 0}, {135, 10}} &&
          at(result, 3).origin.x == 180, "Grow must split free space by factor");

    f.styles[2].max_width = 100;
    result = f.layout({200, 10});
    check(at(result, 1).size.width == 80 && at(result, 2).size.width == 100,
          "A max violation must freeze the item and redistribute");

    Fixture s(box({box(), box(), box()}));
    s.styles[0].direction = tessera::FlexDirection::row;
    s.styles[1].width = Dimension::points(80);
    s.styles[1].shrink = 1;
    s.styles[2].width = Dimension::points(40);
    s.styles[2].shrink = 1;
    s.styles[2].min_width = 35;
    s.styles[3].width = Dimension::points(20);
    result = s.layout({100, 10});
    check(at(result, 1).size.width == 45 && at(result, 2) == Rect{{45, 0}, {35, 10}} &&
          at(result, 3).origin.x == 80, "Shrink must respect minimums and redistribute");

    Fixture c(box({box(), box()}));
    c.styles[1].max_width = 50;
    c.styles[2].min_height = 30;
    result = c.layout({100, 100});
    check(at(result, 1) == Rect{{0, 0}, {50, 0}}, "Stretch must respect max_width");
    check(at(result, 2) == Rect{{0, 0}, {100, 30}}, "Automatic height must respect min_height");
}

void overflow() {
    Fixture f(box({box(), box()}));
    f.styles[0].justify = tessera::Justify::end;
    f.styles[0].align = tessera::Align::center;
    f.styles[1].height = Dimension::points(30);
    f.styles[2].height = Dimension::points(40);
    f.styles[2].width = Dimension::points(80);
    const auto result = f.layout({50, 50});
    check(at(result, 0).size == tessera::Size{50, 50}, "Undersized parent must keep its size");
    check(at(result, 2) == Rect{{0, 30}, {80, 40}}, "Overflow must extend past the end edges without shifting");
}

// Preorder: 0 root, 1 clipper, 2 wide, 3 tall (clips too), 4 inner, 5 sibling.
void overflow_clip() {
    Fixture f(box({box({box(), box({box()})}), box()}));
    f.styles[0].align = tessera::Align::start;
    auto& clipper = f.styles[1];
    clipper.width = Dimension::points(60);
    clipper.height = Dimension::points(40);
    clipper.border = {2, 2, 2, 2};
    clipper.padding = {3, 3, 3, 3};
    clipper.align = tessera::Align::start;
    clipper.overflow = tessera::Overflow::clip;
    f.styles[2].width = Dimension::points(80);
    f.styles[2].height = Dimension::points(10);
    f.styles[3].width = Dimension::points(20);
    f.styles[3].height = Dimension::points(50);
    f.styles[3].overflow = tessera::Overflow::clip;
    f.styles[4].width = Dimension::points(30);
    f.styles[4].height = Dimension::points(30);
    f.styles[5].height = Dimension::points(10);
    const auto result = f.layout({100, 100});
    const Rect padding{{2, 2}, {56, 36}};
    check(!result.boxes[0].clip && !result.boxes[1].clip && !result.boxes[5].clip,
          "A box's own overflow must not clip itself or its siblings");
    check(at(result, 2) == Rect{{5, 5}, {80, 10}} && result.boxes[2].clip == padding &&
              result.boxes[3].clip == padding, "Children must record the clipper's padding box without moving");
    check(result.boxes[4].clip == Rect{{5, 15}, {20, 23}}, "Nested clips must intersect");
    check(tessera::intersect({{0, 0}, {10, 10}}, {{20, 5}, {5, 5}}) == Rect{{20, 5}, {0, 5}},
          "Disjoint intersection must keep the larger origin with zero size");
}

// Preorder: 0 root, 1 scroller, 2 wide, 3 nested scroller, 4 inner, 5 tall.
void scroll_geometry() {
    Fixture f(box({box({box(), box({box()}), box()})}));
    f.styles[0].align = tessera::Align::start;
    auto& scroller = f.styles[1];
    scroller.width = Dimension::points(60);
    scroller.height = Dimension::points(40);
    scroller.border = {2, 2, 2, 2};
    scroller.padding = {3, 3, 3, 3};
    scroller.gap = 4;
    scroller.align = tessera::Align::start;
    scroller.overflow = tessera::Overflow::scroll;
    f.styles[2].width = Dimension::points(80);
    f.styles[2].height = Dimension::points(10);
    f.styles[3].width = Dimension::points(20);
    f.styles[3].height = Dimension::points(20);
    f.styles[3].overflow = tessera::Overflow::scroll;
    f.styles[4].width = Dimension::points(10);
    f.styles[4].height = Dimension::points(50);
    f.styles[5].width = Dimension::points(10);
    f.styles[5].height = Dimension::points(30);
    f.styles[5].margin.bottom = 1;

    const auto still = f.layout({100, 100});
    const Rect viewport{{2, 2}, {56, 36}};
    check(still.boxes[1].scroll == tessera::ScrollGeometry{{86, 75}, {0, 0}} && !still.boxes[0].scroll,
          "Extent must cover each child's margin box plus trailing padding, only on scroll boxes");
    check(still.boxes[1].scroll_limit() == tessera::Point{30, 39}, "Scroll limit must be extent less viewport");
    check(at(still, 2) == Rect{{5, 5}, {80, 10}} && at(still, 5) == Rect{{5, 43}, {10, 30}} &&
              still.boxes[2].clip == viewport && !still.boxes[1].clip,
          "Without offsets, scroll boxes place and clip like overflow clip");
    check(still.boxes[3].scroll == tessera::ScrollGeometry{{20, 50}, {0, 0}}, "Nested extent differs");

    f.offsets.assign(f.tree->size(), {});
    f.offsets[0] = {7, 7}; // Ignored: the root does not scroll.
    f.offsets[1] = {10, 12.5f};
    f.offsets[3] = {-5, 100};
    const auto moved = f.layout({100, 100});
    check(at(moved, 0) == at(still, 0) && at(moved, 1) == at(still, 1) && !moved.boxes[0].scroll,
          "Offsets must not move the scroll box itself or apply to other boxes");
    check(moved.boxes[1].scroll == tessera::ScrollGeometry{{86, 75}, {10, 12.5f}}, "In-range offset must apply");
    check(at(moved, 2) == Rect{{-5, -7.5f}, {80, 10}} && at(moved, 3) == Rect{{-5, 6.5f}, {20, 20}} &&
              at(moved, 5) == Rect{{-5, 30.5f}, {10, 30}}, "Children must move back by the offset");
    check(moved.boxes[2].clip == viewport && moved.boxes[5].clip == viewport, "The scroll viewport clip must not move");
    check(moved.boxes[3].scroll == tessera::ScrollGeometry{{20, 50}, {0, 30}}, "Requested offsets must clamp per axis");
    check(at(moved, 4) == Rect{{-5, -23.5f}, {10, 50}} && moved.boxes[4].clip == Rect{{2, 6.5f}, {13, 20}},
          "Nested scrolling must compose offsets and intersect viewports");

    f.offsets[1] = {1000, -3};
    check(f.layout({100, 100}).boxes[1].scroll->offset == tessera::Point{30, 0}, "Offsets must clamp to [0, limit]");

    Fixture empty(box({box()}));
    empty.styles[1].height = Dimension::points(10);
    empty.styles[1].padding = {1, 1, 1, 1};
    empty.styles[1].overflow = tessera::Overflow::scroll;
    empty.offsets = {{}, {5, 5}};
    const auto lone = empty.layout({20, 20});
    check(lone.boxes[1].scroll == tessera::ScrollGeometry{{20, 10}, {0, 0}},
          "An empty scroll box's extent is its padding box");
}

void fractional_sizes() {
    Fixture f(box({box(), box(), box()}));
    f.styles[0].direction = tessera::FlexDirection::row;
    f.styles[0].padding.left = 0.25f;
    for (std::size_t i = 1; i < 4; ++i) f.styles[i].grow = 1;
    const auto result = f.layout({100.25f, 10.5f});
    const float third = 100.0f / 3;
    for (std::size_t i = 1; i < 4; ++i) {
        const auto box = at(result, i);
        check(std::abs(box.size.width - third) <= 1e-4f, "Fractional grow share differs");
        check(std::abs(box.origin.x - (0.25f + third * static_cast<float>(i - 1))) <= 1e-4f, "Fractional offset differs");
        check(box.size.height == 10.5f, "Fractional cross size differs");
    }
}

void display_and_visibility() {
    Fixture f(box({box(), box({box()}), box(), box()}));
    f.styles[0].gap = 5;
    for (std::size_t i : {1, 2, 4, 5}) f.styles[i].height = Dimension::points(10);
    f.styles[2].display = tessera::Display::none;
    f.styles[2].height = Dimension::points(50);
    f.styles[4].visibility = tessera::Visibility::hidden;
    const auto result = f.layout({100, 100});
    check(result.boxes.size() == 4, "Display::none subtrees must be absent");
    check(result.boxes[2].node.index == 4 && at(result, 2).origin.y == 15 && !result.boxes[2].visible,
          "Hidden boxes keep geometry and gaps but are not visible");
    check(result.boxes[3].node.index == 5 && at(result, 3).origin.y == 30 && result.boxes[3].visible,
          "Non-displayed nodes must not consume gap");

    f.styles[0].display = tessera::Display::none;
    check(f.layout({100, 100}).boxes.empty(), "A non-displayed root yields no boxes");
}

class FailingShaper final : public tessera::TextShaper {
public:
    tessera::Result<tessera::TextMetrics> measure(std::string_view, const tessera::TextStyle&,
                                                  const tessera::TextConstraints&) override {
        return {std::nullopt, {{"font_missing", tessera::Severity::error, "/style/font", "Missing font.", {}}}};
    }
    tessera::Result<tessera::GlyphRun> shape(std::string_view, const tessera::TextStyle&,
                                            const tessera::TextConstraints&) override { return {}; }
};

void failures() {
    Fixture f(box({text("x")}));
    f.styles.pop_back();
    check(has(f.run({10, 10}).diagnostics, "style_count", "/styles"), "Invalid input must be rejected");

    Fixture t(box({text("x")}));
    FailingShaper failing;
    t.shaper = &failing;
    const auto measured = t.run({10, 10});
    check(!measured && has(measured.diagnostics, "font_missing", "/nodes/1/style/font"),
          "Text failures must be located at the node");

    Fixture o(box({box(), box()}));
    o.styles[0].direction = tessera::FlexDirection::row;
    o.styles[1].width = Dimension::points(3e38f);
    o.styles[2].width = Dimension::points(3e38f);
    o.styles[2].margin.left = 3e38f; // Each value is finite; the second origin is not.
    const auto overflowed = o.run({10, 10});
    check(!overflowed && has(overflowed.diagnostics, "non_finite_geometry", "/nodes/2"),
          "Float overflow must be diagnosed");

    Fixture s(box({box()}));
    s.styles[0].overflow = tessera::Overflow::scroll;
    s.styles[1].height = Dimension::points(3e38f);
    s.styles[1].margin.bottom = 3e38f; // The child fits in float range; the extent does not.
    const auto extent = s.run({10, 10});
    check(!extent && has(extent.diagnostics, "non_finite_geometry", "/nodes/0"),
          "A non-finite scroll extent must be diagnosed");
}

void text_wrapping() {
    Fixture column(box({box({text("ABCDEF")}), box()}));
    column.styles[2].text.size = 20;
    column.styles[1].padding = {2, 5, 3, 5};
    column.styles[3].height = Dimension::points(10);
    column.styles[0].gap = 4;
    const auto narrow = column.layout({30, 200});
    check(at(narrow, 1).size == tessera::Size{30, 80} && at(narrow, 2).size == tessera::Size{20, 75} &&
          at(narrow, 3).origin.y == 84, "Nested column must include wrapped descendant height and padding");
    const auto wide = column.layout({70, 200});
    check(at(wide, 1).size.height == 30 && at(wide, 3).origin.y == 34,
          "A new viewport must reflow text and sibling placement");
    column.styles[0].overflow = tessera::Overflow::scroll;
    const auto scrolled = column.layout({30, 60});
    check(scrolled.boxes[0].scroll_limit().y == 34, "Scroll extent must include wrapped child heights");

    Fixture row(box({box({text("ABCDEF")}), box({text("ABCDEF")})}));
    row.styles[0].direction = tessera::FlexDirection::row;
    row.styles[0].align = tessera::Align::start;
    row.styles[1].shrink = row.styles[3].shrink = 1;
    row.styles[2].text.size = row.styles[4].text.size = 20;
    const auto shrunk = row.layout({40, 200});
    check(at(shrunk, 1).size == tessera::Size{20, 75} && at(shrunk, 3).size == tessera::Size{20, 75},
          "Row cross height must use the final flex width");
    check(at(shrunk, 2).size == tessera::Size{20, 75}, "Nested text must use its parent's flex width");

    Fixture limited(box({text("ABCDE")}));
    limited.styles[0].align = tessera::Align::start;
    limited.styles[1].max_width = 20;
    limited.styles[1].text.size = 20;
    const auto bounded = limited.layout({100, 200});
    check(at(bounded, 1).size == tessera::Size{20, 75}, "max_width must constrain intrinsic text height");
    limited.styles[1].height = Dimension::points(10);
    check(at(limited.layout({100, 200}), 1).size.height == 10, "Fixed heights must retain their basis");
}

} // namespace

int main() {
    try {
        fixed_dimensions();
        column_stack();
        row_with_nested_padding();
        justify_and_align();
        grow_shrink_and_limits();
        overflow();
        overflow_clip();
        scroll_geometry();
        fractional_sizes();
        display_and_visibility();
        failures();
        text_wrapping();
        std::cout << "Fixed, stack, flex, constraint, overflow, visibility, and failure layout checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
