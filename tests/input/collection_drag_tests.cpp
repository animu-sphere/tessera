#include <tessera/ui/authoring.hpp>
#include <tessera/input/collection_drag.hpp>
#include "../check.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;
namespace ui = tessera::authoring;

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
// A TreeView with one branch and a disabled leaf, and a short scrolling ListView.
struct Fixture {
    ui::Session session;
    bool expanded = true;
    bool list_disabled = false;
    float scroll = 0;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    ValidationContext actions() const {
        ValidationContext context{{"toggle", "button"}};
        for (const auto* key : {"a", "b", "c", "d", "e", "x0", "x1", "x2", "x3", "x4", "x5"})
            context.actions.insert(std::string("select.") + key);
        return context;
    }
    ui::Element form() const {
        std::vector<ui::TreeItem> children;
        for (const auto* key : {"b", "c", "d"}) children.push_back({key, key, std::string("select.") + key});
        children.push_back({"e", "e", "select.e", "", false, true});
        std::vector<ui::ListItem> list;
        for (int i = 0; i < 6; ++i) list.push_back({"x" + std::to_string(i), "X", "select.x" + std::to_string(i)});
        return ui::Column(
            {ui::TreeView(
                 "Tree", "a", {{"a", "a", "select.a", "toggle", expanded, false, std::move(children)}},
                 {.node = {.id = "tree",
                           .style = {.width = Dimension::points(200), .height = Dimension::points(200), .gap = 4.f}}}),
             ui::ListView(
                 "List", std::nullopt, std::move(list),
                 {.node = {.id = "list", .style = {.width = Dimension::points(200), .height = Dimension::points(60)}},
                  .disabled = list_disabled}),
             ui::Button("Button", "button", {.node = {.id = "button"}})},
            {.id = "root", .style = {.align = Align::start, .gap = 8.f}});
    }
    void build() {
        const auto context = actions();
        auto authored = take(session.lower(form(), context));
        tree = take(UiTree::create(authored.document, context));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        std::vector<Point> requested(tree->size());
        requested[tree->find("list")->index] = {0, scroll};
        layout = take(compute_layout({tree.get(), styles, {320, 480}, &text, requested}));
        session.publish();
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const Rect& box(std::string_view id) const {
        const auto target = node(id);
        return std::find_if(layout.boxes.begin(), layout.boxes.end(),
                            [&](const LayoutBox& box) { return box.node == target; })
            ->border_box;
    }
    // A point inside a row, `fraction` of the way down.
    Point at(std::string_view id, float fraction = .5f) const {
        const auto& rect = box(id);
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height * fraction};
    }
    std::optional<CollectionDropTarget> target(std::string_view id, Point point) const {
        return take(collection_drop_target(hit(), id, point));
    }
};
std::chrono::microseconds clock{0};
InputEvent event(decltype(InputEvent::data) data) { return {clock += 1ms, std::move(data)}; }
InputEvent down(Point point, std::uint32_t pointer = 1, PointerButton button = PointerButton::primary) {
    return event(PointerDown{{pointer}, point, button});
}
InputEvent move(Point point, std::uint32_t pointer = 1) { return event(PointerMove{{pointer}, point}); }
InputEvent up(Point point, std::uint32_t pointer = 1) { return event(PointerUp{{pointer}, point}); }
CollectionDropTarget before(std::string key) { return {std::move(key), DropPlacement::before}; }
CollectionDropTarget after(std::string key) { return {std::move(key), DropPlacement::after}; }

void targets() {
    Fixture f;
    f.build();
    check(f.target("tree", f.at("tree-item-c", .25f)) == before("c") &&
              f.target("tree", f.at("tree-item-c", .75f)) == after("c"),
          "Row halves must select before and after");
    const auto c = f.box("tree-item-c");
    const auto gap = f.box("tree-item-d").origin.y - (c.origin.y + c.size.height);
    check(gap > 0 && f.target("tree", {c.origin.x + 2, c.origin.y + c.size.height + gap * .75f}) == before("d") &&
              f.target("tree", {c.origin.x + 2, c.origin.y + c.size.height + gap * .25f}) == after("c"),
          "A gap must select the nearest row");
    check(f.target("tree", f.at("tree-item-e", .2f)) == before("e"), "Disabled rows remain insertion points");
    const auto tree = f.box("tree");
    check(f.target("tree", {tree.origin.x + 4, tree.origin.y + tree.size.height - 2}) == after("e"),
          "Space below the rows must select after the last row");
    check(!f.target("tree", f.at("button")) && !f.target("tree", f.at("list-item-x0")),
          "Positions outside the collection must not target");
    check(f.target("list", f.at("list-item-x1", .1f)) == before("x1"), "ListView rows must target");
    const auto list = f.box("list");
    check(!f.target("list", {list.origin.x + 2, list.origin.y + list.size.height + 2}),
          "Clipped rows below a ListView must not target");
    check(!f.target("missing", {0, 0}), "An absent collection has no target");
    f.expanded = false;
    f.build();
    check(f.target("tree", {tree.origin.x + 4, tree.origin.y + tree.size.height - 2}) == after("a"),
          "Collapsed rows must not target");
    f.list_disabled = true;
    f.build();
    check(!f.target("list", f.at("list-item-x1")), "A disabled collection must not target");
    check(has(collection_drop_target(f.hit(), "button", {0, 0}).diagnostics, "collection_drag_target", "/id") &&
              has(collection_drop_target(f.hit(), "", {0, 0}).diagnostics, "invalid_identifier", "/id") &&
              has(collection_drop_target(f.hit(), "tree", {NAN, 0}).diagnostics, "invalid_position", "/position"),
          "Invalid collections and positions must reject");
}

void click() {
    Fixture f;
    f.build();
    CollectionDragDispatcher drag("tree");
    const auto c = f.at("tree-item-c");
    auto result = take(drag.dispatch(f.hit(), down(c)));
    check(!result.consumed && drag.pressed() && !drag.dragging(), "A row press must be observed, not consumed");
    result = take(drag.dispatch(f.hit(), move({c.x + 2, c.y + 2})));
    check(!result.consumed && !result.started && !result.target, "Travel below the threshold must not drag");
    result = take(drag.dispatch(f.hit(), up({c.x + 2, c.y + 2})));
    check(!result.consumed && !result.ended && !result.drop && !drag.pressed(),
          "A release below the threshold must stay an ordinary click");
    for (const auto point : {f.at("tree-item-e"), f.at("button"), f.at("list-item-x0")}) {
        (void)take(drag.dispatch(f.hit(), down(point)));
        check(!drag.pressed(), "Disabled rows, other controls and other collections must not start a press");
    }
    (void)take(drag.dispatch(f.hit(), down(c, 1, PointerButton::secondary)));
    check(!drag.pressed(), "A secondary press must not start a press");
    (void)take(drag.dispatch(f.hit(), down(f.at("tree-toggle-a"))));
    check(drag.pressed(), "A press on a branch's toggle must address its row");
}

void dragging() {
    Fixture f;
    f.build();
    CollectionDragDispatcher drag("tree");
    const auto c = f.at("tree-item-c");
    (void)take(drag.dispatch(f.hit(), down(c)));
    auto result = take(drag.dispatch(f.hit(), move(f.at("tree-item-b", .2f))));
    check(result.consumed && result.started && result.source == "c" && result.target == before("b") &&
              drag.dragging() && drag.source() == "c",
          "Crossing the threshold must start a drag and report the insertion point");
    result = take(drag.dispatch(f.hit(), move(f.at("tree-item-d", .8f))));
    check(result.consumed && !result.started && result.target == after("d"), "Moves must update the target");
    check(!take(drag.dispatch(f.hit(), move(f.at("button"), 2))).consumed &&
              !take(drag.dispatch(f.hit(), down(f.at("button"), 2))).consumed &&
              !take(drag.dispatch(f.hit(), event(KeyUp{Key::a}))).consumed &&
              !take(drag.dispatch(f.hit(), event(Scroll{c, {0, 4}}))).consumed && drag.dragging(),
          "Other pointers, key releases and wheel input must pass through");
    result = take(drag.dispatch(f.hit(), move(f.at("button"))));
    check(result.consumed && !result.target, "Leaving the collection must clear the target");
    result = take(drag.dispatch(f.hit(), up(f.at("tree-item-a", .9f))));
    check(result.consumed && result.ended && !result.canceled && result.drop == after("a") && !drag.pressed(),
          "The release must drop at its own insertion point");

    (void)take(drag.dispatch(f.hit(), down(c)));
    (void)take(drag.dispatch(f.hit(), move(f.at("tree-item-b"))));
    result = take(drag.dispatch(f.hit(), up(f.at("button"))));
    check(result.consumed && result.ended && result.canceled && !result.drop,
          "Releasing outside the collection must cancel");

    // A coarse threshold keeps a long press a click.
    CollectionDragDispatcher still("tree", {.threshold = 1000});
    (void)take(still.dispatch(f.hit(), down(c)));
    check(!take(still.dispatch(f.hit(), move(f.at("tree-item-e")))).consumed,
          "Travel below a configured threshold must not drag");
}

void cancellation() {
    Fixture f;
    f.build();
    CollectionDragDispatcher drag("tree");
    const auto c = f.at("tree-item-c");
    const auto start = [&] {
        (void)take(drag.dispatch(f.hit(), down(c)));
        (void)take(drag.dispatch(f.hit(), move(f.at("tree-item-b"))));
    };
    for (auto cancel : {event(KeyDown{Key::escape}), event(Cancel{})}) {
        start();
        const auto result = take(drag.dispatch(f.hit(), cancel));
        check(result.consumed && result.ended && result.canceled && !result.drop && !drag.pressed(),
              "Escape and Cancel must cancel a drag");
        check(!take(drag.dispatch(f.hit(), up(f.at("tree-item-b")))).consumed,
              "The release after cancellation must pass through");
    }
    start();
    check(!take(drag.dispatch(f.hit(), event(PointerCancel{{2}}))).consumed && drag.dragging(),
          "Another pointer's cancellation must not end the drag");
    auto result = take(drag.dispatch(f.hit(), event(PointerCancel{{1}})));
    check(result.consumed && result.canceled && !drag.pressed(), "PointerCancel must cancel the drag");
    for (auto interruption : {event(KeyDown{Key::tab}), event(tessera::TextInput{"a"}), event(Navigate{Direction::up}),
                              event(Activate{})}) {
        start();
        result = take(drag.dispatch(f.hit(), interruption));
        check(!result.consumed && result.ended && result.canceled && !drag.pressed(),
              "Other keyboard, text and logical input must cancel unconsumed");
    }
    (void)take(drag.dispatch(f.hit(), down(c)));
    result = take(drag.dispatch(f.hit(), event(KeyDown{Key::escape})));
    check(!result.consumed && !result.ended && !drag.pressed(), "Escape before dragging must stay with the press");
}

void replacement() {
    Fixture f;
    f.build();
    CollectionDragDispatcher list("list");
    const auto x0 = f.at("list-item-x0", .4f);
    (void)take(list.dispatch(f.hit(), down(x0)));
    (void)take(list.dispatch(f.hit(), move(f.at("list-item-x1", .2f))));
    const auto position = f.at("list-item-x1", .2f);
    f.scroll = f.box("list-item-x1").origin.y - f.box("list-item-x0").origin.y;
    f.build();
    auto result = take(list.refresh(f.hit()));
    check(!result.ended && result.source == "x0" &&
              result.target == take(collection_drop_target(f.hit(), "list", position)) && result.target != before("x1"),
          "Refresh must recompute the target at the last position after scrolling");
    f.list_disabled = true;
    f.build();
    result = take(list.refresh(f.hit()));
    check(result.ended && result.canceled && !list.pressed(), "Disabling the collection must cancel the drag");

    CollectionDragDispatcher tree("tree");
    (void)take(tree.dispatch(f.hit(), down(f.at("tree-item-c"))));
    (void)take(tree.dispatch(f.hit(), move(f.at("tree-item-b"))));
    f.expanded = false;
    f.build();
    result = take(tree.dispatch(f.hit(), up(f.at("tree-item-a"))));
    check(result.ended && result.canceled && !result.consumed && !result.drop,
          "Hiding the dragged row must cancel before routing the release");
}

void rejection() {
    Fixture f;
    f.build();
    CollectionDragDispatcher negative("tree", {.threshold = -1});
    check(
        has(negative.dispatch(f.hit(), down(f.at("tree-item-c"))).diagnostics, "collection_drag_options", "/options") &&
            !negative.pressed() && has(negative.refresh(f.hit()).diagnostics, "collection_drag_options", "/options"),
        "Invalid options must reject");
    CollectionDragDispatcher button("button");
    check(has(button.dispatch(f.hit(), down(f.at("button"))).diagnostics, "collection_drag_target", "/id"),
          "A non-collection ID must reject");
    CollectionDragDispatcher drag("tree");
    check(!drag.dispatch(f.hit(), event(PointerDown{{1}, {NAN, 0}})) && !drag.pressed(),
          "Invalid input must reject without a press");
    check(!drag.dispatch({}, down({0, 0})), "A missing snapshot must reject");
}

} // namespace

int main() {
    try {
        targets();
        click();
        dragging();
        cancellation();
        replacement();
        rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
