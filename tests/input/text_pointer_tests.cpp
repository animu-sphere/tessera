#include <tessera/ui/authoring.hpp>
#include <tessera/input/text_pointer.hpp>
#include <tessera/text/text_caret.hpp>
#include "../check.hpp"
#include <algorithm>
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
const ValidationContext actions{{"before", "name", "notes", "notes.other", "locked", "scale"}};
// A TextInput, a TextEditor, a disabled TextEditor and a NumberInput.
struct Fixture {
    ui::Session session;
    TextEditState notes = take(make_text_edit_state(2, "abcdef\nxy", TextLines::multiple));
    std::string notes_action = "notes";
    bool notes_shown = true;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    ui::Element form() const {
        const auto width = Dimension::points(240);
        std::vector<ui::Element> children{
            ui::Button("Before", "before", {.node = {.id = "before"}}),
            ui::TextInput("Name", take(make_text_edit_state(1, "Cube")), "name", {.node = {.id = "name"}})};
        if (notes_shown)
            children.push_back(
                ui::TextEditor("Notes", notes, notes_action,
                               {.node = {.id = "notes", .style = {.width = width, .padding = Edges{4, 4, 4, 4}}}}));
        children.push_back(ui::TextEditor("Locked", take(make_text_edit_state(3, "x", TextLines::multiple)), "locked",
                                          {.node = {.id = "locked"}, .disabled = true}));
        children.push_back(
            ui::NumberInput("Scale", take(make_text_edit_state(4, "1")), "scale", {.node = {.id = "scale"}}));
        return ui::Column(std::move(children), {.id = "root", .style = {.align = Align::start, .gap = 4.f}});
    }
    void build() {
        auto authored = take(session.lower(form(), actions));
        tree = take(UiTree::create(authored.document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, {320, 400}, &text, {}}));
        session.publish();
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox& box(NodeHandle target) const {
        return *std::find_if(layout.boxes.begin(), layout.boxes.end(),
                             [&](const LayoutBox& box) { return box.node == target; });
    }
    const LayoutBox& box(std::string_view id) const { return box(node(id)); }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
    std::optional<NodeHandle> target(Point point) const { return take(text_pointer_target(hit(), point)); }
    // The host mapping: the caret offset `point` reaches in the value Text's presented rows.
    std::size_t offset(NodeHandle value, Point point) {
        const auto content = box(value).content_box();
        const auto& shown = std::get<std::string>(tree->get(value)->properties.at("text"));
        const auto carets = take(layout_text_carets(text, shown, styles[value.index].text, {content.size.width}));
        return carets.offset_at({point.x - content.origin.x, point.y - content.origin.y});
    }
    // A point on the notes value's row `row`, `column` placeholder cells from its left.
    Point cell(std::size_t row, float column) const {
        const auto content = box("notes-value").content_box();
        const auto& style = styles[node("notes-value").index].text;
        return {content.origin.x + column * style.size / 2,
                content.origin.y + (static_cast<float>(row) + .5f) * style.size * 1.25f};
    }
};
std::chrono::microseconds clock{0};
InputEvent event(decltype(InputEvent::data) data) { return {clock += 1ms, std::move(data)}; }
InputEvent down(Point point, Modifiers modifiers = {}, std::uint32_t pointer = 1,
                PointerButton button = PointerButton::primary) {
    return event(PointerDown{{pointer}, point, button, modifiers});
}
InputEvent move(Point point, std::uint32_t pointer = 1) { return event(PointerMove{{pointer}, point}); }
InputEvent up(Point point, std::uint32_t pointer = 1, PointerButton button = PointerButton::primary) {
    return event(PointerUp{{pointer}, point, button});
}

void targets() {
    Fixture f;
    f.build();
    const auto notes = f.node("notes");
    check(f.target(f.center("notes-value")) == notes && f.target(f.center("name-value")) == f.node("name"),
          "Value Text must target its TextEditor or TextInput");
    const auto& owner = f.box("notes").border_box;
    const auto& status = f.box(f.tree->children(notes).back()).border_box;
    check(f.target({owner.origin.x + 1, owner.origin.y + 1}) == notes &&
              f.target({status.origin.x + 1, status.origin.y + status.size.height / 2}) == notes,
          "The owner's padding and selection status must target the editor");
    check(!f.target(f.center("notes-label")), "The label keeps ordinary focusing");
    check(!f.target(f.center("locked-value")), "A disabled editor must not target");
    check(!f.target(f.center("scale-value")) && !f.target(f.center("before")) && !f.target({318, 398}),
          "NumberInput, other controls and misses must not target");
    const auto rejected = text_pointer_target(f.hit(), {std::nanf(""), 0});
    check(!rejected && has(rejected.diagnostics, "invalid_position", "/position"), "A nonfinite position must reject");
}

void press_and_drag() {
    Fixture f;
    f.build();
    TextPointerDispatcher pointer;
    const auto notes = f.node("notes"), value = f.node("notes-value");
    auto result = take(pointer.dispatch(f.hit(), down(f.cell(0, 2.2f))));
    check(result.consumed && result.focus == notes && result.input == notes && result.value == value &&
              result.point == f.cell(0, 2.2f) && !result.extend && !result.ended && pointer.captured() == "notes",
          "A press must capture, focus and place the caret");
    check(f.offset(value, *result.point) == 2, "The host maps the press to the nearest stop");
    result = take(pointer.dispatch(f.hit(), move(f.cell(1, 1.4f))));
    check(result.consumed && result.extend && !result.focus && f.offset(value, *result.point) == 8,
          "A move must extend the selection to the row under the pointer");
    result = take(pointer.dispatch(f.hit(), move({-50, -50})));
    check(result.consumed && result.extend && f.offset(value, *result.point) == 0,
          "Positions outside the field still report and clamp to its first row");
    result = take(pointer.dispatch(f.hit(), move(f.cell(0, 4), 2)));
    check(!result.consumed && !result.point, "Another pointer passes through");
    result = take(pointer.dispatch(f.hit(), event(Scroll{f.cell(0, 4), {0, 5}})));
    check(!result.consumed && pointer.pressed(), "Scroll passes through and keeps capture");
    result = take(pointer.dispatch(f.hit(), up(f.cell(5, 99))));
    check(result.consumed && result.ended && result.extend && f.offset(value, *result.point) == 9 && !pointer.pressed(),
          "Release extends a final time and ends capture");
    result = take(pointer.dispatch(f.hit(), move(f.cell(0, 1))));
    check(!result.consumed && !result.point, "Moves after release pass through");

    result = take(pointer.dispatch(f.hit(), down(f.cell(0, 3), {.shift = true})));
    check(result.consumed && result.extend && f.offset(value, *result.point) == 3,
          "Shift+press extends from the observed anchor");
    result = take(pointer.dispatch(f.hit(), down(f.cell(0, 1), {}, 2)));
    check(!result.consumed, "A second press while captured passes through");
    result = take(pointer.dispatch(f.hit(), event(PointerCancel{{1}})));
    check(result.consumed && result.ended && !result.point && !pointer.pressed(), "PointerCancel ends capture");

    result = take(pointer.dispatch(f.hit(), down(f.center("notes-value"), {}, 1, PointerButton::secondary)));
    check(!result.consumed && !pointer.pressed(), "A secondary press must not capture");
    (void)take(pointer.dispatch(f.hit(), down(f.center("name-value"))));
    result = take(pointer.dispatch(f.hit(), up(f.center("name-value"), 1, PointerButton::secondary)));
    check(!result.consumed && pointer.pressed(), "A secondary release keeps the primary capture");
    result = take(pointer.dispatch(f.hit(), event(KeyDown{Key::a})));
    check(!result.consumed && result.ended && result.input == f.node("name") && !pointer.pressed(),
          "Keyboard input ends capture unconsumed");
    (void)take(pointer.dispatch(f.hit(), down(f.center("name-value"))));
    result = take(pointer.dispatch(f.hit(), event(Cancel{})));
    check(!result.consumed && result.ended && !pointer.pressed(), "Logical Cancel ends capture unconsumed");
    (void)take(pointer.dispatch(f.hit(), down(f.center("name-value"))));
    result = take(pointer.dispatch(f.hit(), event(KeyUp{Key::a})));
    check(!result.consumed && !result.ended && pointer.pressed(), "KeyUp keeps capture");
}

void replacement() {
    Fixture f;
    f.build();
    TextPointerDispatcher pointer;
    (void)take(pointer.dispatch(f.hit(), down(f.cell(0, 1))));
    f.notes = take(edit_text(f.notes, {2, 1, TextEditSelection{1, 1}}));
    f.build();
    auto result = take(pointer.refresh(f.hit()));
    check(!result.ended && result.input == f.node("notes") && pointer.pressed(), "An edited editor keeps capture");
    result = take(pointer.dispatch(f.hit(), move(f.cell(0, 4))));
    check(result.consumed && f.offset(*result.value, *result.point) == 4, "Capture continues in the new snapshot");
    f.notes_action = "notes.other";
    f.build();
    result = take(pointer.refresh(f.hit()));
    check(result.ended && !result.input && !pointer.pressed(), "A changed command ends capture");
    f.notes_action = "notes";
    f.build();
    (void)take(pointer.dispatch(f.hit(), down(f.cell(0, 1))));
    const auto later = f.cell(0, 4); // Taken while the notes still exist.
    f.notes_shown = false;
    f.build();
    result = take(pointer.dispatch(f.hit(), move(later)));
    check(result.ended && !result.consumed && !pointer.pressed(), "A removed editor ends capture at dispatch");
    (void)take(pointer.dispatch(f.hit(), down(f.center("name-value"))));
    pointer.reset();
    check(!pointer.pressed() && !take(pointer.dispatch(f.hit(), up(f.center("name-value")))).consumed,
          "reset ends capture without a report");
    const auto invalid = pointer.dispatch({}, move({0, 0}));
    check(!invalid && !invalid.diagnostics.empty(), "Invalid snapshots reject");
}
} // namespace

int main() {
    try {
        targets();
        press_and_drag();
        replacement();
        std::cout << "Text pointer targeting, caret placement, drag selection and replacement checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
