#include <tessera/input/collection_drag.hpp>
#include "../detail/targeting.hpp"
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {

bool finite(Point point) { return std::isfinite(point.x) && std::isfinite(point.y); }

// The collection node, or no node when absent; a node without a list or tree role is an error.
Result<std::optional<NodeHandle>> collection(const HitTestInput& input, const std::string& id) {
    auto errors = validate(input);
    if (id.empty())
        errors.push_back({"invalid_identifier", Severity::error, "/id", "Supply a nonempty stable author ID."});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto node = input.tree->find(id);
    if (!node) return {std::optional<NodeHandle>{}, {}};
    const auto role = detail::collection_role(*input.tree->get(*node));
    if (role != "list" && role != "tree")
        return {std::nullopt, {{"collection_drag_target", Severity::error, "/id", "Identify a ListView or TreeView."}}};
    return {node, {}};
}

// Displayed rows of the collection in order, with their keys.
std::vector<std::pair<std::string, std::uint32_t>> rows(const HitTestInput& input, const detail::Targeting& targeting,
                                                        NodeHandle owner, const std::string& id) {
    std::vector<std::pair<std::string, std::uint32_t>> result;
    const auto prefix = id + "-item-";
    for (const auto child : input.tree->children(owner)) {
        const auto* node = input.tree->get(child);
        const auto box = targeting.box(child);
        if (box == no_layout_parent || !input.layout->boxes[box].visible || !node->id ||
            !node->id->starts_with(prefix) || !detail::collection_item(detail::collection_role(*node)))
            continue;
        result.emplace_back(node->id->substr(prefix.size()), box);
    }
    return result;
}

std::optional<CollectionDropTarget> target_at(const HitTestInput& input, const detail::Targeting& targeting,
                                              NodeHandle owner, const std::string& id, Point position) {
    if (!targeting.eligible(owner)) return {};
    const auto& box = input.layout->boxes[targeting.box(owner)];
    const auto visible = box.clip ? intersect(box.border_box, *box.clip) : box.border_box;
    if (!detail::inside(visible, position)) return {};
    std::optional<CollectionDropTarget> best;
    double nearest = INFINITY;
    for (const auto& [key, at] : rows(input, targeting, owner, id)) {
        const auto& row = input.layout->boxes[at].border_box;
        const double top = row.origin.y, bottom = top + row.size.height;
        const double distance = position.y < top ? top - position.y : position.y >= bottom ? position.y - bottom : 0;
        if (distance >= nearest) continue;
        nearest = distance;
        best = CollectionDropTarget{key, position.y < top + row.size.height / 2 ? DropPlacement::before
                                                                                : DropPlacement::after};
    }
    return best;
}

// The eligible item row holding the pressed hit, if the press landed on this collection's rows.
std::optional<std::string> pressed_item(const HitTestInput& input, const detail::Targeting& targeting, NodeHandle owner,
                                        const std::string& id, Point position) {
    const auto hit = targeting.hit(position);
    if (!hit || !targeting.eligible(owner)) return {};
    const auto& boxes = input.layout->boxes;
    for (auto at = targeting.box(*hit); at != no_layout_parent; at = boxes[at].parent) {
        if (boxes[at].parent == no_layout_parent || boxes[boxes[at].parent].node != owner) continue;
        const auto& row = boxes[at].node;
        for (const auto& [key, box] : rows(input, targeting, owner, id))
            if (boxes[box].node == row) return targeting.eligible(row) ? std::optional{key} : std::nullopt;
        return {};
    }
    return {};
}

bool escape(const InputEvent& event) {
    if (std::holds_alternative<Cancel>(event.data)) return true;
    const auto* key = std::get_if<KeyDown>(&event.data);
    return key && key->key == Key::escape && key->modifiers == Modifiers{};
}

bool interrupts(const InputEvent& event) {
    return std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<TextInput>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<FocusNext>(event.data) ||
           std::holds_alternative<FocusPrevious>(event.data) || std::holds_alternative<Activate>(event.data);
}

std::vector<Diagnostic> option_errors(const CollectionDragOptions& options) {
    if (std::isfinite(options.threshold) && options.threshold >= 0) return {};
    return {{"collection_drag_options", Severity::error, "/options", "Supply a finite nonnegative drag threshold."}};
}

} // namespace

Result<std::optional<CollectionDropTarget>> collection_drop_target(const HitTestInput& input, std::string_view id,
                                                                   Point position) {
    const std::string name(id);
    auto owner = collection(input, name);
    if (!owner) return {std::nullopt, std::move(owner.diagnostics)};
    if (!finite(position))
        return {std::nullopt,
                {{"invalid_position", Severity::error, "/position", "Supply a finite root-logical position."}}};
    if (!*owner.value) return {std::optional<CollectionDropTarget>{}, {}};
    const detail::Targeting targeting(input);
    return {target_at(input, targeting, **owner.value, name, position), {}};
}

Result<CollectionDragDispatchResult> CollectionDragDispatcher::refresh(const HitTestInput& input) {
    auto owner = collection(input, id_);
    auto errors = option_errors(options_);
    if (!owner) errors.insert(errors.begin(), owner.diagnostics.begin(), owner.diagnostics.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    CollectionDragDispatchResult result;
    if (!drag_) return {result, {}};
    const detail::Targeting targeting(input);
    const auto row = input.tree->find(id_ + "-item-" + drag_->key);
    const auto listed = [&] {
        const auto shown = rows(input, targeting, **owner.value, id_);
        return std::any_of(shown.begin(), shown.end(), [&](const auto& entry) { return entry.first == drag_->key; });
    };
    if (!*owner.value || !row || !targeting.eligible(**owner.value) || !targeting.eligible(*row) || !listed()) {
        result.ended = result.canceled = drag_->dragging;
        reset();
        return {result, {}};
    }
    if (drag_->dragging) {
        result.source = drag_->key;
        result.target = target_at(input, targeting, **owner.value, id_, drag_->position);
    }
    return {result, {}};
}

Result<CollectionDragDispatchResult> CollectionDragDispatcher::dispatch(const HitTestInput& input,
                                                                        const InputEvent& event) {
    auto owner = collection(input, id_);
    auto errors = validate(event);
    auto options = option_errors(options_);
    errors.insert(errors.end(), options.begin(), options.end());
    if (!owner) errors.insert(errors.begin(), owner.diagnostics.begin(), owner.diagnostics.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    CollectionDragDispatchResult result;
    // A replacement may already have removed the collection or the dragged item.
    auto refreshed = refresh(input);
    if (!refreshed) return refreshed;
    result.ended = refreshed.value->ended;
    result.canceled = refreshed.value->canceled;
    if (!*owner.value) return {result, {}};
    const detail::Targeting targeting(input);
    const auto dragging_result = [&](Point position) {
        drag_->position = position;
        result.consumed = true;
        result.source = drag_->key;
        result.target = target_at(input, targeting, **owner.value, id_, position);
    };
    if (const auto* press = std::get_if<PointerDown>(&event.data)) {
        if (drag_ || press->button != PointerButton::primary) return {result, {}};
        if (const auto key = pressed_item(input, targeting, **owner.value, id_, press->position))
            drag_ = Drag{press->pointer, *key, press->position, press->position};
        return {result, {}};
    }
    if (!drag_) return {result, {}};
    if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (move->pointer != drag_->pointer) return {result, {}};
        if (!drag_->dragging) {
            const double dx = static_cast<double>(move->position.x) - drag_->origin.x;
            const double dy = static_cast<double>(move->position.y) - drag_->origin.y;
            if (std::hypot(dx, dy) < options_.threshold) {
                drag_->position = move->position;
                return {result, {}};
            }
            drag_->dragging = result.started = true;
        }
        dragging_result(move->position);
        return {result, {}};
    }
    if (const auto* release = std::get_if<PointerUp>(&event.data)) {
        if (release->pointer != drag_->pointer || release->button != PointerButton::primary) return {result, {}};
        if (drag_->dragging) {
            dragging_result(release->position);
            result.drop = result.target;
            result.ended = true;
            result.canceled = !result.drop;
        }
        reset();
        return {result, {}};
    }
    if (const auto* lost = std::get_if<PointerCancel>(&event.data)) {
        if (lost->pointer != drag_->pointer) return {result, {}};
        result.consumed = result.ended = result.canceled = drag_->dragging;
        reset();
        return {result, {}};
    }
    if (escape(event) || interrupts(event)) {
        // Escape and Cancel belong to a drag; other input continues to ordinary routing.
        result.consumed = drag_->dragging && escape(event);
        result.ended = result.canceled = drag_->dragging;
        reset();
    }
    return {result, {}};
}

} // namespace tessera
