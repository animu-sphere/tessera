#include <tessera/input/pointer.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/targeting.hpp"
#include <type_traits>
#include <utility>

namespace tessera {
namespace {

bool is_pointer(const InputEvent& event) {
    return std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
           std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data);
}

} // namespace

PointerDispatchResult PointerDispatcher::snapshot() const {
    PointerDispatchResult result;
    result.pointers.reserve(pointers_.size());
    for (const auto& [id, pointer] : pointers_) {
        (void)id;
        result.pointers.push_back(pointer.state);
    }
    return result;
}

void PointerDispatcher::reconcile(const HitTestInput& input, const detail::Targeting& targeting) {
    const bool replacement = tree_ != input.tree->root().tree;
    if (replacement && policy_ == PointerSnapshotPolicy::tree_identity) pointers_.clear();
    tree_ = input.tree->root().tree;
    for (auto& [id, stored] : pointers_) {
        (void)id;
        auto& pointer = stored.state;
        if (replacement) {
            pointer.hovered.reset();
            pointer.active.reset();
            pointer.pressed = stored.press && stored.press->id ? input.tree->find(*stored.press->id) : std::nullopt;
        }
        if (policy_ == PointerSnapshotPolicy::compatible_author_ids && pointer.pressed) {
            const auto* node = input.tree->get(*pointer.pressed);
            if (!stored.press || !node || node->kind != stored.press->kind || !node->events.contains("activate") ||
                node->events.at("activate") != stored.press->action || !targeting.pointer_eligible(*pointer.pressed))
                pointer.pressed.reset();
        }
        targeting.refresh(pointer);
        if (!pointer.pressed) stored.press.reset();
    }
}

std::vector<Diagnostic> validate(const HitTestInput& input) {
    return detail::validate_layout_snapshot(input.tree, input.styles, input.layout);
}

Result<HitTestResult> hit_test(const HitTestInput& input, Point position) {
    auto errors = validate(input);
    detail::Checker(errors).point(position, "/position");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {HitTestResult{detail::Targeting(input).hit(position)}, {}};
}

Result<PointerDispatchResult> PointerDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    detail::Checker(errors).enumeration(policy_, PointerSnapshotPolicy::compatible_author_ids, "/policy");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    reconcile(input, targeting);
    return {snapshot(), {}};
}

Result<PointerDispatchResult> PointerDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = validate(input);
    auto event_errors = validate(event);
    errors.insert(errors.end(), event_errors.begin(), event_errors.end());
    detail::Checker check(errors);
    check.enumeration(policy_, PointerSnapshotPolicy::compatible_author_ids, "/policy");
    if (!is_pointer(event))
        check.error("unsupported_event", "/event", "PointerDispatcher accepts only pointer move/down/up/cancel.");
    if (timestamp_ && event.timestamp < *timestamp_)
        check.error("event_order", "/timestamp", "Send non-decreasing timestamps within this dispatcher's stream.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const detail::Targeting targeting(input);
    reconcile(input, targeting);
    std::optional<NodeHandle> target;
    std::vector<ActionRequest> actions;
    std::visit(
        [&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, PointerCancel>) {
                pointers_.erase(data.pointer.value);
            } else if constexpr (std::is_same_v<T, PointerMove> || std::is_same_v<T, PointerDown> ||
                                 std::is_same_v<T, PointerUp>) {
                auto& stored = pointers_[data.pointer.value];
                auto& pointer = stored.state;
                pointer.pointer = data.pointer;
                pointer.position = data.position;
                targeting.refresh(pointer);
                target = pointer.hovered;
                if constexpr (std::is_same_v<T, PointerDown>) {
                    if (data.button == PointerButton::primary && !pointer.primary_down) {
                        pointer.primary_down = true;
                        pointer.pressed = targeting.binding_owner(target);
                        pointer.active = pointer.pressed;
                        if (policy_ == PointerSnapshotPolicy::compatible_author_ids && pointer.pressed) {
                            const auto& owner = *input.tree->get(*pointer.pressed);
                            stored.press = Press{owner.id, owner.events.at("activate"), owner.kind};
                        }
                    }
                } else if constexpr (std::is_same_v<T, PointerUp>) {
                    if (data.button == PointerButton::primary) {
                        if (pointer.primary_down && pointer.active) {
                            const auto owner = *pointer.active;
                            actions.push_back({"activate", input.tree->get(owner)->events.at("activate"), owner});
                        }
                        pointer.primary_down = false;
                        pointer.pressed.reset();
                        pointer.active.reset();
                        stored.press.reset();
                    }
                }
            }
        },
        event.data);
    timestamp_ = event.timestamp;
    auto result = snapshot();
    result.target = target;
    result.actions = std::move(actions);
    return {std::move(result), {}};
}

void PointerDispatcher::reset() noexcept {
    pointers_.clear();
    tree_ = 0;
    timestamp_.reset();
}

} // namespace tessera
