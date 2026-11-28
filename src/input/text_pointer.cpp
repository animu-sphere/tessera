#include <tessera/input/text_pointer.hpp>
#include "../detail/targeting.hpp"
#include <tessera/semantics/semantics.hpp>
#include <cmath>

namespace tessera {
namespace {

bool text_field(const UiNode& node) {
    const auto* role = effective_property(node, property_names::edit_role);
    if (!role || !std::holds_alternative<std::string>(*role)) return false;
    const auto& name = std::get<std::string>(*role);
    return name == "text_input" || name == "text_editor";
}

// An eligible, focusable text field with a stable ID, an activate command and its value part.
bool usable(const HitTestInput& input, const detail::Targeting& targeting, NodeHandle owner) {
    const auto* node = input.tree->get(owner);
    const auto box = targeting.box(owner);
    return node && text_field(*node) && node->id && !node->id->empty() && node->events.contains("activate") &&
           input.tree->find(*node->id + "-value") && box != no_layout_parent && targeting.focusable(box);
}

std::optional<NodeHandle> target_at(const HitTestInput& input, const detail::Targeting& targeting, Point position) {
    const auto hit = targeting.hit(position);
    if (!hit) return {};
    const auto& boxes = input.layout->boxes;
    // Walk toward the root, remembering the owner's child that holds the hit.
    std::optional<NodeHandle> part;
    for (auto at = targeting.box(*hit); at != no_layout_parent; at = boxes[at].parent) {
        const auto node = boxes[at].node;
        if (!text_field(*input.tree->get(node))) {
            part = node;
            continue;
        }
        if (!usable(input, targeting, node)) return {};
        if (!part) return node;
        const auto& child = input.tree->get(*part)->id;
        return child && *child == *input.tree->get(node)->id + "-label" ? std::nullopt : std::optional{node};
    }
    return {};
}

// Keyboard, text and logical events end capture; KeyUp and Scroll do not.
bool interrupts(const InputEvent& event) {
    return std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<TextInput>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<FocusNext>(event.data) ||
           std::holds_alternative<FocusPrevious>(event.data) || std::holds_alternative<Activate>(event.data) ||
           std::holds_alternative<Cancel>(event.data);
}

} // namespace

Result<std::optional<NodeHandle>> text_pointer_target(const HitTestInput& input, Point position) {
    auto errors = validate(input);
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        errors.push_back({"invalid_position", Severity::error, "/position", "Supply a finite root-logical position."});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    return {target_at(input, targeting, position), {}};
}

std::optional<NodeHandle> TextPointerDispatcher::reconcile(const HitTestInput& input,
                                                           TextPointerDispatchResult& result) {
    if (!drag_) return {};
    const detail::Targeting targeting(input);
    const auto owner = input.tree->find(drag_->id);
    if (owner && usable(input, targeting, *owner) && input.tree->get(*owner)->events.at("activate") == drag_->action)
        return owner;
    reset();
    result.ended = true;
    return {};
}

Result<TextPointerDispatchResult> TextPointerDispatcher::refresh(const HitTestInput& input) {
    if (auto errors = validate(input); !errors.empty()) return {std::nullopt, std::move(errors)};
    TextPointerDispatchResult result;
    result.input = reconcile(input, result);
    return {result, {}};
}

Result<TextPointerDispatchResult> TextPointerDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = validate(input);
    auto next = validate(event);
    errors.insert(errors.end(), next.begin(), next.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    TextPointerDispatchResult result;
    const auto owner = reconcile(input, result);
    const auto report = [&](NodeHandle field, Point point, bool extend) {
        result.consumed = true;
        result.input = field;
        result.value = input.tree->find(*input.tree->get(field)->id + "-value");
        result.point = point;
        result.extend = extend;
    };
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        if (drag_ || press->button != PointerButton::primary) return {result, {}};
        const detail::Targeting targeting(input);
        const auto target = target_at(input, targeting, press->position);
        if (!target) return {result, {}};
        const auto* node = input.tree->get(*target);
        drag_ = Drag{press->pointer, *node->id, node->events.at("activate")};
        report(*target, press->position, press->modifiers.shift);
        result.focus = target;
        return {result, {}};
    }
    if (!owner) return {result, {}};
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (move->pointer == drag_->pointer) report(*owner, move->position, true);
        return {result, {}};
    }
    if (const auto* release = std::get_if<PointerUp>(&event.data)) {
        if (release->pointer != drag_->pointer || release->button != PointerButton::primary) return {result, {}};
        report(*owner, release->position, true);
        result.ended = true;
        reset();
        return {result, {}};
    }
    if (const auto* lost = std::get_if<PointerCancel>(&event.data)) {
        if (lost->pointer != drag_->pointer) return {result, {}};
        result.consumed = result.ended = true;
        result.input = owner;
        reset();
        return {result, {}};
    }
    if (interrupts(event)) {
        result.ended = true;
        result.input = owner;
        reset();
    }
    return {result, {}};
}

} // namespace tessera
