#include <tessera/input/focus.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/targeting.hpp"
#include "../detail/collection.hpp"
#include <algorithm>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {

bool is_command(const InputEvent& event) {
    if (const auto* key = std::get_if<KeyDown>(&event.data))
        return (key->key == Key::home || key->key == Key::end) &&
            !key->modifiers.shift && !key->modifiers.control && !key->modifiers.alt && !key->modifiers.super;
    return std::holds_alternative<FocusNext>(event.data) || std::holds_alternative<FocusPrevious>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<Activate>(event.data) ||
           std::holds_alternative<Cancel>(event.data);
}

std::uint32_t collection_owner(const HitTestInput& input, std::uint32_t box) {
    while (box != no_layout_parent) {
        const auto role = detail::collection_role(*input.tree->get(input.layout->boxes[box].node));
        if (role == "tree" || role == "list" || role == "menu") return box;
        box = input.layout->boxes[box].parent;
    }
    return no_layout_parent;
}

bool navigate_collection(const HitTestInput& input, const detail::Targeting& targeting,
    const std::vector<std::uint32_t>& candidates, const InputEvent& event,
    std::optional<NodeHandle>& focused, std::vector<ActionRequest>& actions) {
    if (!focused) return false;
    const auto& boxes = input.layout->boxes;
    const auto from = targeting.box(*focused);
    const auto& node = *input.tree->get(*focused);
    const auto role = detail::collection_role(node);
    if (!detail::collection_item(role)) return false;
    const auto owner = collection_owner(input, from);
    if (owner == no_layout_parent) return false;
    std::vector<std::uint32_t> items;
    for (const auto candidate : candidates)
        if (detail::collection_item(detail::collection_role(*input.tree->get(boxes[candidate].node))) &&
            collection_owner(input, candidate) == owner) items.push_back(candidate);
    const auto at = std::find(items.begin(), items.end(), from);
    if (at == items.end()) return false;
    if (const auto* key = std::get_if<KeyDown>(&event.data)) {
        focused = boxes[key->key == Key::home ? items.front() : items.back()].node;
        return true;
    }
    const auto direction = std::get<Navigate>(event.data).direction;
    if (direction == Direction::up || direction == Direction::down) {
        if (direction == Direction::up && at != items.begin()) focused = boxes[*std::prev(at)].node;
        if (direction == Direction::down && std::next(at) != items.end()) focused = boxes[*std::next(at)].node;
        if (role == "menu_item") {
            if (direction == Direction::up && at == items.begin()) focused = boxes[items.back()].node;
            if (direction == Direction::down && std::next(at) == items.end()) focused = boxes[items.front()].node;
        }
    } else if (role == "tree_item") {
        const auto* expanded = effective_property(node, property_names::expanded);
        const bool open = expanded && std::get<bool>(*expanded);
        const auto binding = direction == Direction::left ? "collapse" : "expand";
        if (expanded && ((direction == Direction::left && open) || (direction == Direction::right && !open))) {
            if (const auto action = node.events.find(binding); action != node.events.end())
                actions.push_back({binding, action->second, *focused});
        } else if (direction == Direction::left) {
            if (const auto* parent = effective_property(node, property_names::tree_parent)) {
                const auto handle = *input.tree->find(std::get<NodeReference>(*parent).id);
                const auto box = targeting.box(handle);
                if (box != no_layout_parent && targeting.focusable(box)) focused = handle;
            }
        } else if (open) {
            for (const auto child : items) {
                const auto* parent = effective_property(*input.tree->get(boxes[child].node), property_names::tree_parent);
                if (parent && std::get<NodeReference>(*parent).id == node.id) {
                    focused = boxes[child].node;
                    break;
                }
            }
        }
    }
    return true;
}

// Layout box indices of focusable nodes, in tree preorder.
std::vector<std::uint32_t> candidates(const HitTestInput& input, const detail::Targeting& targeting) {
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < input.layout->boxes.size(); ++i) {
        if (targeting.focusable(i)) result.push_back(static_cast<std::uint32_t>(i));
    }
    return result;
}

// Geometry-based movement between border boxes. A candidate must lie wholly beyond the focused box's
// edge in the direction (touching allowed). Lower (unaligned, edge distance, orthogonal gap, preorder)
// wins, so boxes whose orthogonal projections overlap are preferred. There is no wrapping.
std::optional<std::uint32_t> neighbor(const LayoutResult& layout, const std::vector<std::uint32_t>& candidates,
                                      std::uint32_t from, Direction direction) {
    struct Span { double start, end; };
    const auto horizontal = [](const Rect& r) { return Span{r.origin.x, static_cast<double>(r.origin.x) + r.size.width}; };
    const auto vertical = [](const Rect& r) { return Span{r.origin.y, static_cast<double>(r.origin.y) + r.size.height}; };
    const auto& current = layout.boxes[from].border_box;
    const bool along_x = direction == Direction::left || direction == Direction::right;
    const auto main = along_x ? horizontal(current) : vertical(current);
    const auto cross = along_x ? vertical(current) : horizontal(current);
    std::optional<std::tuple<bool, double, double, std::uint32_t>> best;
    for (const auto index : candidates) {
        if (index == from) continue;
        const auto& rect = layout.boxes[index].border_box;
        const auto main_other = along_x ? horizontal(rect) : vertical(rect);
        const auto cross_other = along_x ? vertical(rect) : horizontal(rect);
        const double distance = direction == Direction::right || direction == Direction::down ?
                                    main_other.start - main.end : main.start - main_other.end;
        if (distance < 0) continue;
        const bool aligned = cross_other.start < cross.end && cross.start < cross_other.end;
        const double gap = std::max({0.0, cross_other.start - cross.end, cross.start - cross_other.end});
        const std::tuple score{!aligned, distance, gap, index};
        if (!best || score < *best) best = score;
    }
    if (!best) return {};
    return std::get<3>(*best);
}

struct Reconciled {
    std::optional<NodeHandle> focused;
    bool moved = false; // Stored focus was lost or moved to another node.
};

// The anchor is the stored focus in the same tree, or the node with the stored author ID in a
// replacement tree. An ineligible anchor moves focus to the nearest following focusable node in
// preorder, else the nearest preceding one. A replacement tree without the anchor recovers in its place:
// after the nearest stored preceding neighbor present in the new tree, else before the nearest stored
// following one, by the same rule; without either, focus clears.
Reconciled reconcile(const HitTestInput& input, const detail::Targeting& targeting,
                     const std::vector<std::uint32_t>& candidates, std::optional<NodeHandle> focused,
                     const std::optional<std::string>& id, const std::vector<std::string>& preceding,
                     const std::vector<std::string>& following, std::uint64_t tree) {
    if (!focused) return {};
    const auto& boxes = input.layout->boxes;
    // The first candidate at or after a preorder position, else the last candidate.
    const auto from = [&](std::uint32_t position) -> Reconciled {
        const auto at = std::find_if(candidates.begin(), candidates.end(),
                                     [&](std::uint32_t index) { return boxes[index].node.index >= position; });
        if (at != candidates.end()) return {boxes[*at].node, true};
        if (!candidates.empty()) return {boxes[candidates.back()].node, true};
        return {std::nullopt, true};
    };
    std::optional<NodeHandle> anchor;
    if (tree == input.tree->root().tree) anchor = focused;
    else if (id) anchor = input.tree->find(*id);
    if (!anchor) {
        for (const auto& neighbor : preceding)
            if (const auto node = input.tree->find(neighbor)) return from(node->index + 1);
        for (const auto& neighbor : following)
            if (const auto node = input.tree->find(neighbor)) return from(node->index);
        return {std::nullopt, true};
    }
    const auto box = targeting.box(*anchor);
    if (box != no_layout_parent && targeting.focusable(box)) return {anchor, false};
    if (detail::collection_role(*input.tree->get(*anchor)) == "tree_item") {
        auto parent = effective_property(*input.tree->get(*anchor), property_names::tree_parent);
        while (parent) {
            const auto handle = *input.tree->find(std::get<NodeReference>(*parent).id);
            const auto candidate = targeting.box(handle);
            if (candidate != no_layout_parent && targeting.focusable(candidate)) return {handle, true};
            parent = effective_property(*input.tree->get(handle), property_names::tree_parent);
        }
    }
    return from(anchor->index + 1);
}

} // namespace

Result<FocusDispatchResult> FocusDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const detail::Targeting targeting(input);
    const auto focusable = candidates(input, targeting);
    const auto focused = reconcile(input, targeting, focusable, focused_, id_, preceding_, following_, tree_).focused;
    store(input, focusable, focused);
    return {FocusDispatchResult{focused, {}}, {}};
}

Result<FocusDispatchResult> FocusDispatcher::focus(const HitTestInput& input, NodeHandle target) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    detail::Checker check(errors);
    const detail::Targeting targeting(input);
    if (!input.tree->get(target)) {
        check.error("stale_target", "/target", "Resolve the target again in the current snapshot; this handle belongs to another tree.");
        return {std::nullopt, std::move(errors)};
    }
    const auto box = targeting.box(target);
    if (box == no_layout_parent || !input.layout->boxes[box].visible)
        check.error("hidden_target", "/target", "The target is display-none or hidden and cannot take focus.");
    else if (!targeting.in_scope(box))
        check.error("modal_target", "/target", "Focus a control inside the active Modal, or close it first.");
    else if (!targeting.eligible(target))
        check.error("disabled_target", "/target", "The target or an ancestor is disabled.");
    else if (!targeting.focusable(box))
        check.error("not_focusable", "/target", "Set the target's focusable property to true.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    store(input, candidates(input, targeting), target);
    return {FocusDispatchResult{target, {}}, {}};
}

Result<FocusDispatchResult> FocusDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = validate(input);
    auto event_errors = validate(event);
    errors.insert(errors.end(), event_errors.begin(), event_errors.end());
    detail::Checker check(errors);
    if (!is_command(event))
        check.error("unsupported_event", "/event", "Use focus next/previous, navigate, activate, cancel, or unmodified Home/End.");
    if (timestamp_ && event.timestamp < *timestamp_)
        check.error("event_order", "/timestamp", "Send non-decreasing timestamps within this dispatcher's stream.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const detail::Targeting targeting(input);
    const auto focusable = candidates(input, targeting);
    const auto& boxes = input.layout->boxes;
    const auto reconciled = reconcile(input, targeting, focusable, focused_, id_, preceding_, following_, tree_);
    auto focused = reconciled.focused;
    std::vector<ActionRequest> actions;
    const auto request = [&](std::optional<NodeHandle> origin, const char* binding) {
        if (const auto owner = targeting.binding_owner(origin, binding))
            actions.push_back({binding, input.tree->get(*owner)->events.find(binding)->second, *owner});
    };
    if (!reconciled.moved) std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, FocusNext>) {
            if (focusable.empty()) return;
            const auto next = !focused ? focusable.end() :
                std::find_if(focusable.begin(), focusable.end(),
                             [&](std::uint32_t index) { return boxes[index].node.index > focused->index; });
            focused = boxes[next == focusable.end() ? focusable.front() : *next].node;
        } else if constexpr (std::is_same_v<T, FocusPrevious>) {
            if (focusable.empty()) return;
            const auto previous = !focused ? focusable.rend() :
                std::find_if(focusable.rbegin(), focusable.rend(),
                             [&](std::uint32_t index) { return boxes[index].node.index < focused->index; });
            focused = boxes[previous == focusable.rend() ? focusable.back() : *previous].node;
        } else if constexpr (std::is_same_v<T, Navigate>) {
            if (navigate_collection(input, targeting, focusable, event, focused, actions)) return;
            if (!focused) {
                if (!focusable.empty()) focused = boxes[focusable.front()].node;
            } else if (const auto next = neighbor(*input.layout, focusable, targeting.box(*focused), data.direction)) {
                focused = boxes[*next].node;
            }
        } else if constexpr (std::is_same_v<T, KeyDown>) {
            (void)navigate_collection(input, targeting, focusable, event, focused, actions);
        } else if constexpr (std::is_same_v<T, Activate>) {
            request(focused, "activate");
        } else if constexpr (std::is_same_v<T, Cancel>) {
            request(focused ? focused : input.tree->root(), "cancel");
        }
    }, event.data);
    store(input, focusable, focused);
    timestamp_ = event.timestamp;
    return {FocusDispatchResult{focused, std::move(actions)}, {}};
}

void FocusDispatcher::store(const HitTestInput& input, const std::vector<std::uint32_t>& candidates,
                            std::optional<NodeHandle> focused) {
    std::vector<std::string> preceding, following;
    if (focused) {
        for (const auto index : candidates) {
            const auto node = input.layout->boxes[index].node;
            if (node == *focused) continue;
            if (const auto& id = input.tree->get(node)->id)
                (node.index < focused->index ? preceding : following).push_back(*id);
        }
        std::reverse(preceding.begin(), preceding.end());
    }
    id_ = focused ? input.tree->get(*focused)->id : std::nullopt;
    focused_ = focused;
    preceding_ = std::move(preceding);
    following_ = std::move(following);
    tree_ = input.tree->root().tree;
}

void FocusDispatcher::reset() noexcept {
    focused_.reset();
    id_.reset();
    preceding_.clear();
    following_.clear();
    tree_ = 0;
    timestamp_.reset();
}

} // namespace tessera
