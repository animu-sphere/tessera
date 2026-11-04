#include <tessera/input/pointer.hpp>
#include <tessera/ui/property_metadata.hpp>
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

    explicit Fixture(tessera::UiNode root = box({button("start"), button("quit")}),
                     tessera::PointerSnapshotPolicy policy = tessera::PointerSnapshotPolicy::tree_identity)
        : dispatcher(policy) {
        tessera::UiDocument document;
        document.root = std::move(root);
        auto created = tessera::UiTree::create(document, {{"start", "quit", "root"}});
        check(static_cast<bool>(created), created.diagnostics.empty()
                                              ? "Fixture tree rejected"
                                              : "Fixture tree rejected: " + created.diagnostics.front().code + " at " +
                                                    created.diagnostics.front().path);
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
    void replace(tessera::UiNode root) {
        Fixture next(std::move(root));
        tree = std::move(next.tree);
        styles = std::move(next.styles);
        layout = std::move(next.layout);
    }
    tessera::HitTestResult hit(tessera::Point point) const {
        const auto result = tessera::hit_test(input(), point);
        check(result && result.diagnostics.empty(), "Valid hit query rejected");
        return *result.value;
    }
    template<class Event> tessera::PointerDispatchResult send(Event event) {
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
    check(f.hit({115, 95}).target == f.node(2),
          "Overflow must hit outside parent and viewport without implicit clipping");
    f.layout.boxes[2].border_box.size.width = 0;
    check(!f.hit({110, 95}).target, "Zero-area box must not hit");

    Fixture disabled(box({button("start", true)}));
    check(disabled.hit({1, 1}).target == disabled.node(0), "Disabled subtree must exclude both button and label");
    auto root = box({button("start")});
    root.properties["disabled"] = true;
    Fixture disabled_root(std::move(root));
    check(!disabled_root.hit({1, 1}).target, "Disabled root must exclude all descendants");
    // Absence follows the descriptor's absent value (false), matching an explicit false.
    auto implicit = button("start");
    implicit.properties.erase("disabled");
    const auto absent_value = tessera::find_property_descriptor(tessera::property_names::disabled)->absent_value;
    const auto implicit_target = Fixture(box({std::move(implicit)})).hit({1, 1}).target;
    const auto explicit_target = Fixture(box({button("start", false)})).hit({1, 1}).target;
    check(absent_value == tessera::Property{false} && implicit_target && explicit_target &&
              implicit_target->index == explicit_target->index,
          "Absent disabled must target like the descriptor default");
    Fixture absent;
    absent.styles[1].display = tessera::Display::none;
    absent.compute();
    check(absent.hit({1, 1}).target == absent.node(4),
          "Display-none subtree must not affect remaining preorder handles");
    absent.styles[0].display = tessera::Display::none;
    absent.compute();
    check(!absent.hit({1, 1}).target, "Display-none root must yield no hit");
}

// Preorder: 0 root, 1 list (clips at y = 30), 2 start, 3 label, 4 quit (straddles the clip), 5 label.
void clipped_targets_and_activation() {
    Fixture f(box({box({button("start"), button("quit")})}));
    f.styles[1].height = tessera::Dimension::points(30);
    f.styles[1].overflow = tessera::Overflow::clip;
    f.compute();
    check(f.layout.boxes[4].border_box == tessera::Rect{{0, 20}, {40, 20}} &&
              f.layout.boxes[5].clip == tessera::Rect{{0, 0}, {40, 30}},
          "Clip fixture geometry differs");
    check(f.hit({10, 29.5f}).target == f.node(5), "Visible part of a straddling box must hit");
    check(f.hit({10, 30}).target == f.node(0) && f.hit({10, 35}).target == f.node(0),
          "Clipped part must fall through to the box below, with a half-open clip edge");

    f.send(tessera::PointerDown{{1}, {10, 35}});
    check(f.send(tessera::PointerUp{{1}, {10, 35}}).actions.empty(), "Clipped click must not activate");
    f.send(tessera::PointerDown{{1}, {10, 25}});
    auto dragged = f.send(tessera::PointerMove{{1}, {10, 35}});
    check(dragged.pointers[0].pressed == f.node(4) && !dragged.pointers[0].active,
          "Dragging into the clipped part must leave the binding");
    check(f.send(tessera::PointerUp{{1}, {10, 35}}).actions.empty(), "Release in the clipped part must not activate");
    f.send(tessera::PointerDown{{1}, {10, 25}});
    check(f.send(tessera::PointerUp{{1}, {10, 25}}).actions ==
              std::vector<tessera::ActionRequest>{{"activate", "quit", f.node(4)}},
          "Click in the visible part must activate");
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
    check(cancel.actions.empty() && !cancel.target && cancel.pointers.size() == 1,
          "Cancel must erase only its pointer");
    check(f.send(tessera::PointerUp{{9}, {1, 1}}).actions.empty(), "Cancelled press must not activate");
    auto quit = f.send(tessera::PointerUp{{2}, {1, 21}});
    check(quit.actions.size() == 1 && quit.actions[0].action == "quit",
          "Other pointer's press must survive cancellation");
    f.send(tessera::PointerDown{{0}, {1, 1}, tessera::PointerButton::secondary});
    check(f.send(tessera::PointerUp{{0}, {1, 1}, tessera::PointerButton::secondary}).actions.empty(),
          "Secondary click must not activate");
    f.send(tessera::PointerDown{{0}, {1, 1}});
    auto middle = f.send(tessera::PointerUp{{0}, {1, 1}, tessera::PointerButton::middle});
    check(middle.actions.empty() && middle.pointers[0].primary_down,
          "Other-button release must not clear primary press");
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
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(),
          "Reappearing target must not resurrect a cancelled press");

    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.styles[1].display = tessera::Display::none;
    f.compute();
    check(f.dispatcher.refresh(f.input()).value->pointers[0].hovered == f.node(4),
          "Refresh must retarget stationary pointer");
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(),
          "Disappeared target must not activate shifted sibling");

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
    check(has(f.dispatcher.dispatch(f.input(), {0us, tessera::PointerCancel{{1}}}).diagnostics, "event_order",
              "/timestamp"),
          "Backwards timestamp needs located diagnostic");
    check(has(f.dispatcher.dispatch(f.input(), {2us, tessera::Activate{}}).diagnostics, "unsupported_event", "/event"),
          "Non-pointer event accepted");
    check(
        !f.dispatcher.dispatch(f.input(), {2us, tessera::PointerUp{{1}, {std::numeric_limits<float>::quiet_NaN(), 1}}}),
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
    check(has(tessera::hit_test(f.input(), {0, std::numeric_limits<float>::infinity()}).diagnostics, "invalid_number",
              "/position/y"),
          "Non-finite hit position accepted");
    f.layout.boxes[1].node = other.tree->root();
    check(has(tessera::hit_test(f.input(), {}).diagnostics, "layout_node", "/layout/boxes/1/node"),
          "Foreign layout handle accepted by hit test");
}

tessera::UiNode stable_buttons() {
    auto start = button("start");
    start.id = "start-button";
    auto quit = button("quit");
    quit.id = "quit-button";
    return box({std::move(start), std::move(quit)});
}

void compatible_replacements_retarget_owned_identities() {
    Fixture f(stable_buttons(), tessera::PointerSnapshotPolicy::compatible_author_ids);
    f.send(tessera::PointerDown{{1}, {1, 1}});
    const auto original = f.node(1);
    for (int i = 0; i < 4; ++i) {
        auto root = stable_buttons();
        root.children.insert(root.children.begin(), box());
        root.children[1].children[0].properties["text"] = std::string("telemetry ") + std::to_string(i);
        f.replace(std::move(root));
        const auto refreshed = f.dispatcher.refresh(f.input());
        check(refreshed && refreshed.value->actions.empty() && !refreshed.value->target &&
                  refreshed.value->pointers.size() == 1,
              "Replacement refresh must retain one pointer without producing actions");
        const auto& state = refreshed.value->pointers[0];
        check(!f.tree->get(original) && state.primary_down && state.pressed == f.node(2) && state.active == f.node(2) &&
                  state.hovered == f.node(3),
              "Compatible replacements must resolve the author ID and hit in the current tree, not reuse indices");
    }
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions ==
              std::vector<tessera::ActionRequest>{{"activate", "start", f.node(2)}},
          "Multiple replacements must activate exactly once on the current owner");
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(), "Replacement must not duplicate activation");

    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.send(tessera::PointerMove{{1}, {-1, -1}});
    f.replace(stable_buttons());
    const auto outside = f.dispatcher.refresh(f.input());
    check(outside && outside.value->pointers[0].pressed == f.node(1) && !outside.value->pointers[0].active &&
              !outside.value->pointers[0].hovered,
          "Replacement outside must retain capture without active or hover feedback");
    check(f.send(tessera::PointerUp{{1}, {-1, -1}}).actions.empty(),
          "Release outside after replacement must remain inert");
    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.send(tessera::PointerMove{{1}, {-1, -1}});
    f.replace(stable_buttons());
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.size() == 1,
          "Dispatch without an explicit refresh must reconcile a returning press");

    f.send(tessera::PointerMove{{2}, {1, 1}});
    f.replace(stable_buttons());
    f.styles[1].display = tessera::Display::none;
    f.compute();
    const auto shifted = f.dispatcher.refresh(f.input());
    check(shifted && shifted.value->pointers.size() == 2 && shifted.value->pointers[1].hovered == f.node(4) &&
              !shifted.value->pointers[1].primary_down,
          "Stationary hover without a press must follow accepted replacement geometry");

    f.dispatcher.reset();
    f.time = 0us;
    f.replace(stable_buttons());
    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.replace(stable_buttons());
    check(f.send(tessera::PointerUp{{1}, {1, 21}}).actions.empty(),
          "Another owner's release after replacement must not activate");
}

void incompatible_replacements_cancel_permanently() {
    for (const std::string_view failure :
         {"removed", "missing_id", "changed_action", "missing_binding", "kind", "hidden", "display_none", "disabled",
          "ancestor_disabled", "pointer_events", "ancestor_pointer_events", "clipped", "zero_area", "modal"}) {
        Fixture f(stable_buttons(), tessera::PointerSnapshotPolicy::compatible_author_ids);
        f.send(tessera::PointerDown{{1}, {1, 1}});
        auto root = stable_buttons();
        if (failure == "removed") root.children.erase(root.children.begin());
        if (failure == "missing_id") root.children[0].id.reset();
        if (failure == "changed_action") root.children[0].events["activate"] = "quit";
        if (failure == "missing_binding") root.children[0].events.clear();
        if (failure == "kind") {
            root.children[0].kind = tessera::NodeKind::text;
            root.children[0].properties["text"] = std::string("changed kind");
            root.children[0].children.clear();
        }
        if (failure == "disabled") root.children[0].properties["disabled"] = true;
        if (failure == "ancestor_disabled") root.properties["disabled"] = true;
        if (failure == "pointer_events") root.children[0].properties["pointer_events"] = false;
        if (failure == "ancestor_pointer_events") root.properties["pointer_events"] = false;
        if (failure == "modal") {
            root.children[1].properties["modal"] = true;
            root.children[1].events.clear();
        }
        f.replace(std::move(root));
        if (failure == "hidden") f.styles[1].visibility = tessera::Visibility::hidden;
        if (failure == "display_none") f.styles[1].display = tessera::Display::none;
        if (failure == "clipped") {
            f.styles[0].overflow = tessera::Overflow::clip;
            f.styles[1].margin.top = 100;
        }
        if (failure == "zero_area") f.styles[1].width = tessera::Dimension::points(0);
        f.compute();
        const auto refreshed = f.dispatcher.refresh(f.input());
        check(refreshed && refreshed.value->actions.empty() && refreshed.value->pointers.size() == 1 &&
                  refreshed.value->pointers[0].primary_down && !refreshed.value->pointers[0].pressed &&
                  !refreshed.value->pointers[0].active,
              "Unsafe replacement must cancel its capture: " + std::string(failure));
        f.replace(stable_buttons());
        f.send(tessera::PointerDown{{1}, {1, 1}});
        check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(),
              "Restoration or repeated down must not revive a cancelled capture: " + std::string(failure));
        f.send(tessera::PointerDown{{1}, {1, 1}});
        check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.size() == 1,
              "A fresh press must work after safe cancellation");
    }

    Fixture anonymous(box({button("start")}), tessera::PointerSnapshotPolicy::compatible_author_ids);
    anonymous.send(tessera::PointerDown{{1}, {1, 1}});
    check(anonymous.send(tessera::PointerUp{{1}, {1, 1}}).actions.size() == 1,
          "Anonymous same-snapshot buttons must retain ordinary click behavior");
    anonymous.send(tessera::PointerDown{{1}, {1, 1}});
    anonymous.replace(stable_buttons());
    check(anonymous.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(),
          "A press without a stable original author ID must not migrate");

    auto duplicate = stable_buttons();
    duplicate.children[1].id = duplicate.children[0].id;
    tessera::UiDocument document;
    document.root = std::move(duplicate);
    check(
        has(tessera::UiTree::create(document, {{"start", "quit"}}).diagnostics, "duplicate_id", "/root/children/1/id"),
        "Ambiguous author IDs must be rejected before publication");
}

void compatible_cancellation_and_rejection_are_safe() {
    Fixture f(stable_buttons(), tessera::PointerSnapshotPolicy::compatible_author_ids);
    f.send(tessera::PointerDown{{9}, {1, 1}});
    f.send(tessera::PointerDown{{2}, {1, 21}});
    f.replace(stable_buttons());
    auto before = f.dispatcher.refresh(f.input());
    check(before && before.value->pointers[0].pressed == f.node(3) && before.value->pointers[1].pressed == f.node(1),
          "Each pointer must independently resolve its replacement owner");
    f.send(tessera::PointerCancel{{9}});
    f.replace(stable_buttons());
    check(f.send(tessera::PointerUp{{9}, {1, 1}}).actions.empty(),
          "Capture/focus-loss cancellation must survive later compatible replacements");
    check(f.send(tessera::PointerUp{{2}, {1, 21}}).actions ==
              std::vector<tessera::ActionRequest>{{"activate", "quit", f.node(3)}},
          "Cancellation must not affect another pointer");
    f.send(tessera::PointerDown{{2}, {1, 21}});
    f.dispatcher.reset();
    f.time = 0us;
    f.replace(stable_buttons());
    check(f.send(tessera::PointerUp{{2}, {1, 21}}).actions.empty(),
          "Reset must discard compatible captures as well as the stream clock");

    f.send(tessera::PointerDown{{1}, {1, 1}});
    before = f.dispatcher.refresh(f.input());
    Fixture next(stable_buttons());
    auto invalid = next.input();
    invalid.styles = invalid.styles.first(1);
    check(!f.dispatcher.refresh(invalid) &&
              !f.dispatcher.dispatch(invalid, {f.time + 1us, tessera::PointerCancel{{1}}}) &&
              !f.dispatcher.dispatch(next.input(), {0us, tessera::PointerUp{{1}, {1, 1}}}),
          "Invalid replacement input or time must be rejected before remapping");
    check(f.dispatcher.refresh(f.input()).value == before.value,
          "Rejected replacement must leave the accepted pointer snapshot unchanged");
    const auto up = f.dispatcher.dispatch(next.input(), {f.time + 1us, tessera::PointerUp{{1}, {1, 1}}});
    check(up && up.value->actions == std::vector<tessera::ActionRequest>{{"activate", "start", next.node(1)}},
          "A valid retry must retain the original compatible capture");

    f.dispatcher.reset();
    f.time = 0us;
    f.send(tessera::PointerDown{{1}, {1, 1}});
    f.styles[1].visibility = tessera::Visibility::hidden;
    f.compute();
    check(!f.dispatcher.refresh(f.input()).value->pointers[0].pressed,
          "Same-tree ineligibility must still cancel an opted-in capture");
    f.replace(stable_buttons());
    check(f.send(tessera::PointerUp{{1}, {1, 1}}).actions.empty(),
          "Replacement must not resurrect a press cancelled in the same tree");

    Fixture defaults(stable_buttons());
    defaults.send(tessera::PointerDown{{1}, {1, 1}});
    defaults.replace(stable_buttons());
    check(defaults.dispatcher.refresh(defaults.input()).value->pointers.empty(),
          "The default must still clear all pointers even with compatible stable IDs");
    tessera::PointerDispatcher invalid_policy(static_cast<tessera::PointerSnapshotPolicy>(255));
    check(has(invalid_policy.refresh(defaults.input()).diagnostics, "unknown_value", "/policy") &&
              has(invalid_policy.dispatch(defaults.input(), {0us, tessera::PointerCancel{{1}}}).diagnostics,
                  "unknown_value", "/policy"),
          "Invalid replacement policy must fail explicitly on refresh and dispatch");
}

} // namespace

int main() {
    try {
        hit_order_edges_overflow_and_eligibility();
        clipped_targets_and_activation();
        click_binding_lookup_and_drag();
        cancellation_buttons_and_multiple_pointers();
        refresh_replacement_and_invalid_input();
        compatible_replacements_retarget_owned_identities();
        incompatible_replacements_cancel_permanently();
        compatible_cancellation_and_rejection_are_safe();
        std::cout << "Pointer targeting/binding/cancellation, multi-pointer, refresh, and validation checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
