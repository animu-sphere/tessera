#include <tessera/input/split_pane.hpp>
#include <tessera/semantics/semantics.hpp>
#include <tessera/ui/authoring.hpp>
#include "axis_adjustment.hpp"
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {
float axis(Point point, FlexDirection direction) { return direction == FlexDirection::row ? point.x : point.y; }
float extent(Size size, FlexDirection direction) { return direction == FlexDirection::row ? size.width : size.height; }
}

Result<std::optional<SplitPaneGeometry>> split_pane_geometry(const HitTestInput& input, std::string_view id) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (id.empty())
        return {std::nullopt, {{"split_pane_id", Severity::error, "/id", "Supply the SplitPane's stable author ID."}}};
    const auto root = input.tree->find(id);
    if (!root) return {std::optional<SplitPaneGeometry>{}, {}};
    const auto first = input.tree->find(std::string(id) + "-first");
    const auto handle = input.tree->find(std::string(id) + "-handle");
    const auto second = input.tree->find(std::string(id) + "-second");
    if (!first || !handle || !second) return {std::optional<SplitPaneGeometry>{}, {}};
    if (input.tree->children(*root) != std::vector<NodeHandle>{*first, *handle, *second})
        return {std::nullopt,
                {{"split_pane_structure", Severity::error, "/id",
                  "Use the generated first/handle/second children of a public SplitPane."}}};
    auto box = [&](NodeHandle node) -> const LayoutBox* {
        const auto found = std::find_if(input.layout->boxes.begin(), input.layout->boxes.end(),
                                        [&](const auto& value) { return value.node == node; });
        return found == input.layout->boxes.end() ? nullptr : &*found;
    };
    const auto* parent_box = box(*root);
    const auto* first_box = box(*first);
    const auto* handle_box = box(*handle);
    const auto* second_box = box(*second);
    if (!parent_box || !first_box || !handle_box || !second_box || !handle_box->visible)
        return {std::optional<SplitPaneGeometry>{}, {}};
    auto semantics = build_semantic_tree({input.tree, input.styles, input.layout});
    if (!semantics) return {std::nullopt, std::move(semantics.diagnostics)};
    const auto entry = std::find_if(semantics.value->nodes.begin(), semantics.value->nodes.end(),
                                    [&](const auto& value) { return value.node == *handle; });
    if (entry == semantics.value->nodes.end() || entry->role != SemanticRole::separator)
        return {std::nullopt,
                {{"split_pane_structure", Severity::error, "/id", "Keep the generated handle's separator semantics."}}};
    const auto direction = input.styles[root->index].direction;
    const auto& a = input.styles[first->index];
    const auto& b = input.styles[second->index];
    const authoring::SplitPaneLimits limits{direction == FlexDirection::row ? a.min_width : a.min_height,
                                            direction == FlexDirection::row ? b.min_width : b.min_height,
                                            extent(handle_box->border_box.size, direction)};
    const float available = extent(parent_box->content_box().size, direction);
    auto maximum = authoring::split_pane_size_at(limits, available, available);
    if (!maximum) return {std::nullopt, std::move(maximum.diagnostics)};
    return {SplitPaneGeometry{*handle, handle_box->border_box, first_box->border_box.origin, direction,
                              extent(first_box->border_box.size, direction), limits.minimum_first, *maximum.value,
                              entry->enabled && entry->focusable && !entry->actions.empty()},
            {}};
}

Result<std::optional<SplitPaneGeometry>> SplitPaneDispatcher::reconcile_geometry(const HitTestInput& input) {
    if (!std::isfinite(step_) || step_ <= 0)
        return {std::nullopt,
                {{"split_pane_step", Severity::error, "/step", "Supply a finite positive keyboard step."}}};
    auto geometry = split_pane_geometry(input, id_);
    if (!geometry || !drag_) return geometry;
    const auto& view = *geometry.value;
    if (!view || !view->enabled || drag_->direction != view->direction ||
        drag_->handle_size != extent(view->bounds.size, view->direction) ||
        input.tree->get(view->handle)->events.at("activate") != drag_->action)
        reset();
    return geometry;
}

Result<SplitPaneDispatchResult> SplitPaneDispatcher::refresh(const HitTestInput& input) {
    auto geometry = reconcile_geometry(input);
    if (!geometry) return {std::nullopt, std::move(geometry.diagnostics)};
    SplitPaneDispatchResult result;
    if (const auto& view = *geometry.value) result.size = view->size;
    return {result, {}};
}

Result<SplitPaneDispatchResult> SplitPaneDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                              std::optional<NodeHandle> focused) {
    auto errors = validate(event);
    auto semantic_errors = validate(SemanticInput{input.tree, input.styles, input.layout, focused});
    errors.insert(errors.end(), semantic_errors.begin(), semantic_errors.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto candidate = *this;
    auto geometry = candidate.reconcile_geometry(input);
    if (!geometry) return {std::nullopt, std::move(geometry.diagnostics)};
    SplitPaneDispatchResult result;
    if (!*geometry.value || !(**geometry.value).enabled) {
        *this = std::move(candidate);
        return {result, {}};
    }
    const auto& view = **geometry.value;
    auto adjustment = [&](float requested) -> Result<SplitPaneDispatchResult> {
        const float size = std::clamp(requested, view.minimum, view.maximum);
        result.handled = true;
        if (size != view.size) {
            auto action = request_semantic_action({input.tree, input.styles, input.layout}, view.handle, "activate");
            if (!action) return {std::nullopt, std::move(action.diagnostics)};
            result.size = size;
            result.action = std::move(*action.value);
        }
        *this = std::move(candidate);
        return {result, {}};
    };
    if (const auto* cancel = std::get_if<PointerCancel>(&event.data)) {
        if (candidate.drag_ && candidate.drag_->pointer == cancel->pointer) {
            candidate.reset();
            result.handled = true;
        }
    } else if (const auto* down = std::get_if<PointerDown>(&event.data)) {
        if (!candidate.drag_ && down->button == PointerButton::primary) {
            auto hit = hit_test(input, down->position);
            if (!hit) return {std::nullopt, std::move(hit.diagnostics)};
            if (hit.value->target == view.handle) {
                candidate.drag_ =
                    Drag{down->pointer, axis(down->position, view.direction) - axis(view.bounds.origin, view.direction),
                         view.direction, extent(view.bounds.size, view.direction),
                         input.tree->get(view.handle)->events.at("activate")};
                result.handled = true;
                result.focus = view.handle;
            }
        }
    } else if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (candidate.drag_ && candidate.drag_->pointer == move->pointer)
            return adjustment(axis(move->position, view.direction) - axis(view.first_origin, view.direction) -
                              candidate.drag_->grab);
    } else if (const auto* up = std::get_if<PointerUp>(&event.data)) {
        if (candidate.drag_ && candidate.drag_->pointer == up->pointer && up->button == PointerButton::primary) {
            const auto grab = candidate.drag_->grab;
            candidate.reset();
            return adjustment(axis(up->position, view.direction) - axis(view.first_origin, view.direction) - grab);
        }
    } else if (std::holds_alternative<Cancel>(event.data) ||
               (std::get_if<KeyDown>(&event.data) && std::get<KeyDown>(event.data).key == Key::escape &&
                std::get<KeyDown>(event.data).modifiers == Modifiers{})) {
        result.handled = candidate.drag_.has_value();
        candidate.reset();
    } else {
        candidate.reset();
        if (focused == view.handle) {
            if (const auto requested = detail::axis_adjustment(event, view.direction == FlexDirection::row, view.size,
                                                               view.minimum, view.maximum, step_))
                return adjustment(*requested);
        }
    }
    *this = std::move(candidate);
    return {result, {}};
}
} // namespace tessera
