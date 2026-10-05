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

PointerDispatchResult snapshot(const std::map<std::uint32_t, PointerState>& pointers) {
    PointerDispatchResult result;
    result.pointers.reserve(pointers.size());
    for (const auto& [id, pointer] : pointers) {
        (void)id;
        result.pointers.push_back(pointer);
    }
    return result;
}

} // namespace

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
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    if (tree_ != input.tree->root().tree) pointers_.clear();
    tree_ = input.tree->root().tree;
    for (auto& [id, pointer] : pointers_) {
        (void)id;
        targeting.refresh(pointer);
    }
    return {snapshot(pointers_), {}};
}

Result<PointerDispatchResult> PointerDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = validate(input);
    auto event_errors = validate(event);
    errors.insert(errors.end(), event_errors.begin(), event_errors.end());
    detail::Checker check(errors);
    if (!is_pointer(event)) check.error("unsupported_event", "/event", "PointerDispatcher accepts only pointer move/down/up/cancel.");
    if (timestamp_ && event.timestamp < *timestamp_)
        check.error("event_order", "/timestamp", "Send non-decreasing timestamps within this dispatcher's stream.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const detail::Targeting targeting(input);
    if (tree_ != input.tree->root().tree) pointers_.clear();
    tree_ = input.tree->root().tree;
    for (auto& [id, pointer] : pointers_) {
        (void)id;
        targeting.refresh(pointer);
    }
    std::optional<NodeHandle> target;
    std::vector<ActionRequest> actions;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, PointerCancel>) {
            pointers_.erase(data.pointer.value);
        } else if constexpr (std::is_same_v<T, PointerMove> || std::is_same_v<T, PointerDown> || std::is_same_v<T, PointerUp>) {
            auto& pointer = pointers_[data.pointer.value];
            pointer.pointer = data.pointer;
            pointer.position = data.position;
            targeting.refresh(pointer);
            target = pointer.hovered;
            if constexpr (std::is_same_v<T, PointerDown>) {
                if (data.button == PointerButton::primary && !pointer.primary_down) {
                    pointer.primary_down = true;
                    pointer.pressed = targeting.binding_owner(target);
                    pointer.active = pointer.pressed;
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
                }
            }
        }
    }, event.data);
    timestamp_ = event.timestamp;
    auto result = snapshot(pointers_);
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
