#include <tessera/input/focus.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/targeting.hpp"
#include <algorithm>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {

bool is_command(const InputEvent& event) {
    return std::holds_alternative<FocusNext>(event.data) || std::holds_alternative<FocusPrevious>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<Activate>(event.data) ||
           std::holds_alternative<Cancel>(event.data);
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
        check.error("unsupported_event", "/event", "FocusDispatcher accepts only focus next/previous, navigate, activate, and cancel.");
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
            if (!focused) {
                if (!focusable.empty()) focused = boxes[focusable.front()].node;
            } else if (const auto next = neighbor(*input.layout, focusable, targeting.box(*focused), data.direction)) {
                focused = boxes[*next].node;
            }
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
