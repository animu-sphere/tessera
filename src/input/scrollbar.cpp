#include <tessera/input/scrollbar.hpp>
#include <tessera/semantics/semantics.hpp>
#include "../detail/targeting.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {
namespace {
bool near(float a, float b) {
    return std::abs(a - b) <= 8 * std::numeric_limits<float>::epsilon() * std::max({1.f, std::abs(a), std::abs(b)});
}
}

Result<std::optional<ScrollbarGeometry>> scrollbar_geometry(const HitTestInput& input, std::string_view id,
                                                            std::string_view scroll_id) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (id.empty() || scroll_id.empty() || id == scroll_id)
        return {std::nullopt,
                {{"scrollbar_id", Severity::error, "/id", "Supply distinct stable scrollbar and ScrollView IDs."}}};
    const auto root = input.tree->find(id);
    const auto container = input.tree->find(scroll_id);
    if (!root || !container) return {std::optional<ScrollbarGeometry>{}, {}};
    auto box = [&](NodeHandle node) -> const LayoutBox* {
        const auto found = std::find_if(input.layout->boxes.begin(), input.layout->boxes.end(),
                                        [&](const auto& value) { return value.node == node; });
        return found == input.layout->boxes.end() ? nullptr : &*found;
    };
    const auto* track = box(*root);
    const auto* scroll = box(*container);
    if (!track || !scroll || !track->visible || !scroll->visible) return {std::optional<ScrollbarGeometry>{}, {}};
    if (!scroll->scroll)
        return {
            std::nullopt,
            {{"scrollbar_container", Severity::error, "/scroll_id", "Bind the scrollbar to an Overflow::scroll box."}}};
    const auto before = input.tree->find(std::string(id) + "-before");
    const auto thumb = input.tree->find(std::string(id) + "-thumb");
    if (!before || !thumb || input.tree->children(*root) != std::vector<NodeHandle>{*before, *thumb})
        return {std::nullopt,
                {{"scrollbar_structure", Severity::error, "/id",
                  "Keep VerticalScrollbar's generated before/thumb children."}}};
    ScrollbarGeometry view{
        *container, *thumb, track->content_box(), {}, scroll->scroll->offset, scroll->scroll_limit().y};
    const auto* handle = box(*thumb);
    if (handle) view.bounds = handle->border_box;
    if (!near(view.track.size.height, scroll->padding_box().size.height))
        return {std::nullopt,
                {{"scrollbar_geometry", Severity::error, "/id",
                  "Recompose the scrollbar from the accepted scroll viewport."}}};
    if (view.maximum <= 0 || !handle || !handle->visible) return {view, {}};
    const float travel = view.track.size.height - view.bounds.size.height;
    const auto expected_size = std::min(
        view.track.size.height, std::max(input.styles[thumb->index].min_height,
                                         static_cast<float>(double(view.track.size.height) * view.track.size.height /
                                                            scroll->scroll->extent.height)));
    const auto expected = static_cast<float>(double(std::max(0.f, travel)) * view.offset.y / view.maximum);
    if (travel < 0 || !near(view.bounds.size.height, expected_size) ||
        !near(view.bounds.origin.y, view.track.origin.y + expected))
        return {std::nullopt,
                {{"scrollbar_geometry", Severity::error, "/id",
                  "Recompose the thumb from the accepted scroll extent and offset before dispatch."}}};
    auto semantics = build_semantic_tree({input.tree, input.styles, input.layout});
    if (!semantics) return {std::nullopt, std::move(semantics.diagnostics)};
    const auto entry = std::find_if(semantics.value->nodes.begin(), semantics.value->nodes.end(),
                                    [&](const auto& value) { return value.node == *thumb; });
    if (entry == semantics.value->nodes.end() || entry->role != SemanticRole::scrollbar)
        return {std::nullopt,
                {{"scrollbar_structure", Severity::error, "/id", "Keep the generated thumb's scrollbar semantics."}}};
    const detail::Targeting targeting(input);
    const auto index = static_cast<std::uint32_t>(scroll - input.layout->boxes.data());
    const auto visible = handle->clip ? intersect(view.bounds, *handle->clip) : view.bounds;
    view.enabled = travel > 0 && visible.size.width > 0 && visible.size.height > 0 && targeting.eligible(index) &&
                   entry->enabled && entry->focusable && !entry->actions.empty();
    return {view, {}};
}

Result<ScrollbarDispatchResult> ScrollbarDispatcher::refresh(const HitTestInput& input) {
    if (!std::isfinite(step_) || step_ <= 0)
        return {std::nullopt,
                {{"scrollbar_step", Severity::error, "/step", "Supply a finite positive keyboard step."}}};
    auto geometry = scrollbar_geometry(input, id_, scroll_id_);
    if (!geometry) return {std::nullopt, std::move(geometry.diagnostics)};
    ScrollbarDispatchResult result;
    if (!*geometry.value) {
        reset();
        return {result, {}};
    }
    const auto& view = **geometry.value;
    result.update = ScrollUpdate{view.container, view.offset};
    const auto* node = input.tree->get(view.thumb);
    if (drag_ &&
        (!view.enabled || drag_->thumb_size != view.bounds.size.height || drag_->track_size != view.track.size.height ||
         drag_->maximum != view.maximum || node->events.at("activate") != drag_->action))
        reset();
    return {result, {}};
}

Result<ScrollbarDispatchResult> ScrollbarDispatcher::dispatch(const HitTestInput& input, const InputEvent& event,
                                                              std::optional<NodeHandle> focused) {
    auto errors = validate(event);
    auto semantic_errors = validate(SemanticInput{input.tree, input.styles, input.layout, focused});
    errors.insert(errors.end(), semantic_errors.begin(), semantic_errors.end());
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto candidate = *this;
    auto refreshed = candidate.refresh(input);
    if (!refreshed) return refreshed;
    auto geometry = scrollbar_geometry(input, id_, scroll_id_);
    if (!geometry) return {std::nullopt, std::move(geometry.diagnostics)};
    ScrollbarDispatchResult result;
    if (!*geometry.value || !(**geometry.value).enabled) {
        *this = std::move(candidate);
        return {result, {}};
    }
    const auto& view = **geometry.value;
    auto adjustment = [&](double requested, bool rounded_pointer = false) -> Result<ScrollbarDispatchResult> {
        auto offset = view.offset;
        offset.y = static_cast<float>(std::clamp(requested, 0., double(view.maximum)));
        result.handled = true;
        if (offset.y != view.offset.y &&
            !(rounded_pointer && offset.y > 0 && offset.y < view.maximum && near(offset.y, view.offset.y))) {
            auto action = request_semantic_action({input.tree, input.styles, input.layout}, view.thumb, "activate");
            if (!action) return {std::nullopt, std::move(action.diagnostics)};
            result.update = ScrollUpdate{view.container, offset};
            result.action = std::move(*action.value);
        }
        *this = std::move(candidate);
        return {result, {}};
    };
    auto dragged_offset = [&](Point point, float grab) {
        const double travel = double(view.track.size.height) - view.bounds.size.height;
        return std::clamp((double(point.y) - view.track.origin.y - grab) / travel, 0., 1.) * view.maximum;
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
            if (hit.value->target == view.thumb) {
                candidate.drag_ = Drag{down->pointer,
                                       down->position.y - view.bounds.origin.y,
                                       view.bounds.size.height,
                                       view.track.size.height,
                                       view.maximum,
                                       input.tree->get(view.thumb)->events.at("activate")};
                result.handled = true;
                result.focus = view.thumb;
            }
        }
    } else if (const auto* move = std::get_if<PointerMove>(&event.data)) {
        if (candidate.drag_ && candidate.drag_->pointer == move->pointer)
            return adjustment(dragged_offset(move->position, candidate.drag_->grab), true);
    } else if (const auto* up = std::get_if<PointerUp>(&event.data)) {
        if (candidate.drag_ && candidate.drag_->pointer == up->pointer && up->button == PointerButton::primary) {
            const auto requested = dragged_offset(up->position, candidate.drag_->grab);
            candidate.reset();
            return adjustment(requested, true);
        }
    } else if (std::holds_alternative<Cancel>(event.data) ||
               (std::get_if<KeyDown>(&event.data) && std::get<KeyDown>(event.data).key == Key::escape &&
                std::get<KeyDown>(event.data).modifiers == Modifiers{})) {
        result.handled = candidate.drag_.has_value();
        candidate.reset();
    } else {
        if (!std::holds_alternative<Scroll>(event.data)) candidate.reset();
        if (focused == view.thumb) {
            double requested = view.offset.y;
            bool adjusts = false;
            if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
                switch (key->key) {
                    case Key::home:
                        requested = 0;
                        adjusts = true;
                        break;
                    case Key::end:
                        requested = view.maximum;
                        adjusts = true;
                        break;
                    case Key::up:
                        requested -= step_;
                        adjusts = true;
                        break;
                    case Key::down:
                        requested += step_;
                        adjusts = true;
                        break;
                    default:
                        break;
                }
            }
            if (const auto* navigate = std::get_if<Navigate>(&event.data)) {
                if (navigate->direction == Direction::up) {
                    requested -= step_;
                    adjusts = true;
                }
                if (navigate->direction == Direction::down) {
                    requested += step_;
                    adjusts = true;
                }
            }
            if (std::holds_alternative<Activate>(event.data)) {
                requested += step_;
                adjusts = true;
            }
            if (adjusts) {
                candidate.reset();
                return adjustment(requested);
            }
        }
    }
    *this = std::move(candidate);
    return {result, {}};
}
} // namespace tessera
