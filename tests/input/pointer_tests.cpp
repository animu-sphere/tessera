#include <tessera/input/pointer.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.children = std::move(children);
    return result;
}
tessera::UiNode button(std::string action, bool disabled = false) {
    auto label = box();
    label.kind = tessera::NodeKind::text;
    label.properties["text"] = std::string("label");
    auto result = box({std::move(label)});
    result.events["activate"] = std::move(action);
    result.properties["disabled"] = disabled;
    return result;
}

struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;
    tessera::PointerDispatcher dispatcher;
    std::chrono::microseconds time{0};

    explicit Fixture(tessera::UiNode root = box({button("start"), button("quit")})) {
        tessera::UiDocument document;
        document.root = std::move(root);
        auto created = tessera::UiTree::create(document, {{"start", "quit", "root"}});
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        for (auto& style : styles) style.align = tessera::Align::start;
        compute();
    }
    void compute() {
        auto result = tessera::compute_layout({tree.get(), styles, {100, 80}, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::NodeHandle node(std::uint32_t index) const { return {tree->root().tree, index}; }
    tessera::HitTestInput input() const { return {tree.get(), styles, &layout}; }
    tessera::HitTestResult hit(tessera::Point point) const {
        const auto result = tessera::hit_test(input(), point);
        check(result && result.diagnostics.empty(), "Valid hit query rejected");
        return *result.value;
    }
    template<class Event>
    tessera::PointerDispatchResult send(Event event) {
        time += 1us;
        const auto result = dispatcher.dispatch(input(), {time, std::move(event)});
        check(result && result.diagnostics.empty(), "Valid pointer dispatch rejected");
        return *result.value;
    }
};

void hit_order_edges_overflow_and_eligibility() {
    Fixture f(box({button("start"), box()}));
    f.layout.boxes[1].border_box = {{0, 0}, {80, 50}};
    f.layout.boxes[2].border_box = {{10, 10}, {20, 20}};
    f.layout.boxes[3].border_box = {{15, 15}, {40, 40}};
    check(f.hit({16, 16}).target == f.node(3), "Later transparent sibling must be topmost");
    check(f.hit({10, 10}).target == f.node(2), "Left/top edge must hit deepest label");
    check(f.hit({30, 10}).target == f.node(1), "Right label edge must select containing button");
    check(f.hit({10, 30}).target == f.node(1), "Bottom label edge must select containing button");
    check(!f.hit({100, 20}).target && !f.hit({20, 80}).target, "Root right/bottom edges must be excluded");
    check(!f.hit({-1, -1}).target, "Outside query must return successful no-hit");
    f.styles[3].opacity = 0;
    check(f.hit({16, 16}).target == f.node(3), "Zero opacity must not change targeting");
    f.styles[3].visibility = tessera::Visibility::hidden;
    f.layout.boxes[3].visible = false;
    check(f.hit({16, 16}).target == f.node(2), "Hidden top box must be excluded");
    f.styles[1].visibility = tessera::Visibility::hidden;
    f.layout.boxes[1].visible = false;
    check(f.hit({16, 16}).target == f.node(2), "Locally visible descendant of hidden parent must hit");
    f.layout.boxes[2].border_box = {{110, 90}, {20, 10}};
    check(f.hit({115, 95}).target == f.node(2), "Overflow must hit outside parent and viewport without implicit clipping");
    f.layout.boxes[2].border_box.size.width = 0;
    check(!f.hit({110, 95}).target, "Zero-area box must not hit");

    Fixture disabled(box({button("start", true)}));
    check(disabled.hit({1, 1}).target == disabled.node(0), "Disabled subtree must exclude both button and label");
    auto root = box({button("start")});
    root.properties["disabled"] = true;
    Fixture disabled_root(std::move(root));
    check(!disabled_root.hit({1, 1}).target, "Disabled root must exclude all descendants");
    Fixture absent;
    absent.styles[1].display = tessera::Display::none;
    absent.compute();
    check(absent.hit({1, 1}).target == absent.node(4), "Display-none subtree must not affect remaining preorder handles");
    absent.styles[0].display = tessera::Display::none;
    absent.compute();
    check(!absent.hit({1, 1}).target, "Display-none root must yield no hit");
}

void click_binding_lookup_and_drag() {
    auto root = box({button("start"), button("quit")});
    root.events["activate"] = "root";
    Fixture f(std::move(root));
    auto moved = f.send(tessera::PointerMove{{7}, {1, 1}});
    check(moved.target == f.node(2) && moved.pointers[0].hovered == f.node(2), "Hover must target the label");
    auto down = f.send(tessera::PointerDown{{7}, {1, 1}});
    check(down.actions.empty() && down.pointers[0].pressed == f.node(1) && down.pointers[0].active == f.node(1),
          "Press must find button binding before root binding");
    // Enlarge the button without moving its label: label-to-padding movement still activates.
    f.layout.boxes[1].border_box.size.width = 60;
    auto up = f.send(tessera::PointerUp{{7}, {50, 1}});
    check(up.target == f.node(1) && up.actions == std::vector<tessera::ActionRequest>{{"activate", "start", f.node(1)}},
          "Release on same binding owner's padding must activate exactly once");
    check(!up.pointers[0].pressed && !up.pointers[0].active && !up.pointers[0].primary_down,
          "Release must clear press/active state");
    check(f.send(tessera::PointerUp{{7}, {1, 1}}).actions.empty(), "Release without press must not activate");

    f.send(tessera::PointerDown{{7}, {1, 1}});
    auto outside = f.send(tessera::PointerMove{{7}, {-1, -1}});
    check(outside.pointers[0].pressed == f.node(1) && !outside.pointers[0].active && !outside.pointers[0].hovered,
          "Dragging out must retain press but clear active/hover");
    auto inside = f.send(tessera::PointerMove{{7}, {1, 1}});
    check(inside.pointers[0].active == f.node(1), "Dragging back must restore active");
    check(f.send(tessera::PointerUp{{7}, {1, 1}}).actions.size() == 1, "Drag-out/back click must activate");
    f.send(tessera::PointerDown{{7}, {1, 1}});
    check(f.send(tessera::PointerUp{{7}, {1, 21}}).actions.empty(), "Release on a different button must not activate");
    f.send(tessera::PointerDown{{7}, {-1, -1}});
    f.send(tessera::PointerDown{{7}, {1, 1}});
    check(f.send(tessera::PointerUp{{7}, {1, 1}}).actions.empty(), "Repeated down must not replace an outside press");

    Fixture nested(box({button("start")}));
    nested.styles[1].visibility = tessera::Visibility::hidden;
    nested.compute();
    nested.send(tessera::PointerDown{{0}, {1, 1}});
    check(nested.send(tessera::PointerUp{{0}, {1, 1}}).actions.empty(), "Hidden binding ancestor must not activate");
}

void cancellation_buttons_and_multiple_pointers() {
    Fixture f;
    f.send(tessera::PointerDown{{9}, {1, 1}});
    auto down = f.send(tessera::PointerDown{{2}, {1, 21}});
    check(down.pointers.size() == 2 && down.pointers[0].pointer.value == 2 && down.pointers[1].pointer.value == 9,
          "Independent pointer snapshots must use stable ID order");
    auto cancel = f.send(tessera::PointerCancel{{9}});
    check(cancel.actions.empty() && !cancel.target && cancel.pointers.size() == 1, "Cancel must erase only its pointer");
    check(f.send(tessera::PointerUp{{9}, {1, 1}}).actions.empty(), "Cancelled press must not activate");
    auto quit = f.send(tessera::PointerUp{{2}, {1, 21}});
    check(quit.actions.size() == 1 && quit.actions[0].action == "quit", "Other pointer's press must survive cancellation");
    f.send(tessera::PointerDown{{0}, {1, 1}, tessera::PointerButton::secondary});
    check(f.send(tessera::PointerUp{{0}, {1, 1}, tessera::PointerButton::secondary}).actions.empty(),
          "Secondary click must not activate");
    f.send(tessera::PointerDown{{0}, {1, 1}});
    auto middle = f.send(tessera::PointerUp{{0}, {1, 1}, tessera::PointerButton::middle});
    check(middle.actions.empty() && middle.pointers[0].primary_down, "Other-button release must not clear primary press");
    check(f.send(tessera::PointerUp{{0}, {1, 1}}).actions.size() == 1, "Primary release must still activate");
    check(f.send(tessera::PointerCancel{{123}}).actions.empty(), "Unknown pointer cancel must be harmless");
}

void refresh_replacement_and_invalid_input() {
    Fixture f;
    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.styles[1].visibility = tessera::Visibility::hidden;
    f.compute();
    auto refreshed = f.dispatcher.refresh(f.input());
    check(refreshed && refreshed.value->actions.empty() && !refreshed.value->pointers[0].pressed,
          "Stationary hidden target must cancel its press");
    f.styles[1].visibility = tessera::Visibility::visible;
    f.compute();
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(), "Reappearing target must not resurrect a cancelled press");

    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.styles[1].display = tessera::Display::none;
    f.compute();
    check(f.dispatcher.refresh(f.input()).value->pointers[0].hovered == f.node(4), "Refresh must retarget stationary pointer");
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(), "Disappeared target must not activate shifted sibling");

    Fixture other;
    auto replaced = f.dispatcher.dispatch(other.input(), {f.time + 1us, tessera::PointerUp{{1}, {1, 1}}});
    check(replaced && replaced.value->actions.empty() && !replaced.value->pointers[0].pressed,
          "New tree snapshot must not retain old press handles");
    check(f.dispatcher.refresh(f.input()).value->pointers.empty(), "New identity on refresh must clear all pointers");
    f.dispatcher.reset();
    f.time = 0us;
    f.styles[1].display = tessera::Display::flex;
    f.compute();
    f.send(tessera::PointerDown{{1}, {1, 1}});
    const auto before = f.dispatcher.refresh(f.input());
    check(!f.dispatcher.dispatch(f.input(), {0us, tessera::PointerCancel{{1}}}), "Backwards timestamp accepted");
    check(has(f.dispatcher.dispatch(f.input(), {0us, tessera::PointerCancel{{1}}}).diagnostics,
              "event_order", "/timestamp"), "Backwards timestamp needs located diagnostic");
    check(has(f.dispatcher.dispatch(f.input(), {2us, tessera::Activate{}}).diagnostics,
              "unsupported_event", "/event"), "Non-pointer event accepted");
    check(!f.dispatcher.dispatch(f.input(), {2us, tessera::PointerUp{{1}, {std::numeric_limits<float>::quiet_NaN(), 1}}}),
          "Invalid pointer geometry accepted");
    auto invalid = f.input();
    invalid.styles = invalid.styles.first(1);
    check(!f.dispatcher.refresh(invalid) && !f.dispatcher.dispatch(invalid, {2us, tessera::PointerCancel{{1}}}),
          "Invalid snapshot accepted");
    const auto after = f.dispatcher.refresh(f.input());
    check(before && after && *before.value == *after.value, "Rejected calls must leave hover/press state unchanged");
    auto same_time = f.dispatcher.dispatch(f.input(), {1us, tessera::PointerUp{{1}, {1, 1}}});
    check(same_time && same_time.value->actions.size() == 1, "Equal timestamp release must preserve the valid press");
    f.dispatcher.reset();
    check(f.dispatcher.dispatch(f.input(), {0us, tessera::PointerUp{{1}, {1, 1}}}).value->actions.empty(),
          "Reset must discard presses and restart the clock");

    check(has(tessera::hit_test({}, {}).diagnostics, "missing_input", "/tree"), "Missing hit dependencies accepted");
    check(has(tessera::hit_test(f.input(), {0, std::numeric_limits<float>::infinity()}).diagnostics,
              "invalid_number", "/position/y"), "Non-finite hit position accepted");
    f.layout.boxes[1].node = other.tree->root();
    check(has(tessera::hit_test(f.input(), {}).diagnostics, "layout_node", "/layout/boxes/1/node"),
          "Foreign layout handle accepted by hit test");
}

} // namespace

int main() {
    try {
        hit_order_edges_overflow_and_eligibility();
        click_binding_lookup_and_drag();
        cancellation_buttons_and_multiple_pointers();
        refresh_replacement_and_invalid_input();
        std::cout << "Pointer targeting/binding/cancellation, multi-pointer, refresh, and validation checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
