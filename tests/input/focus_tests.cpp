#include <tessera/input/focus.hpp>
#include "../check.hpp"
#include <iostream>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

tessera::UiNode item(std::string id, std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.id = std::move(id);
    result.properties["focusable"] = true;
    result.children = std::move(children);
    return result;
}
tessera::UiNode label(std::string id) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.id = std::move(id);
    result.properties["text"] = std::string("label");
    return result;
}
tessera::UiNode button(std::string id, std::string action) {
    auto result = item(id, {label(id + "-label")});
    result.events["activate"] = std::move(action);
    return result;
}
tessera::UiNode root(std::vector<tessera::UiNode> children) {
    tessera::UiNode result;
    result.id = "root";
    result.children = std::move(children);
    return result;
}

struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;
    tessera::FocusDispatcher focus;
    std::chrono::microseconds time{0};

    explicit Fixture(tessera::UiNode node) {
        tessera::UiDocument document;
        document.root = std::move(node);
        auto created = tessera::UiTree::create(document, {{"start", "quit", "open", "back", "close"}});
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        for (auto& style : styles) style.align = tessera::Align::start;
        compute();
    }
    void compute() {
        auto result = tessera::compute_layout({tree.get(), styles, {100, 200}, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::NodeHandle id(std::string_view author) const { return *tree->find(author); }
    tessera::ResolvedStyle& style(std::string_view author) { return styles[id(author).index]; }
    tessera::LayoutBox& box(std::string_view author) {
        for (auto& box : layout.boxes) {
            if (box.node == id(author)) return box;
        }
        throw std::runtime_error("Fixture box missing");
    }
    tessera::HitTestInput input() const { return {tree.get(), styles, &layout}; }
    template<class Event>
    tessera::FocusDispatchResult send(Event event, const tessera::HitTestInput* snapshot = nullptr) {
        time += 1us;
        const auto result = focus.dispatch(snapshot ? *snapshot : input(), {time, std::move(event)});
        check(result && result.diagnostics.empty(), "Valid focus dispatch rejected");
        return *result.value;
    }
    std::optional<tessera::NodeHandle> refresh(const tessera::HitTestInput* snapshot = nullptr) {
        const auto result = focus.refresh(snapshot ? *snapshot : input());
        check(result && result.diagnostics.empty(), "Valid focus refresh rejected");
        return result.value->focused;
    }
    void set(std::string_view author) {
        const auto result = focus.focus(input(), id(author));
        check(result && result.value->focused == id(author), "Eligible focus request rejected");
    }
};

void sequential_traversal_and_eligibility() {
    auto options = button("options", "open");
    options.properties["disabled"] = true;
    auto credits = item("credits");
    credits.properties.erase("focusable"); // Absence follows the descriptor default (false).
    Fixture f(root({button("start", "start"), std::move(options), std::move(credits), item("extras"),
                    item("group", {item("inner")}), button("quit", "quit")}));
    f.style("extras").visibility = tessera::Visibility::hidden;
    f.style("group").visibility = tessera::Visibility::hidden;
    f.compute();
    check(f.send(tessera::FocusNext{}).focused == f.id("start"), "First FocusNext must focus the first eligible node");
    check(f.send(tessera::FocusNext{}).focused == f.id("inner"),
          "Traversal must skip disabled/non-focusable/hidden nodes but keep a visible child of a hidden parent");
    check(f.send(tessera::FocusNext{}).focused == f.id("quit"), "FocusNext must follow tree preorder");
    check(f.send(tessera::FocusNext{}).focused == f.id("start"), "FocusNext must wrap to the first node");
    check(f.send(tessera::FocusPrevious{}).focused == f.id("quit"), "FocusPrevious must wrap to the last node");
    check(f.send(tessera::FocusPrevious{}).focused == f.id("inner"), "FocusPrevious must reverse preorder");

    f.focus.reset();
    check(f.send(tessera::FocusPrevious{}).focused == f.id("quit"), "First FocusPrevious must focus the last node");
    f.style("group").display = tessera::Display::none;
    f.compute();
    f.set("start");
    check(f.send(tessera::FocusNext{}).focused == f.id("quit"), "Display-none subtrees must be skipped");

    Fixture nothing(root({}));
    const auto result = nothing.send(tessera::FocusNext{});
    check(!result.focused && result.actions.empty() && !nothing.send(tessera::Navigate{}).focused,
          "A snapshot without focusable nodes must succeed without focus");
}

// Boxes are placed directly; only border boxes matter for directional movement.
void directional_navigation() {
    Fixture f(root({item("a"), item("b"), item("c"), item("d"), item("e"), item("f"), item("g")}));
    const auto place = [&](std::string_view author, tessera::Rect rect) { f.box(author).border_box = rect; };
    place("a", {{0, 0}, {20, 10}});
    place("b", {{30, 0}, {20, 10}});
    place("c", {{0, 20}, {20, 10}});
    place("d", {{30, 20}, {20, 10}});
    place("e", {{0, 60}, {20, 10}});  // Far below c, aligned with it.
    place("f", {{25, 30}, {4, 4}});   // Touches c's bottom edge, unaligned with it.
    place("g", {{60, 0}, {20, 10}});  // Rightmost.
    using tessera::Direction;
    const auto move = [&](std::string_view from, Direction direction) {
        f.set(from);
        return f.send(tessera::Navigate{direction}).focused;
    };
    f.focus.reset();
    check(f.send(tessera::Navigate{Direction::up}).focused == f.id("a"), "Navigate without focus must focus the first node");
    check(move("a", Direction::right) == f.id("b") && move("a", Direction::down) == f.id("c"),
          "Grid moves must reach the adjacent aligned box");
    check(move("d", Direction::left) == f.id("c") && move("d", Direction::up) == f.id("b"),
          "Left/up moves must use the leading edges");
    check(move("g", Direction::right) == f.id("g"), "No candidate must keep focus without wrapping");
    check(move("c", Direction::down) == f.id("e"), "An aligned box must win over a nearer unaligned box");
    check(move("b", Direction::left) == f.id("a"), "Nearer unaligned boxes must lose to an aligned one");
    f.style("e").visibility = tessera::Visibility::hidden;
    f.box("e").visible = false;
    check(move("c", Direction::down) == f.id("f"), "A touching unaligned box must be reachable");
    place("f", {{0, 25}, {4, 10}});
    check(move("c", Direction::down) == f.id("c"), "A box starting before the far edge must be excluded");
    // Equal score: earlier preorder wins.
    place("g", {{30, 20}, {20, 10}});
    check(move("c", Direction::right) == f.id("d"), "Equal candidates must tie-break by preorder");
    place("b", {{0, 40}, {20, 10}});
    place("d", {{0, 40}, {20, 10}});
    check(move("a", Direction::down) == f.id("c"), "The nearest aligned edge must win");
    check(move("c", Direction::down) == f.id("b") && f.send(tessera::Navigate{Direction::down, true}).focused == f.id("b"),
          "Equal boxes must tie-break by preorder; repeat must not change selection");
}

void activation_and_cancel() {
    auto start = button("start", "start");
    start.events["cancel"] = "close";
    auto open = button("open", "open");
    open.properties["focusable"] = false;
    open.children[0].properties["focusable"] = true; // A focusable label activates its button.
    auto top = root({std::move(start), button("quit", "quit"), std::move(open), item("info")});
    top.events["cancel"] = "back";
    Fixture f(std::move(top));
    using Requests = std::vector<tessera::ActionRequest>;
    check(f.send(tessera::Activate{}).actions.empty(), "Activate without focus must not request an action");
    check(f.send(tessera::Cancel{}).actions == Requests{{"cancel", "back", f.id("root")}},
          "Cancel without focus must use the root binding");
    check(f.send(tessera::FocusNext{}).focused == f.id("start"), "Focus fixture order differs");
    const auto activated = f.send(tessera::Activate{});
    check(activated.actions == Requests{{"activate", "start", f.id("start")}} && activated.focused == f.id("start"),
          "Activate must request the focused binding without moving focus");

    // The pointer path produces the same request for the same button.
    tessera::PointerDispatcher pointer;
    const auto point = f.box("start").content_box().origin;
    pointer.dispatch(f.input(), {1us, tessera::PointerDown{{1}, point}});
    const auto click = pointer.dispatch(f.input(), {2us, tessera::PointerUp{{1}, point}});
    check(click && click.value->actions == activated.actions, "Focus activation must equal pointer activation");

    check(f.send(tessera::Cancel{}).actions == Requests{{"cancel", "close", f.id("start")}},
          "Cancel must prefer the focused node's own binding");
    f.set("open-label");
    check(f.send(tessera::Activate{}).actions == Requests{{"activate", "open", f.id("open")}},
          "A focused label must activate its nearest eligible binding owner");
    f.set("info");
    check(f.send(tessera::Activate{}).actions.empty(), "A focused node without a binding must not activate");
    check(f.send(tessera::Cancel{}).actions == Requests{{"cancel", "back", f.id("root")}},
          "Cancel must walk to the nearest ancestor binding");
}

void recovery_and_replacement() {
    const auto menu = [](bool with_b = true, bool disable_b = false) {
        std::vector<tessera::UiNode> children{button("a", "start")};
        if (with_b) {
            children.push_back(button("b", "quit"));
            children.back().properties["disabled"] = disable_b;
        }
        children.push_back(button("c", "open"));
        children.push_back(item(""));
        children.back().id.reset(); // Focusable without an author ID.
        return root(std::move(children));
    };
    Fixture f(menu());
    const auto last = f.layout.boxes.back().node;
    f.set("b");
    f.style("b").visibility = tessera::Visibility::hidden;
    f.compute();
    check(f.refresh() == f.id("c"), "Hidden focus must recover to the following node");
    f.style("b").visibility = tessera::Visibility::visible;
    f.compute();
    check(f.focus.focus(f.input(), last).value->focused == last, "Focus without an author ID rejected");
    f.styles[last.index].display = tessera::Display::none;
    f.compute();
    check(f.refresh() == f.id("c"), "Removed last focus must recover to the preceding node");
    check(f.refresh() == f.id("c"), "Refresh of eligible focus must not move it");
    f.styles[last.index].display = tessera::Display::flex;
    f.compute();

    // A command that finds the focus ineligible only reports the recovery.
    f.set("a");
    f.style("a").visibility = tessera::Visibility::hidden;
    f.compute();
    const auto consumed = f.send(tessera::Activate{});
    check(consumed.focused == f.id("b") && consumed.actions.empty(), "Recovery must consume Activate");
    f.style("a").visibility = tessera::Visibility::visible;
    f.compute();
    check(f.send(tessera::FocusNext{}).focused == f.id("c"), "Commands must resume after recovery");

    // Replacement trees restore focus by author ID; handles never carry over.
    Fixture same(menu());
    auto snapshot = same.input();
    check(f.refresh(&snapshot) == same.id("c"), "Replacement must restore focus by author ID");
    Fixture disabled(menu(true, true));
    f.set("b");
    snapshot = disabled.input();
    check(f.refresh(&snapshot) == disabled.id("c"), "An ineligible restored ID must recover beside it");
    Fixture removed(menu(false));
    f.set("b");
    snapshot = removed.input();
    check(f.refresh(&snapshot) == removed.id("c"), "A missing author ID must recover after its nearest surviving predecessor");
    check(f.focus.focus(f.input(), last).value->focused == last, "Focus without an author ID rejected");
    snapshot = same.input();
    const auto moved = f.send(tessera::FocusNext{}, &snapshot);
    check(moved.focused == same.layout.boxes.back().node && moved.actions.empty(),
          "Focus without an author ID must recover by position and consume the command");
}

// A replacement tree without the focused author ID recovers in its place: after the nearest surviving
// focusable predecessor, else before the nearest surviving successor; with neither, focus clears.
void replacement_recovers_removed_focus() {
    const auto ids = [](std::vector<std::string> names) {
        std::vector<tessera::UiNode> children;
        for (auto& name : names) children.push_back(button(std::move(name), "open"));
        return root(std::move(children));
    };
    Fixture f(ids({"a", "b", "c", "d"}));
    const auto recover = [&](std::string_view from, const Fixture& next) {
        f.set(from);
        const auto snapshot = next.input();
        return f.refresh(&snapshot);
    };
    Fixture last(ids({"a", "b", "c"}));
    check(recover("d", last) == last.id("c"), "A removed last item must recover to its predecessor");
    Fixture first(ids({"b", "c", "d"}));
    check(recover("a", first) == first.id("b"), "A removed first item must recover to its successor");
    Fixture inserted(ids({"a", "n", "c", "d"}));
    check(recover("b", inserted) == inserted.id("n"), "A replaced item must recover to the node now in its place");
    Fixture skipped(ids({"a", "d"}));
    check(recover("b", skipped) == skipped.id("d"), "Recovery must skip removed neighbors");
    auto disabled_menu = ids({"a", "c", "d"});
    disabled_menu.children[1].properties["disabled"] = true;
    Fixture disabled(std::move(disabled_menu));
    check(recover("b", disabled) == disabled.id("d"), "Recovery in place must skip ineligible nodes");
    Fixture unrelated(ids({"x", "y"}));
    check(!recover("b", unrelated), "Without a surviving neighbor, focus must clear");

    f.set("b");
    auto snapshot = unrelated.input();
    const auto lost = f.send(tessera::FocusNext{}, &snapshot);
    check(!lost.focused && lost.actions.empty(), "Losing focus during dispatch must consume the command");
    check(f.send(tessera::FocusNext{}, &snapshot).focused == unrelated.id("x"), "Traversal must restart after losing focus");
}

void programmatic_focus_and_validation() {
    auto disabled = button("off", "open");
    disabled.properties["disabled"] = true;
    auto plain = item("plain");
    plain.properties["focusable"] = false;
    Fixture f(root({button("start", "start"), std::move(disabled), std::move(plain), item("hidden"), item("gone")}));
    f.style("hidden").visibility = tessera::Visibility::hidden;
    f.style("gone").display = tessera::Display::none;
    f.compute();
    Fixture other(root({button("start", "start")}));
    check(has(f.focus.focus(f.input(), other.id("start")).diagnostics, "stale_target", "/target"),
          "Foreign handle accepted");
    check(has(f.focus.focus(f.input(), f.id("hidden")).diagnostics, "hidden_target", "/target") &&
              has(f.focus.focus(f.input(), f.id("gone")).diagnostics, "hidden_target", "/target"),
          "Hidden or display-none focus accepted");
    check(has(f.focus.focus(f.input(), f.id("off")).diagnostics, "disabled_target", "/target") &&
              has(f.focus.focus(f.input(), f.id("off-label")).diagnostics, "disabled_target", "/target"),
          "Disabled focus accepted");
    check(has(f.focus.focus(f.input(), f.id("plain")).diagnostics, "not_focusable", "/target"),
          "Non-focusable focus accepted");

    f.set("start");
    const auto before = f.refresh();
    check(has(f.focus.dispatch(f.input(), {1us, tessera::PointerDown{{1}, {1, 1}}}).diagnostics, "unsupported_event", "/event") &&
              has(f.focus.dispatch(f.input(), {1us, tessera::KeyDown{tessera::Key::tab}}).diagnostics,
                  "unsupported_event", "/event"),
          "Pointer or key event accepted; hosts translate keys into logical commands");
    check(has(f.focus.dispatch(f.input(), {1us, tessera::Navigate{static_cast<tessera::Direction>(9)}}).diagnostics,
              "unknown_value", "/direction"), "Invalid direction accepted");
    check(f.send(tessera::FocusNext{}).focused == f.id("start"), "A single focusable node must wrap to itself");
    check(has(f.focus.dispatch(f.input(), {0us, tessera::FocusNext{}}).diagnostics, "event_order", "/timestamp"),
          "Backwards timestamp accepted");
    auto invalid = f.input();
    invalid.styles = invalid.styles.first(1);
    check(!f.focus.dispatch(invalid, {f.time, tessera::FocusNext{}}) && !f.focus.refresh(invalid) &&
              !f.focus.focus(invalid, f.id("start")),
          "Invalid snapshot accepted");
    check(f.refresh() == before, "Rejected calls must leave focus unchanged");
    check(f.focus.dispatch(f.input(), {f.time, tessera::Activate{}}).value->actions.size() == 1,
          "Equal timestamp must be accepted");
    f.focus.reset();
    const auto restarted = f.focus.dispatch(f.input(), {0us, tessera::Activate{}});
    check(restarted && !restarted.value->focused && restarted.value->actions.empty(),
          "Reset must clear focus and restart the clock");
}

} // namespace

int main() {
    try {
        sequential_traversal_and_eligibility();
        directional_navigation();
        activation_and_cancel();
        recovery_and_replacement();
        replacement_recovers_removed_focus();
        programmatic_focus_and_validation();
        std::cout << "Focus traversal, directional navigation, activation/cancel, recovery, and validation checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
