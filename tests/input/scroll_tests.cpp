#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using tessera::Dimension;
using tessera::Point;
using tessera::Rect;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.children = std::move(children);
    return result;
}
tessera::UiNode button(std::string action) {
    auto label = box();
    label.kind = tessera::NodeKind::text;
    label.properties["text"] = std::string("item");
    auto result = box({std::move(label)});
    result.events["activate"] = std::move(action);
    return result;
}

// Preorder: 0 scrolling root, 1 list, buttons 2/4/6/8 with labels 3/5/7/9, 10 spacer.
// The root's viewport is 60x40 with extent 60; the list's is 60x30 with extent 80.
struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    std::vector<Point> offsets;
    mutable tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;

    Fixture() {
        tessera::UiDocument document;
        document.root = box({box({button("a"), button("b"), button("c"), button("d")}), box()});
        auto created = tessera::UiTree::create(document, {{"a", "b", "c", "d"}});
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        offsets.resize(tree->size());
        for (std::size_t i = 0; i < styles.size(); ++i)
            styles[i].background = {0.1f * static_cast<float>(i % 10), 0.5f, 0.5f, 1};
        styles[0].overflow = tessera::Overflow::scroll;
        styles[1].overflow = tessera::Overflow::scroll;
        styles[1].height = Dimension::points(30);
        for (std::size_t i : {2, 4, 6, 8}) styles[i].height = Dimension::points(20);
        styles[10].height = Dimension::points(30);
        compute();
    }
    void compute() {
        auto result = tessera::compute_layout({tree.get(), styles, {60, 40}, &shaper, offsets});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    void apply(const std::vector<tessera::ScrollUpdate>& updates) {
        for (const auto& update : updates) offsets[update.container.index] = update.offset;
        compute();
    }
    tessera::NodeHandle node(std::uint32_t index) const { return {tree->root().tree, index}; }
    tessera::HitTestInput input() const { return {tree.get(), styles, &layout}; }
    tessera::ScrollRouteResult route(Point position, Point delta) const {
        auto result = tessera::route_scroll(input(), {position, delta});
        check(result && result.diagnostics.empty(), "Valid scroll rejected");
        return std::move(*result.value);
    }
    std::vector<tessera::ScrollUpdate> reveal(std::uint32_t index) const {
        auto result = tessera::scroll_into_view(input(), node(index));
        check(result && result.diagnostics.empty(), "Valid scroll-into-view rejected");
        return std::move(*result.value);
    }
    float top(std::uint32_t index) const { return layout.boxes.at(index).border_box.origin.y; }
};

bool inside(const Rect& rect, Point point) {
    return point.x >= rect.origin.x && point.x < rect.origin.x + rect.size.width && point.y >= rect.origin.y &&
           point.y < rect.origin.y + rect.size.height;
}

// The topmost background painted at each sample must be the hit target's border box. Every box paints an
// opaque background and none is disabled, so paint and input must agree everywhere, including clip edges.
void check_agreement(const Fixture& f, std::string_view what) {
    auto paint = tessera::build_paint_list({f.tree.get(), f.styles, &f.layout, &f.shaper});
    check(static_cast<bool>(paint), "Scrolled paint rejected");
    for (float y = -4; y < 44; y += 0.5f) {
        for (float x : {1.0f, 30.0f, 59.5f}) {
            const Point point{x, y};
            std::optional<Rect> clip;
            std::optional<Rect> painted;
            for (const auto& command : paint.value->commands) {
                if (const auto* push = std::get_if<tessera::PushClip>(&command)) clip = push->rect;
                else if (std::holds_alternative<tessera::PopClip>(command)) clip.reset();
                else if (const auto* rect = std::get_if<tessera::DrawRect>(&command);
                         rect && inside(rect->rect, point) && (!clip || inside(*clip, point)))
                    painted = rect->rect;
            }
            const auto hit = tessera::hit_test(f.input(), point);
            check(static_cast<bool>(hit), "Hit query rejected");
            std::optional<Rect> target;
            if (hit.value->target) {
                for (const auto& box : f.layout.boxes)
                    if (box.node == *hit.value->target) target = box.border_box;
            }
            check(painted == target, std::string(what) + ": paint and hit testing disagree");
        }
    }
}

void routing_chains_per_axis() {
    Fixture f;
    check(f.layout.boxes[1].scroll_limit() == Point{0, 50} && f.layout.boxes[0].scroll_limit() == Point{0, 20},
          "Fixture limits differ");
    check_agreement(f, "unscrolled");

    auto routed = f.route({30, 25}, {0, 25});
    check(routed.target == f.node(4) || routed.target == f.node(5), "Scroll must hit the second item");
    check(routed.updates == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 25}}} && routed.remaining == Point{},
          "The innermost scroll box must consume the delta it can");
    f.apply(routed.updates);
    check(f.top(2) == -25 && f.top(6) == 15, "Items must move by the applied offset");
    check_agreement(f, "list scrolled");

    routed = f.route({30, 10}, {3, 40});
    check(routed.updates == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 50}}, {f.node(0), {0, 15}}} &&
              routed.remaining == Point{3, 0},
          "Leftover delta must chain outward per axis and report what nothing consumed");
    f.apply(routed.updates);
    check(f.top(1) == -15 && f.top(8) == -5 && f.top(10) == 15, "Nested offsets must compose");
    check_agreement(f, "both scrolled");

    routed = f.route({30, 20}, {0, 10});
    check(routed.target == f.node(10) && routed.updates == std::vector<tessera::ScrollUpdate>{{f.node(0), {0, 20}}},
          "Outside the list, only the root may scroll");
    routed = f.route({30, 10}, {0, -60});
    check(routed.updates == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 0}}, {f.node(0), {0, 5}}} &&
              routed.remaining == Point{},
          "Negative deltas must chain toward the start");
    routed = f.route({30, 10}, {0, -70});
    check(routed.updates.size() == 2 && routed.updates[1].offset == Point{} && routed.remaining == Point{0, -5},
          "Both limits reached must leave the remainder");

    auto miss = f.route({70, 10}, {0, 5});
    check(!miss.target && miss.updates.empty() && miss.remaining == Point{0, 5}, "A miss must not scroll");

    f.styles[1].visibility = tessera::Visibility::hidden;
    f.compute();
    routed = f.route({30, 10}, {0, 100});
    check((routed.target == f.node(8) || routed.target == f.node(9)) &&
              routed.updates == std::vector<tessera::ScrollUpdate>{{f.node(0), {0, 20}}},
          "A hidden scroll box must not receive scrolling over its visible items");
}

void reveal_minimal_nested_and_oversized() {
    Fixture f;
    check(f.reveal(2).empty() && f.reveal(1).empty(), "Visible targets must not scroll");
    check(f.reveal(8) == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 50}}}, "Reveal must align the trailing edge");
    check(f.reveal(10) == std::vector<tessera::ScrollUpdate>{{f.node(0), {0, 20}}}, "Reveal must scroll the root");

    f.offsets[0] = {0, 20};
    f.compute();
    check(f.reveal(4) == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 10}}, {f.node(0), {0, 10}}},
          "Outer boxes must reveal the position adjusted by inner offsets");
    f.apply(f.reveal(4));
    check(f.top(1) == -10 && f.top(4) == 0, "Revealed item must start at the visible top");
    check(f.reveal(4).empty(), "A revealed target must be stable");
    check(f.reveal(2) == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 0}}, {f.node(0), {0, 0}}},
          "Reveal must align the leading edge");

    f.styles[6].height = Dimension::points(45);
    f.offsets = std::vector<Point>(f.tree->size());
    f.compute();
    check(f.reveal(6) == std::vector<tessera::ScrollUpdate>{{f.node(1), {0, 40}}},
          "A target larger than a viewport must align its leading edge");
}

// With fractional geometry the revealed edge lands within float rounding of the viewport edge; revealing
// again must still request no change, so hosts can treat an empty reveal as "in view".
void reveal_is_stable_under_rounding() {
    for (int step = 1; step < 10; ++step) {
        Fixture f;
        for (std::size_t i : {2, 4, 6, 8}) f.styles[i].height = Dimension::points(20 + 0.1f * static_cast<float>(step));
        f.styles[1].margin = {0.3f * static_cast<float>(step), 0, 0, 0};
        f.compute();
        for (std::uint32_t target : {8u, 6u, 2u, 10u, 4u}) {
            f.apply(f.reveal(target));
            check(f.reveal(target).empty(), "A revealed target must be stable under rounding");
        }
    }
}

void invalid_input() {
    Fixture f;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto bad = tessera::route_scroll(f.input(), {{nan, 0}, {0, nan}});
    check(!bad && has(bad.diagnostics, "invalid_number", "/position/x") &&
              has(bad.diagnostics, "invalid_number", "/delta/y"),
          "Non-finite scroll values must be rejected");

    Fixture other;
    const auto stale = tessera::scroll_into_view(f.input(), other.node(4));
    check(!stale && has(stale.diagnostics, "stale_target", "/target"), "Foreign handles must be rejected");
    f.styles[8].display = tessera::Display::none;
    f.compute();
    const auto hidden = tessera::scroll_into_view(f.input(), f.node(9));
    check(!hidden && has(hidden.diagnostics, "hidden_target", "/target"), "Display-none targets must be rejected");

    f.layout.boxes[1].scroll->offset.y = 51;
    const auto out = tessera::route_scroll(f.input(), {{30, 10}, {0, 1}});
    check(!out && has(out.diagnostics, "layout_scroll", "/layout/boxes/1/scroll"), "Out-of-range offsets must be rejected");
    f.layout.boxes[1].scroll.reset();
    check(has(tessera::validate(f.input()), "layout_scroll", "/layout/boxes/1/scroll"),
          "Missing scroll geometry must be rejected");
}

} // namespace

int main() {
    try {
        routing_chains_per_axis();
        reveal_minimal_nested_and_oversized();
        reveal_is_stable_under_rounding();
        invalid_input();
        std::cout << "Scroll routing, chaining, reveal, paint/hit agreement, and rejection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
