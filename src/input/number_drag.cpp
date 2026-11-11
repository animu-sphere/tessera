#include <tessera/input/number_drag.hpp>
#include "../detail/targeting.hpp"
#include <tessera/semantics/semantics.hpp>
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {

bool number_input(const UiNode& node) {
    const auto* role = effective_property(node, property_names::edit_role);
    return role && std::holds_alternative<std::string>(*role) && std::get<std::string>(*role) == "number_input";
}

// An eligible, focusable number_input with a stable ID and an activate command.
bool usable(const HitTestInput& input, const detail::Targeting& targeting, NodeHandle owner) {
    const auto* node = input.tree->get(owner);
    const auto box = targeting.box(owner);
    return node && number_input(*node) && node->id && !node->id->empty() && node->events.contains("activate") &&
           box != no_layout_parent && targeting.focusable(box);
}

std::optional<NodeHandle> target_at(const HitTestInput& input, const detail::Targeting& targeting, Point position) {
    const auto hit = targeting.hit(position);
    if (!hit) return {};
    const auto& boxes = input.layout->boxes;
    // Walk toward the root, remembering the owner's child that holds the hit.
    std::optional<NodeHandle> part;
    for (auto at = targeting.box(*hit); at != no_layout_parent; at = boxes[at].parent) {
        const auto node = boxes[at].node;
        if (!number_input(*input.tree->get(node))) {
            part = node;
            continue;
        }
        if (!usable(input, targeting, node)) return {};
        if (!part) return node;
        const auto& id = *input.tree->get(node)->id;
        const auto& child = input.tree->get(*part)->id;
        return child && (*child == id + "-label" || *child == id + "-field") ? std::optional{node} : std::nullopt;
    }
    return {};
}

bool escape(const InputEvent& event) {
    if (std::holds_alternative<Cancel>(event.data)) return true;
    const auto* key = std::get_if<KeyDown>(&event.data);
    return key && key->key == Key::escape && key->modifiers == Modifiers{};
}

// Keyboard, text and logical events end capture; KeyUp and Scroll do not.
bool interrupts(const InputEvent& event) {
    return std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<TextInput>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<FocusNext>(event.data) ||
           std::holds_alternative<FocusPrevious>(event.data) || std::holds_alternative<Activate>(event.data);
}

std::vector<Diagnostic> option_errors(const NumberDragOptions& options) {
    if (std::isfinite(options.threshold) && options.threshold >= 0 && std::isfinite(options.distance) &&
        options.distance > 0)
        return {};
    return {{"number_drag_options", Severity::error, "/options",
             "Supply a finite nonnegative threshold and a finite positive distance per step."}};
}

} // namespace

Result<std::optional<NodeHandle>> number_drag_target(const HitTestInput& input, Point position) {
    auto errors = validate(input);
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        errors.push_back({"invalid_position", Severity::error, "/position", "Supply a finite root-logical position."});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    return {target_at(input, targeting, position), {}};
}

std::optional<NodeHandle> NumberDragDispatcher::reconcile(const HitTestInput& input, NumberDragDispatchResult& result) {
    if (!drag_) return {};
    const detail::Targeting targeting(input);
    const auto owner = input.tree->find(drag_->id);
    if (owner && usable(input, targeting, *owner) && input.tree->get(*owner)->events.at("activate") == drag_->action)
        return owner;
    reset();
    result.ended = true;
    return {};
}

Result<NumberDragDispatchResult> NumberDragDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    auto options = option_errors(options_);
    errors.insert(errors.end(), options.begin(), options.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    NumberDragDispatchResult result;
    result.input = reconcile(input, result);
    return {result, {}};
}

Result<NumberDragDispatchResult> NumberDragDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = validate(input);
    auto next = validate(event);
    errors.insert(errors.end(), next.begin(), next.end());
    auto options = option_errors(options_);
    errors.insert(errors.end(), options.begin(), options.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    // Work on a copy so a rejected request leaves the capture unchanged.
    auto candidate = *this;
    NumberDragDispatchResult result;
    const auto owner = candidate.reconcile(input, result);
    // Requests the steps reached at `x` once the press has travelled far enough to scrub.
    const auto scrub = [&](float x) -> Result<NumberDragDispatchResult> {
        auto& drag = *candidate.drag_;
        const double offset = static_cast<double>(x) - drag.origin;
        if (!drag.scrubbing && std::abs(offset) < options_.threshold) return {result, {}};
        result.started = !drag.scrubbing;
        drag.scrubbing = true;
        const auto steps = static_cast<int>(std::clamp(std::trunc(offset / options_.distance), -1e9, 1e9));
        if (steps != drag.steps) {
            auto action = request_semantic_action({input.tree, input.styles, input.layout}, *owner, "activate");
            if (!action) return {std::nullopt, std::move(action.diagnostics)};
            result.steps = steps;
            result.action = std::move(*action.value);
            drag.steps = steps;
        }
        return {result, {}};
    };
    const auto finish = [&](Result<NumberDragDispatchResult> outcome) {
        if (outcome) *this = std::move(candidate);
        return outcome;
    };
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        if (candidate.drag_ || press->button != PointerButton::primary) return finish({result, {}});
        const detail::Targeting targeting(input);
        const auto target = target_at(input, targeting, press->position);
        if (!target) return finish({result, {}});
        const auto* node = input.tree->get(*target);
        candidate.drag_ = Drag{press->pointer, *node->id, node->events.at("activate"), press->position.x};
        result.consumed = true;
        result.input = result.focus = target;
        return finish({result, {}});
    }
    if (!owner) return finish({result, {}});
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (move->pointer != candidate.drag_->pointer) return finish({result, {}});
        result.consumed = true;
        result.input = owner;
        return finish(scrub(move->position.x));
    }
    if (const auto* release = std::get_if<PointerUp>(&event.data)) {
        if (release->pointer != candidate.drag_->pointer || release->button != PointerButton::primary)
            return finish({result, {}});
        result.consumed = result.ended = true;
        result.input = owner;
        auto outcome = scrub(release->position.x);
        candidate.reset();
        return finish(std::move(outcome));
    }
    if (const auto* lost = std::get_if<PointerCancel>(&event.data)) {
        if (lost->pointer != candidate.drag_->pointer) return finish({result, {}});
        result.consumed = result.ended = true;
        result.input = owner;
        candidate.reset();
        return finish({result, {}});
    }
    if (escape(event) && candidate.drag_->scrubbing) {
        result.consumed = result.ended = result.canceled = true;
        result.input = owner;
        if (candidate.drag_->steps != 0) {
            auto action = request_semantic_action({input.tree, input.styles, input.layout}, *owner, "activate");
            if (!action) return {std::nullopt, std::move(action.diagnostics)};
            result.steps = 0;
            result.action = std::move(*action.value);
        }
        candidate.reset();
        return finish({result, {}});
    }
    // A pending press, or a scrub interrupted by other keyboard or logical input, ends unconsumed.
    if (escape(event) || interrupts(event)) {
        result.ended = true;
        result.input = owner;
        candidate.reset();
    }
    return finish({result, {}});
}

} // namespace tessera
