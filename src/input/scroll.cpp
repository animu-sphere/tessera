#include <tessera/input/scroll.hpp>
#include "../detail/checks.hpp"
#include "../detail/targeting.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tessera {
namespace {

// Moves one axis's offset by `delta` within [0, limit] and returns the part of `delta` left over.
float consume(float& offset, float delta, float limit) {
    if (delta > 0) {
        const float room = limit - offset;
        if (delta < room) {
            offset = std::min(offset + delta, limit);
            return 0;
        }
        offset = limit;
        return delta - room;
    }
    if (delta < 0) {
        if (-delta < offset) {
            offset = std::max(offset + delta, 0.0f);
            return 0;
        }
        const float left = delta + offset;
        offset = 0;
        return left;
    }
    return 0;
}

// Float steps of slack, relative to the coordinates involved, within which a span counts as revealed.
constexpr float reveal_rounding_steps = 4;

// Adjusts one axis's offset so the span [start, start + size) lies inside the viewport where possible, and
// moves `start` with the content. A misalignment within rounding of the offset arithmetic counts as none, so
// revealing an already revealed span requests no change.
void reveal(float& offset, float& start, float size, float view_start, float view_size, float limit) {
    float delta = 0;
    if (size > view_size || start < view_start) delta = start - view_start;
    else if (start + size > view_start + view_size) delta = start + size - (view_start + view_size);
    const float magnitude = std::max({std::fabs(start), std::fabs(start + size), std::fabs(view_start),
                                      std::fabs(view_start + view_size)}) + offset;
    if (std::fabs(delta) <= reveal_rounding_steps * std::numeric_limits<float>::epsilon() * magnitude) return;
    const float next = std::min(std::max(offset + delta, 0.0f), limit);
    start -= next - offset;
    offset = next;
}

} // namespace

Result<ScrollRouteResult> route_scroll(const HitTestInput& input, const Scroll& scroll) {
    auto errors = validate(input);
    detail::Checker check(errors);
    check.point(scroll.position, "/position");
    check.point(scroll.delta, "/delta");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const detail::Targeting targeting(input);
    ScrollRouteResult result;
    result.target = targeting.hit(scroll.position);
    result.remaining = scroll.delta;
    if (!result.target) return {std::move(result), {}};
    const auto& boxes = input.layout->boxes;
    for (auto index = targeting.box(*result.target); index != no_layout_parent; index = boxes[index].parent) {
        const auto& box = boxes[index];
        if (!box.scroll || !targeting.eligible(index)) continue;
        if (result.remaining.x == 0 && result.remaining.y == 0) break;
        const auto limit = box.scroll_limit();
        auto offset = box.scroll->offset;
        result.remaining.x = consume(offset.x, result.remaining.x, limit.x);
        result.remaining.y = consume(offset.y, result.remaining.y, limit.y);
        if (offset != box.scroll->offset) result.updates.push_back({box.node, offset});
    }
    return {std::move(result), {}};
}

Result<std::vector<ScrollUpdate>> scroll_into_view(const HitTestInput& input, NodeHandle target) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    detail::Checker check(errors);
    const detail::Targeting targeting(input);
    if (!input.tree->get(target))
        check.error("stale_target", "/target", "Use a handle from the supplied tree snapshot.");
    else if (targeting.box(target) == no_layout_parent)
        check.error("hidden_target", "/target", "A display-none node has no geometry to reveal.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const auto& boxes = input.layout->boxes;
    const auto& own = boxes[targeting.box(target)];
    auto rect = own.border_box; // Moves with each offset change so outer boxes reveal the adjusted position.
    std::vector<ScrollUpdate> updates;
    for (auto index = own.parent; index != no_layout_parent; index = boxes[index].parent) {
        const auto& box = boxes[index];
        if (!box.scroll) continue;
        const auto viewport = box.padding_box();
        const auto limit = box.scroll_limit();
        auto offset = box.scroll->offset;
        reveal(offset.x, rect.origin.x, rect.size.width, viewport.origin.x, viewport.size.width, limit.x);
        reveal(offset.y, rect.origin.y, rect.size.height, viewport.origin.y, viewport.size.height, limit.y);
        if (offset != box.scroll->offset) updates.push_back({box.node, offset});
    }
    return {std::move(updates), {}};
}

} // namespace tessera
