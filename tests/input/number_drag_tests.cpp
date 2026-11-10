#include <tessera/ui/authoring.hpp>
#include <tessera/input/number_drag.hpp>
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
const ValidationContext actions{{"before", "scale", "scale.other", "locked", "position.x", "position.y"}};
TextEditState draft(std::uint64_t owner, std::string text) {
    return take(make_text_edit_state(owner, std::move(text)));
}
// A NumberInput with step parts, a disabled NumberInput and a two-component VectorInput.
struct Fixture {
    ui::Session session;
    Signal<bool> disabled = session.state("disabled", false);
    std::string scale_action = "scale";
    bool scale_shown = true;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    ui::Element form() const {
        const auto width = Dimension::points(240);
        std::vector<ui::Element> children{ui::Button("Before", "before", {.node = {.id = "before"}})};
        if (scale_shown)
            children.push_back(
                ui::NumberInput("Scale", draft(1, "1.5"), scale_action,
                                {.node = {.id = "scale", .style = {.width = width, .padding = Edges{4, 4, 4, 4}}},
                                 .disabled = disabled}));
        children.push_back(
            ui::NumberInput("Locked", draft(2, "0"), "locked", {.node = {.id = "locked"}, .disabled = true}));
        children.push_back(ui::VectorInput(
            "Position", {{"x", "X", draft(3, "0"), "position.x"}, {"y", "Y", draft(4, "2"), "position.y"}},
            {.node = {.id = "position", .style = {.width = width}}}));
        return ui::Column(std::move(children), {.id = "root", .style = {.align = Align::start, .gap = 4.f}});
    }
    void build() {
        auto authored = take(session.lower(form(), actions));
        tree = take(UiTree::create(authored.document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, {320, 240}, &text, {}}));
        session.publish();
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox& box(std::string_view id) const {
        const auto target = node(id);
        return *std::find_if(layout.boxes.begin(), layout.boxes.end(),
                             [&](const LayoutBox& box) { return box.node == target; });
    }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
    std::optional<NodeHandle> target(Point point) const { return take(number_drag_target(hit(), point)); }
};
std::chrono::microseconds clock{0};
InputEvent event(decltype(InputEvent::data) data) { return {clock += 1ms, std::move(data)}; }
InputEvent down(Point point, std::uint32_t pointer = 1, PointerButton button = PointerButton::primary) {
    return event(PointerDown{{pointer}, point, button});
}
InputEvent move(Point point, std::uint32_t pointer = 1) { return event(PointerMove{{pointer}, point}); }
InputEvent up(Point point, std::uint32_t pointer = 1, PointerButton button = PointerButton::primary) {
    return event(PointerUp{{pointer}, point, button});
}
InputEvent key(Key key) { return event(KeyDown{key}); }
Point offset(Point point, float x, float y = 0) { return {point.x + x, point.y + y}; }

void targets() {
    Fixture f;
    f.build();
    const auto scale = f.node("scale");
    check(f.target(f.center("scale-label")) == scale && f.target(f.center("scale-field")) == scale &&
              f.target(f.center("scale-value")) == scale,
          "The label, field and draft Text must target their NumberInput");
    const auto& owner = f.box("scale").border_box;
    check(f.target({owner.origin.x + 1, owner.origin.y + 1}) == scale,
          "The owner's own padding must target the NumberInput");
    check(!f.target(f.center("scale-decrease")) && !f.target(f.center("scale-increase")),
          "Step parts must keep their own press behavior");
    check(!f.target(f.center("before")) && !f.target({310, 230}), "Other controls and misses must not target");
    check(!f.target(f.center("locked-field")), "A disabled NumberInput must not target");
    check(f.target(f.center("position-y-label")) == f.node("position-y") &&
              f.target(f.center("position-x-field")) == f.node("position-x"),
          "VectorInput components must target through their own label and field");
    check(!f.target(f.center("position-label")), "The VectorInput group label is not a component");
    const auto rejected = number_drag_target(f.hit(), {std::nanf(""), 0});
    check(!rejected && has(rejected.diagnostics, "invalid_position", "/position"), "A nonfinite position must reject");
}

void click() {
    Fixture f;
    f.build();
    NumberDragDispatcher drag;
    const auto start = f.center("scale-value");
    auto result = take(drag.dispatch(f.hit(), down(start)));
    check(result.consumed && result.focus == f.node("scale") && result.input == f.node("scale") && !result.started &&
              !result.action && drag.pressed() && !drag.scrubbing() && drag.captured() == "scale",
          "A press must capture and focus the input without a request");
    result = take(drag.dispatch(f.hit(), move(offset(start, 2.5f, 30))));
    check(result.consumed && !result.started && !result.action && !drag.scrubbing(),
          "Vertical travel and travel below the threshold must not scrub");
    result = take(drag.dispatch(f.hit(), up(offset(start, -2.5f))));
    check(result.consumed && result.ended && !result.started && !result.action && !drag.pressed(),
          "A release below the threshold must end as a click without a request");
    result = take(drag.dispatch(f.hit(), down(f.center("scale-increase"))));
    check(!result.consumed && !drag.pressed(), "A step-part press must pass to ordinary routing");
    result = take(drag.dispatch(f.hit(), down(f.center("scale-field"), 1, PointerButton::secondary)));
    check(!result.consumed && !drag.pressed(), "A secondary press must not capture");
}

void scrub() {
    Fixture f;
    f.build();
    NumberDragDispatcher drag;
    const auto start = f.center("scale-label");
    (void)take(drag.dispatch(f.hit(), down(start)));
    auto result = take(drag.dispatch(f.hit(), move(offset(start, 3))));
    check(result.consumed && result.started && !result.steps && !result.action && drag.scrubbing(),
          "Reaching the threshold must start a scrub without a zero-step request");
    result = take(drag.dispatch(f.hit(), move(offset(start, 9, 40))));
    check(!result.started && result.steps == 2 && result.action && result.action->action == "scale" &&
              result.action->binding == "activate" && result.action->target == f.node("scale"),
          "Moves must request whole steps through the input's command, beyond its box");
    result = take(drag.dispatch(f.hit(), move(offset(start, 11.9f))));
    check(result.consumed && !result.steps && !result.action, "An unchanged step count must not request");
    result = take(drag.dispatch(f.hit(), move(offset(start, -5))));
    check(result.steps == -1 && result.action, "Steps must truncate toward zero on either side");
    result = take(drag.dispatch(f.hit(), move(offset(start, -3.9f))));
    check(result.steps == 0 && result.action, "Returning to the origin must request zero steps");
    // Other pointers, key releases and wheel input pass through without ending capture.
    check(!take(drag.dispatch(f.hit(), move(offset(start, 40), 2))).consumed &&
              !take(drag.dispatch(f.hit(), down(f.center("before"), 2))).consumed &&
              !take(drag.dispatch(f.hit(), event(KeyUp{Key::a}))).consumed,
          "Unrelated input must pass through");
    check(!take(drag.dispatch(f.hit(), event(Scroll{start, {0, 10}}))).consumed && drag.scrubbing(),
          "Scroll must pass through without ending capture");
    result = take(drag.dispatch(f.hit(), up(offset(start, 17))));
    check(result.consumed && result.ended && result.steps == 4 && result.action && !drag.pressed(),
          "The release must request its own steps and end capture");

    // A release far from the press scrubs even without intermediate moves.
    (void)take(drag.dispatch(f.hit(), down(start)));
    result = take(drag.dispatch(f.hit(), up(offset(start, -8))));
    check(result.started && result.ended && result.steps == -2 && result.action,
          "A single fast release must start, request and end");

    // Larger custom steps with no threshold.
    NumberDragDispatcher coarse({.threshold = 0, .distance = 10});
    (void)take(coarse.dispatch(f.hit(), down(f.center("position-y-field"))));
    result = take(coarse.dispatch(f.hit(), move(offset(f.center("position-y-field"), 25))));
    check(result.started && result.steps == 2 && result.action->action == "position.y" &&
              result.action->target == f.node("position-y"),
          "Options must scale steps and VectorInput components must use their own command");
}

void cancellation() {
    Fixture f;
    f.build();
    NumberDragDispatcher drag;
    const auto start = f.center("scale-field");
    (void)take(drag.dispatch(f.hit(), down(start)));
    (void)take(drag.dispatch(f.hit(), move(offset(start, 12))));
    auto result = take(drag.dispatch(f.hit(), key(Key::escape)));
    check(result.consumed && result.canceled && result.ended && result.steps == 0 && result.action &&
              result.action->action == "scale" && !drag.pressed(),
          "Escape must restore the origin with a zero-step request");
    check(!take(drag.dispatch(f.hit(), up(offset(start, 30)))).consumed,
          "The release after cancellation must pass through");

    (void)take(drag.dispatch(f.hit(), down(start)));
    (void)take(drag.dispatch(f.hit(), move(offset(start, 3))));
    result = take(drag.dispatch(f.hit(), event(Cancel{})));
    check(result.consumed && result.canceled && result.ended && !result.action,
          "Logical Cancel at the origin must end without a request");

    (void)take(drag.dispatch(f.hit(), down(start)));
    result = take(drag.dispatch(f.hit(), key(Key::escape)));
    check(!result.consumed && result.ended && !result.canceled && !drag.pressed(),
          "Escape before scrubbing must end the press and stay with ordinary routing");

    (void)take(drag.dispatch(f.hit(), down(start)));
    (void)take(drag.dispatch(f.hit(), move(offset(start, 12))));
    result = take(drag.dispatch(f.hit(), event(PointerCancel{{2}})));
    check(!result.consumed && drag.scrubbing(), "Another pointer's cancellation must not end capture");
    result = take(drag.dispatch(f.hit(), event(PointerCancel{{1}})));
    check(result.consumed && result.ended && !result.canceled && !result.action && !drag.pressed(),
          "PointerCancel must end capture keeping the last request");

    for (auto interruption : {key(Key::tab), event(tessera::TextInput{"5"}), event(FocusNext{}),
                              event(Navigate{Direction::up}), event(Activate{})}) {
        (void)take(drag.dispatch(f.hit(), down(start)));
        (void)take(drag.dispatch(f.hit(), move(offset(start, 12))));
        result = take(drag.dispatch(f.hit(), interruption));
        check(!result.consumed && result.ended && !result.action && !drag.pressed(),
              "Keyboard, text and logical input must end capture unconsumed");
    }
}

void replacement() {
    Fixture f;
    f.build();
    NumberDragDispatcher drag;
    const auto start = f.center("scale-field");
    (void)take(drag.dispatch(f.hit(), down(start)));
    (void)take(drag.dispatch(f.hit(), move(offset(start, 8))));
    f.build();
    auto result = take(drag.refresh(f.hit()));
    check(!result.ended && result.input == f.node("scale") && drag.scrubbing(),
          "A compatible replacement must keep capture by author ID");
    result = take(drag.dispatch(f.hit(), move(offset(start, 12))));
    check(result.steps == 3 && result.action && result.action->target == f.node("scale"),
          "Steps must continue from the original press after replacement");

    f.session.update([&] { f.disabled.write(true); });
    f.build();
    result = take(drag.refresh(f.hit()));
    check(result.ended && !result.input && !drag.pressed(), "Disabling the input must end capture");
    f.session.update([&] { f.disabled.write(false); });
    f.build();

    (void)take(drag.dispatch(f.hit(), down(start)));
    f.scale_action = "scale.other";
    f.build();
    result = take(drag.dispatch(f.hit(), up(offset(start, 20))));
    check(result.ended && !result.consumed && !result.action && !drag.pressed(),
          "A changed command must end capture before routing the release");

    f.scale_action = "scale";
    f.build();
    (void)take(drag.dispatch(f.hit(), down(start)));
    f.scale_shown = false;
    f.build();
    check(take(drag.refresh(f.hit())).ended && !drag.pressed(), "Removing the input must end capture");
}

void rejection() {
    Fixture f;
    f.build();
    for (const auto options : {NumberDragOptions{-1, 4}, NumberDragOptions{3, 0}, NumberDragOptions{3, INFINITY}}) {
        NumberDragDispatcher drag(options);
        const auto dispatched = drag.dispatch(f.hit(), down(f.center("scale-field")));
        check(!dispatched && has(dispatched.diagnostics, "number_drag_options", "/options") && !drag.pressed(),
              "Invalid options must reject dispatch without capture");
        check(has(drag.refresh(f.hit()).diagnostics, "number_drag_options", "/options"),
              "Invalid options must reject refresh");
    }
    NumberDragDispatcher drag;
    const auto invalid = drag.dispatch(f.hit(), event(PointerDown{{1}, {std::nanf(""), 0}}));
    check(!invalid && !drag.pressed(), "Invalid input must reject without capture");
    check(!drag.dispatch({}, down(f.center("scale-field"))), "A missing snapshot must reject");
}

} // namespace

int main() {
    try {
        targets();
        click();
        scrub();
        cancellation();
        replacement();
        rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
