#include <tessera/input/pointer.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include <type_traits>
#include <utility>

namespace tessera {
namespace {

bool disabled(const UiNode& node) {
    const auto found = node.properties.find("disabled");
    return found != node.properties.end() && std::get<bool>(found->second);
}

// One validated snapshot, independent of paint generation or renderer state.
class Targeting {
public:
    explicit Targeting(const HitTestInput& input) : input_(input), by_node_(input.tree->size(), no_layout_parent) {
        const auto& boxes = input.layout->boxes;
        disabled_.reserve(boxes.size());
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto& box = boxes[i];
            by_node_[box.node.index] = static_cast<std::uint32_t>(i);
            disabled_.push_back(disabled(*input.tree->get(box.node)) ||
                                (box.parent != no_layout_parent && disabled_[box.parent]));
        }
    }

    std::optional<NodeHandle> hit(Point point) const {
        const auto& boxes = input_.layout->boxes;
        for (std::size_t i = boxes.size(); i > 0; --i) {
            const auto& box = boxes[i - 1];
            if (!eligible(i - 1)) continue;
            // Double subtraction avoids float overflow at a finite box's far edge.
            const double x = static_cast<double>(point.x) - box.border_box.origin.x;
            const double y = static_cast<double>(point.y) - box.border_box.origin.y;
            if (x >= 0 && x < box.border_box.size.width && y >= 0 && y < box.border_box.size.height)
                return box.node;
        }
        return {};
    }

    bool eligible(NodeHandle node) const {
        return input_.tree->get(node) && by_node_[node.index] != no_layout_parent && eligible(by_node_[node.index]);
    }

    std::optional<NodeHandle> binding_owner(std::optional<NodeHandle> target) const {
        if (!target) return {};
        auto index = by_node_[target->index];
        while (index != no_layout_parent) {
            const auto& box = input_.layout->boxes[index];
            if (eligible(index) && input_.tree->get(box.node)->events.contains("activate")) return box.node;
            index = box.parent;
        }
        return {};
    }

    void refresh(PointerState& pointer) const {
        pointer.hovered = hit(pointer.position);
        if (pointer.pressed && !eligible(*pointer.pressed)) pointer.pressed.reset();
        pointer.active = pointer.pressed && pointer.pressed == binding_owner(pointer.hovered) ? pointer.pressed :
                                                                                              std::nullopt;
    }

private:
    bool eligible(std::size_t index) const { return input_.layout->boxes[index].visible && !disabled_[index]; }
    const HitTestInput& input_;
    std::vector<std::uint32_t> by_node_;
    std::vector<bool> disabled_;
};

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
    return {HitTestResult{Targeting(input).hit(position)}, {}};
}

Result<PointerDispatchResult> PointerDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const Targeting targeting(input);
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

    const Targeting targeting(input);
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
