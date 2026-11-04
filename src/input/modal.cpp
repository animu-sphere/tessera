#include <tessera/input/modal.hpp>
#include "../detail/modal.hpp"
#include "../detail/targeting.hpp"

namespace tessera {
namespace {

void check_modal(std::vector<Diagnostic>& errors, const HitTestInput& input, const std::string& id,
                 std::optional<NodeHandle> focused) {
    if (id.empty())
        errors.push_back({"invalid_identifier", Severity::error, "/id", "Supply the Modal's stable author ID."});
    if (!input.tree) return;
    const auto node = input.tree->find(id);
    if (node && !detail::modal(*input.tree->get(*node)))
        errors.push_back({"modal_target", Severity::error, "/id", "Identify a Modal, not an ordinary content node."});
    if (focused && !input.tree->get(*focused))
        errors.push_back({"stale_target", Severity::error, "/focused", "Pass focus from the current snapshot."});
}

bool descendant(const HitTestInput& input, const detail::Targeting& targeting, NodeHandle node, NodeHandle ancestor) {
    for (auto at = targeting.box(node); at != no_layout_parent; at = input.layout->boxes[at].parent)
        if (input.layout->boxes[at].node == ancestor) return true;
    return false;
}

} // namespace

ModalDispatchResult ModalDispatcher::update(const HitTestInput& input, std::optional<NodeHandle> focused) {
    ModalDispatchResult result;
    const detail::Targeting targeting(input);
    const auto node = input.tree->find(id_);
    result.active = node && targeting.eligible(*node);
    result.reset_pointer = result.active != open_;
    if (result.active) {
        if (!open_ && focused && !descendant(input, targeting, *focused, *node))
            restore_ = input.tree->get(*focused)->id;
        const auto at = focused ? targeting.box(*focused) : no_layout_parent;
        if (!open_ || at == no_layout_parent || !targeting.focusable(at)) {
            for (std::size_t i = 0; i < input.layout->boxes.size(); ++i)
                if (targeting.focusable(i)) {
                    result.focus = input.layout->boxes[i].node;
                    break;
                }
        }
    } else {
        if (open_) {
            const auto target = restore_ ? input.tree->find(*restore_) : std::nullopt;
            const auto at = target ? targeting.box(*target) : no_layout_parent;
            if (at != no_layout_parent && targeting.focusable(at)) result.focus = target;
            restore_.reset();
            result.dismiss = !node || input.styles[node->index].display != Display::none;
        }
        if (focused) {
            const auto at = targeting.box(*focused);
            if (at != no_layout_parent && targeting.focusable(at))
                restore_ = input.tree->get(*focused)->id;
            else
                restore_.reset();
        } else
            restore_.reset();
    }
    open_ = result.active;
    return result;
}

Result<ModalDispatchResult> ModalDispatcher::refresh(const HitTestInput& input, std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    check_modal(errors, input, id_, focused);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {update(input, focused), {}};
}

Result<ModalDispatchResult> ModalDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                      std::optional<NodeHandle> focused) {
    auto errors = validate(input);
    auto next = validate(event);
    errors.insert(errors.end(), next.begin(), next.end());
    check_modal(errors, input, id_, focused);
    if (timestamp_ && event.timestamp < *timestamp_)
        errors.push_back({"event_order", Severity::error, "/timestamp", "Send non-decreasing timestamps."});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = update(input, focused);
    timestamp_ = event.timestamp;
    result.consumed = result.reset_pointer;
    if (!result.active) return {result, {}};
    const auto* key = std::get_if<KeyDown>(&event.data);
    if (std::holds_alternative<Cancel>(event.data) ||
        (key && key->key == Key::escape && key->modifiers == Modifiers{})) {
        result.dismiss = result.consumed = result.reset_pointer = true;
    }
    return {result, {}};
}

} // namespace tessera
