#include <tessera/ui/authoring.hpp>
#include <tessera/input/drag_drop.hpp>
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
constexpr const char* color = "application/x-color";
constexpr const char* uri = "text/uri-list";
constexpr DragOperations copy{.copy = true};
// Two sources, a panel accepting colors and URIs that holds a link-only target and a disabled
// color target, a short scrolling ListView whose rows accept colors, and a plain button.
struct Fixture {
    ui::Session session;
    bool swatch_disabled = false;
    float scroll = 0;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    DragDropScope scope{{{"swatch", color, copy}, {"file", uri, {.move = true, .copy = true, .link = true}}},
                        {{"panel", {color, uri}, {.move = true, .copy = true}},
                         {"inner", {uri}, {.link = true}},
                         {"off", {color}, copy},
                         {"absent", {color}, copy}}};
    Fixture() {
        for (int i = 0; i < 6; ++i) scope.targets.push_back({"list-item-x" + std::to_string(i), {color}, copy});
    }
    ValidationContext actions() const {
        ValidationContext context{{"button"}};
        for (int i = 0; i < 6; ++i) context.actions.insert("select.x" + std::to_string(i));
        return context;
    }
    ui::Element form() const {
        std::vector<ui::ListItem> list;
        for (int i = 0; i < 6; ++i) list.push_back({"x" + std::to_string(i), "X", "select.x" + std::to_string(i)});
        return ui::Column(
            {ui::Row({ui::Button("Swatch", "button", {.node = {.id = "swatch"}, .disabled = swatch_disabled}),
                      ui::Button("File", "button", {.node = {.id = "file"}})},
                     {.id = "sources", .style = {.gap = 8.f}}),
             ui::Column({ui::Button("Inner", "button", {.node = {.id = "inner"}}),
                         ui::Button("Off", "button", {.node = {.id = "off"}, .disabled = true})},
                        {.id = "panel",
                         .style = {.width = Dimension::points(200),
                                   .height = Dimension::points(120),
                                   .padding = Edges{8, 8, 8, 8},
                                   .gap = 8.f}}),
             ui::ListView(
                 "List", std::nullopt, std::move(list),
                 {.node = {.id = "list", .style = {.width = Dimension::points(200), .height = Dimension::points(60)}}}),
             ui::Button("Plain", "button", {.node = {.id = "plain"}})},
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
    const Rect& box(std::string_view id) const {
        const auto target = *tree->find(id);
        return std::find_if(layout.boxes.begin(), layout.boxes.end(),
                            [&](const LayoutBox& box) { return box.node == target; })
            ->border_box;
    }
    Point at(std::string_view id, float fx = .5f, float fy = .5f) const {
        const auto& rect = box(id);
        return {rect.origin.x + rect.size.width * fx, rect.origin.y + rect.size.height * fy};
    }
    const DragSource& source(std::string_view id) const {
        return *std::find_if(scope.sources.begin(), scope.sources.end(),
                             [&](const DragSource& source) { return source.id == id; });
    }
    std::optional<DragOver> target(std::string_view source_id, Point point, Modifiers modifiers = {}) const {
        return take(drop_target_at(hit(), scope, source(source_id), point, modifiers));
    }
};
std::chrono::microseconds clock{0};
InputEvent event(decltype(InputEvent::data) data) { return {clock += 1ms, std::move(data)}; }
InputEvent down(Point point, std::uint32_t pointer = 1, PointerButton button = PointerButton::primary) {
    return event(PointerDown{{pointer}, point, button});
}
InputEvent move(Point point, Modifiers modifiers = {}, std::uint32_t pointer = 1) {
    return event(PointerMove{{pointer}, point, modifiers});
}
InputEvent up(Point point, Modifiers modifiers = {}, std::uint32_t pointer = 1) {
    return event(PointerUp{{pointer}, point, PointerButton::primary, modifiers});
}
DragOver over(std::string target, DragOperation operation) { return {std::move(target), operation}; }

void operations() {
    constexpr DragOperations all{.move = true, .copy = true, .link = true};
    constexpr Modifiers control{.control = true}, shift{.shift = true}, both{.shift = true, .control = true};
    check(drag_operation(all, {}) == DragOperation::move && drag_operation(all, control) == DragOperation::copy &&
              drag_operation(all, shift) == DragOperation::move && drag_operation(all, both) == DragOperation::link,
          "Control, Shift and Control+Shift must request copy, move and link");
    check(drag_operation(DragOperations{.copy = true, .link = true}, {}) == DragOperation::copy &&
              drag_operation(DragOperations{.link = true}, {}) == DragOperation::link &&
              drag_operation(all, Modifiers{.alt = true, .super = true}) == DragOperation::move,
          "Unmodified drags must prefer move, then copy, then link; other modifiers must be ignored");
    check(!drag_operation(copy, shift) && !drag_operation(copy, both) && !drag_operation({}, {}),
          "A requested operation outside the allowed set must return none");
    check(all.contains(DragOperation::link) && !copy.contains(DragOperation::move) && DragOperations{}.empty(),
          "Operation sets must report their members");
}

void validation() {
    check(validate(Fixture{}.scope).empty(), "The fixture scope must be valid");
    const DragDropScope invalid{
        {{"", color, copy}, {"a", "Application/X-Color", copy}, {"a", "text", copy}, {"b", "text/", {}}},
        {{"t", {}, copy}, {"t", {"text/plain", "text/plain", "a/b c"}, {}}}};
    const auto errors = validate(invalid);
    check(has(errors, "invalid_identifier", "/sources/0/id") && has(errors, "invalid_drag_type", "/sources/1/type") &&
              has(errors, "duplicate_identifier", "/sources/2/id") &&
              has(errors, "invalid_drag_type", "/sources/2/type") &&
              has(errors, "invalid_drag_type", "/sources/3/type") &&
              has(errors, "drag_operations", "/sources/3/operations") &&
              has(errors, "invalid_drag_type", "/targets/0/types") &&
              has(errors, "duplicate_identifier", "/targets/1/id") &&
              has(errors, "duplicate_drag_type", "/targets/1/types/1") &&
              has(errors, "invalid_drag_type", "/targets/1/types/2") &&
              has(errors, "drag_operations", "/targets/1/operations"),
          "Scope validation must locate identifier, type and operation errors");
    check(validate(DragDropScope{{{"s", "vnd.a-b+c/x.y_z!#$&^", copy}}, {}}).empty(),
          "Restricted-name punctuation must be accepted");
}

void targets() {
    Fixture f;
    f.build();
    constexpr Modifiers control{.control = true}, both{.shift = true, .control = true};
    check(f.target("swatch", f.at("panel", .5f, .9f)) == over("panel", DragOperation::copy),
          "A target must accept its types with the shared operation");
    check(f.target("swatch", f.at("inner")) == over("panel", DragOperation::copy),
          "A target that rejects the type must defer to an enclosing target");
    check(f.target("file", f.at("inner")) == over("inner", DragOperation::link) &&
              f.target("file", f.at("inner"), control) == over("panel", DragOperation::copy) &&
              f.target("file", f.at("panel", .5f, .9f)) == over("panel", DragOperation::move) &&
              !f.target("file", f.at("panel", .5f, .9f), both),
          "Modifiers must select among the operations each target shares");
    check(f.target("swatch", f.at("off")) == over("panel", DragOperation::copy), "A disabled target must not accept");
    check(f.target("swatch", f.at("list-item-x1")) == over("list-item-x1", DragOperation::copy) &&
              !f.target("file", f.at("list-item-x1")),
          "Collection rows must accept only their types");
    const auto list = f.box("list");
    check(!f.target("swatch", {list.origin.x + 2, list.origin.y + list.size.height + 1}) &&
              !f.target("swatch", f.at("plain")) && !f.target("swatch", {-10, -10}),
          "Clipped rows, non-targets and empty space must not accept");
    check(has(drop_target_at(f.hit(), f.scope, f.source("swatch"), {NAN, 0}).diagnostics, "invalid_position",
              "/position") &&
              has(drop_target_at(f.hit(), f.scope, DragSource{"s", "bad", copy}, {0, 0}).diagnostics,
                  "invalid_drag_type", "/source/type") &&
              has(drop_target_at(f.hit(), DragDropScope{{}, {{"", {color}, copy}}}, f.source("swatch"), {0, 0})
                      .diagnostics,
                  "invalid_identifier", "/targets/0/id"),
          "Invalid positions, sources and scopes must reject");
}

void click() {
    Fixture f;
    f.build();
    DragDropDispatcher drag;
    const auto swatch = f.at("swatch");
    auto result = take(drag.dispatch(f.hit(), f.scope, down(swatch)));
    check(!result.consumed && drag.pressed() && !drag.dragging() && !drag.source(),
          "A source press must be observed, not consumed");
    result = take(drag.dispatch(f.hit(), f.scope, move({swatch.x + 2, swatch.y + 2})));
    check(!result.consumed && !result.started && !result.target && !result.source,
          "Travel below the threshold must not drag");
    result = take(drag.dispatch(f.hit(), f.scope, up({swatch.x + 2, swatch.y + 2})));
    check(!result.consumed && !result.ended && !result.drop && !drag.pressed(),
          "A release below the threshold must stay an ordinary click");
    for (const auto point : {f.at("inner"), f.at("plain"), f.at("list-item-x0")}) {
        (void)take(drag.dispatch(f.hit(), f.scope, down(point)));
        check(!drag.pressed(), "Presses outside every source must not start a press");
    }
    (void)take(drag.dispatch(f.hit(), f.scope, down(swatch, 1, PointerButton::secondary)));
    check(!drag.pressed(), "A secondary press must not start a press");
    f.swatch_disabled = true;
    f.build();
    (void)take(drag.dispatch(f.hit(), f.scope, down(swatch)));
    check(!drag.pressed(), "A disabled source must not start a press");
    (void)take(drag.dispatch(f.hit(), {}, down(f.at("file"))));
    check(!drag.pressed(), "A node outside the scope must not start a press");
}

void dragging() {
    Fixture f;
    f.build();
    DragDropDispatcher drag;
    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("file"))));
    auto result = take(drag.dispatch(f.hit(), f.scope, move(f.at("inner"))));
    check(result.consumed && result.started && result.source == f.source("file") && result.position == f.at("inner") &&
              result.target == over("inner", DragOperation::link) && drag.dragging() &&
              drag.source() == f.source("file"),
          "Crossing the threshold must start a drag and report the accepting target");
    result = take(drag.dispatch(f.hit(), f.scope, move(f.at("inner"), {.control = true})));
    check(result.consumed && !result.started && result.target == over("panel", DragOperation::copy),
          "Changing modifiers on a move must change the operation and target");
    check(!take(drag.dispatch(f.hit(), f.scope, move(f.at("plain"), {}, 2))).consumed &&
              !take(drag.dispatch(f.hit(), f.scope, down(f.at("plain"), 2))).consumed &&
              !take(drag.dispatch(f.hit(), f.scope, down(f.at("plain"), 1, PointerButton::secondary))).consumed &&
              !take(drag.dispatch(f.hit(), f.scope, event(KeyUp{Key::a}))).consumed &&
              !take(drag.dispatch(f.hit(), f.scope, event(Scroll{f.at("list"), {0, 4}}))).consumed && drag.dragging(),
          "Other pointers, secondary buttons, key releases and wheel input must pass through");
    result = take(drag.dispatch(f.hit(), f.scope, move(f.at("plain"))));
    check(result.consumed && !result.target && result.source, "Leaving every target must clear the target");
    result = take(drag.dispatch(f.hit(), f.scope, up(f.at("panel", .5f, .9f), {.control = true})));
    check(result.consumed && result.ended && !result.canceled && result.drop == over("panel", DragOperation::copy) &&
              !result.target && !result.position && result.source == f.source("file") && !drag.pressed(),
          "The release must drop on its own target with its own modifiers");

    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    (void)take(drag.dispatch(f.hit(), f.scope, move(f.at("list-item-x0"))));
    result = take(drag.dispatch(f.hit(), f.scope, up(f.at("plain"))));
    check(result.consumed && result.ended && result.canceled && !result.drop,
          "Releasing where nothing accepts must cancel");

    DragDropDispatcher still({.threshold = 1000});
    (void)take(still.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    check(!take(still.dispatch(f.hit(), f.scope, move(f.at("plain")))).consumed,
          "Travel below a configured threshold must not drag");
}

void cancellation() {
    Fixture f;
    f.build();
    DragDropDispatcher drag;
    const auto start = [&] {
        (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
        (void)take(drag.dispatch(f.hit(), f.scope, move(f.at("panel", .5f, .9f))));
    };
    for (auto cancel : {event(KeyDown{Key::escape}), event(Cancel{})}) {
        start();
        const auto result = take(drag.dispatch(f.hit(), f.scope, cancel));
        check(result.consumed && result.ended && result.canceled && !result.drop &&
                  result.source == f.source("swatch") && !drag.pressed(),
              "Escape and Cancel must cancel a drag");
        check(!take(drag.dispatch(f.hit(), f.scope, up(f.at("panel")))).consumed,
              "The release after cancellation must pass through");
    }
    start();
    check(!take(drag.dispatch(f.hit(), f.scope, event(KeyDown{Key::escape, {.shift = true}}))).consumed &&
              !drag.pressed(),
          "Modified Escape must cancel unconsumed");
    start();
    check(!take(drag.dispatch(f.hit(), f.scope, event(PointerCancel{{2}}))).consumed && drag.dragging(),
          "Another pointer's cancellation must not end the drag");
    auto result = take(drag.dispatch(f.hit(), f.scope, event(PointerCancel{{1}})));
    check(result.consumed && result.canceled && result.source && !drag.pressed(), "PointerCancel must cancel");
    for (auto interruption : {event(KeyDown{Key::tab}), event(tessera::TextInput{"a"}), event(Navigate{Direction::up}),
                              event(FocusNext{}), event(FocusPrevious{}), event(Activate{})}) {
        start();
        result = take(drag.dispatch(f.hit(), f.scope, interruption));
        check(!result.consumed && result.ended && result.canceled && !drag.pressed(),
              "Other keyboard, text and logical input must cancel unconsumed");
    }
    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    result = take(drag.dispatch(f.hit(), f.scope, event(KeyDown{Key::escape})));
    check(!result.consumed && !result.ended && !result.source && !drag.pressed(),
          "Escape before dragging must stay with the press");
}

void replacement() {
    Fixture f;
    f.build();
    DragDropDispatcher drag;
    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    const auto position = f.at("list-item-x1", .5f, .2f);
    (void)take(drag.dispatch(f.hit(), f.scope, move(position)));
    f.scroll = f.box("list-item-x1").origin.y - f.box("list-item-x0").origin.y;
    f.build();
    auto result = take(drag.refresh(f.hit(), f.scope));
    check(!result.ended && result.source == f.source("swatch") && result.position == position &&
              result.target == over("list-item-x2", DragOperation::copy),
          "Refresh must recompute the target at the last position after scrolling");
    f.scope.targets.erase(f.scope.targets.begin() + 6);
    result = take(drag.refresh(f.hit(), f.scope));
    check(!result.ended && !result.target, "A target leaving the scope must stop accepting");
    auto narrowed = f.scope;
    narrowed.sources[0].operations = {.move = true, .copy = true};
    result = take(drag.refresh(f.hit(), narrowed));
    check(result.ended && result.canceled && result.source == f.source("swatch") && !drag.pressed(),
          "A changed source entry must cancel the drag");

    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    (void)take(drag.dispatch(f.hit(), f.scope, move(f.at("plain"))));
    f.swatch_disabled = true;
    f.build();
    result = take(drag.dispatch(f.hit(), f.scope, up(f.at("panel", .5f, .9f))));
    check(result.ended && result.canceled && !result.consumed && !result.drop,
          "Disabling the source must cancel before routing the release");

    f.swatch_disabled = false;
    f.build();
    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    result = take(drag.refresh(f.hit(), {}));
    check(!result.ended && !result.source && !drag.pressed(),
          "A pressed source leaving the scope must end the press without a drag");
}

void rejection() {
    Fixture f;
    f.build();
    DragDropDispatcher negative({.threshold = -1});
    check(has(negative.dispatch(f.hit(), f.scope, down(f.at("swatch"))).diagnostics, "drag_drop_options", "/options") &&
              !negative.pressed() &&
              has(negative.refresh(f.hit(), f.scope).diagnostics, "drag_drop_options", "/options"),
          "Invalid options must reject");
    DragDropDispatcher drag;
    const DragDropScope invalid{{{"swatch", "bad", copy}}, {}};
    check(has(drag.dispatch(f.hit(), invalid, down(f.at("swatch"))).diagnostics, "invalid_drag_type",
              "/sources/0/type") &&
              !drag.pressed(),
          "An invalid scope must reject without a press");
    check(!drag.dispatch(f.hit(), f.scope, event(PointerDown{{1}, {NAN, 0}})) && !drag.pressed(),
          "Invalid input must reject without a press");
    check(!drag.dispatch({}, f.scope, down({0, 0})) && !drag.refresh({}, f.scope), "A missing snapshot must reject");
    (void)take(drag.dispatch(f.hit(), f.scope, down(f.at("swatch"))));
    (void)take(drag.dispatch(f.hit(), f.scope, move(f.at("plain"))));
    check(!drag.dispatch(f.hit(), invalid, up(f.at("plain"))) && drag.dragging(),
          "A rejected call must leave the drag unchanged");
}

} // namespace

int main() {
    try {
        operations();
        validation();
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
