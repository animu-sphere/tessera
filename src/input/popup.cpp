#include <tessera/input/popup.hpp>
#include "../detail/popup.hpp"
#include "../detail/targeting.hpp"
#include <algorithm>

namespace tessera {
namespace {

bool cancel(const InputEvent& event) {
    if (std::holds_alternative<Cancel>(event.data)) return true;
    const auto* key = std::get_if<KeyDown>(&event.data);
    return key && key->key == Key::escape && key->modifiers == Modifiers{};
}

bool descendant(const HitTestInput& input, NodeHandle node, NodeHandle ancestor) {
    const auto& boxes = input.layout->boxes;
    const auto found = std::find_if(boxes.begin(), boxes.end(),
        [&](const LayoutBox& box) { return box.node == node; });
    if (found == boxes.end()) return false;
    auto at = static_cast<std::uint32_t>(found - boxes.begin());
    while (at != no_layout_parent) {
        if (boxes[at].node == ancestor) return true;
        at = boxes[at].parent;
    }
    return false;
}

std::vector<Diagnostic> event_errors(const HitTestInput& input, const InputEvent& event,
    std::optional<std::chrono::microseconds> previous) {
    auto errors = validate(input);
    auto next = validate(event);
    errors.insert(errors.end(), next.begin(), next.end());
    if (previous && event.timestamp < *previous)
        errors.push_back({"event_order", Severity::error, "/timestamp",
            "Send non-decreasing timestamps within this dispatcher's stream."});
    return errors;
}

std::optional<NodeHandle> anchor_focus(const HitTestInput& input, const std::optional<std::string>& id) {
    const auto node = id ? input.tree->find(*id) : std::nullopt;
    if (!node) return {};
    const detail::Targeting targeting(input);
    const auto at = targeting.box(*node);
    return at != no_layout_parent && targeting.focusable(at) ? node : std::nullopt;
}

void check_focus(std::vector<Diagnostic>& errors, const HitTestInput& input, std::optional<NodeHandle> focused) {
    if (input.tree && focused && !input.tree->get(*focused))
        errors.push_back({"stale_target", Severity::error, "/focused",
            "Pass the focus handle for the current snapshot, not an expired tree."});
}

void check_id(std::vector<Diagnostic>& errors, const std::string& id) {
    if (id.empty())
        errors.push_back({"invalid_identifier", Severity::error, "/id", "Supply a nonempty stable author ID."});
}

void check_popup(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id) {
    check_id(errors, id);
    if (!input.tree) return;
    const auto node = input.tree->find(id);
    if (node && !detail::popup_anchor(*input.tree->get(*node)))
        errors.push_back({"popup_target", Severity::error, "/id", "Identify a Popup, not an ordinary content node."});
}

} // namespace

PopupDispatchResult PopupDispatcher::update(const HitTestInput& input) {
    PopupDispatchResult result;
    const bool was_open = open_;
    const auto node = input.tree->find(id_);
    const auto* anchor = node ? detail::popup_anchor(*input.tree->get(*node)) : nullptr;
    const detail::Targeting targeting(input);
    const auto at = node ? targeting.box(*node) : no_layout_parent;
    const auto anchor_node = anchor ? input.tree->find(*anchor) : std::nullopt;
    const bool shown = anchor && at != no_layout_parent && targeting.eligible(*node) &&
        anchor_node && targeting.eligible(*anchor_node);
    if (open_ && (!shown || !anchor_ || *anchor_ != *anchor)) {
        result.focus = anchor_focus(input, anchor_);
        open_ = false;
    }
    if (!shown) {
        result.dismiss = (was_open && !node) || (anchor && input.styles[node->index].display != Display::none);
        return result;
    }
    anchor_ = *anchor;
    if (!open_) {
        for (std::size_t i = 0; i < input.layout->boxes.size(); ++i) {
            const auto candidate = input.layout->boxes[i].node;
            if (targeting.focusable(i) && descendant(input, candidate, *node)) {
                result.focus = candidate;
                break;
            }
        }
    }
    open_ = true;
    return result;
}

Result<PopupDispatchResult> PopupDispatcher::refresh(const HitTestInput& input) {
    auto errors = validate(input);
    check_popup(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {update(input), {}};
}

Result<PopupDispatchResult> PopupDispatcher::dispatch(const HitTestInput& input, const InputEvent& event) {
    auto errors = event_errors(input, event, timestamp_);
    check_popup(errors, input, id_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = update(input);
    timestamp_ = event.timestamp;
    if (!open_) return {result, {}};
    bool dismiss = cancel(event);
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        const auto target = detail::Targeting(input).hit(press->position);
        const auto popup = *input.tree->find(id_);
        dismiss = !target || !descendant(input, *target, popup);
    }
    if (dismiss) {
        result.dismiss = result.consumed = true;
        result.focus = anchor_focus(input, anchor_);
        // Keep the open transition until the host publishes the closed view.
    }
    return {result, {}};
}

TooltipDispatchResult TooltipDispatcher::update(const HitTestInput& input, std::optional<NodeHandle> focused) {
    const detail::Targeting targeting(input);
    const auto anchor = input.tree->find(anchor_);
    bool active = false;
    if (anchor && targeting.eligible(*anchor)) {
        const auto at = targeting.box(*anchor);
        const auto& box = input.layout->boxes[at];
        const auto visible = box.clip ? intersect(box.border_box, *box.clip) : box.border_box;
        if (visible.size.width > 0 && visible.size.height > 0) {
            const auto hit = pointer_ ? targeting.hit(*pointer_) : std::nullopt;
            active = (hit && descendant(input, *hit, *anchor)) ||
                (focused && targeting.eligible(*focused) && descendant(input, *focused, *anchor));
        }
    }
    if (!active) suppressed_ = false;
    return {active && !suppressed_};
}

Result<TooltipDispatchResult> TooltipDispatcher::refresh(const HitTestInput& input, std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    check_focus(errors, input, focused);
    check_id(errors, anchor_);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {update(input, focused), {}};
}

Result<TooltipDispatchResult> TooltipDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
    std::optional<NodeHandle> focused) {
    auto errors = event_errors(input, event, timestamp_);
    check_id(errors, anchor_);
    check_focus(errors, input, focused);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        pointer_ = move->position;
        pointer_id_ = move->pointer;
    } else if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        pointer_ = press->position;
        pointer_id_ = press->pointer;
        suppressed_ = true;
    } else if (const auto* lost = std::get_if<PointerCancel>(&event.data);
               lost && pointer_id_ == lost->pointer) {
        pointer_.reset();
        pointer_id_.reset();
    }
    if (cancel(event)) suppressed_ = true;
    timestamp_ = event.timestamp;
    return {update(input, focused), {}};
}

} // namespace tessera
