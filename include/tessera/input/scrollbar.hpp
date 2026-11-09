#pragma once

#include <tessera/input/scroll.hpp>
#include <string>

namespace tessera {

// Derived from the generated track: VerticalScrollbar adjusts Y, HorizontalScrollbar adjusts X.
enum class ScrollbarAxis : std::uint8_t { vertical, horizontal };

struct ScrollbarGeometry {
    ScrollbarAxis axis = ScrollbarAxis::vertical;
    NodeHandle container;
    NodeHandle thumb;
    Rect track;
    Rect bounds;
    Point offset;
    float maximum = 0; // Main-axis scroll limit.
    bool enabled = false;
};
// Both IDs refer to one coherent accepted presentation. Missing/hidden controls
// are absent; stale authored thumb geometry is diagnosed rather than dispatched.
Result<std::optional<ScrollbarGeometry>> scrollbar_geometry(const HitTestInput&, std::string_view id,
                                                            std::string_view scroll_id);

struct ScrollbarDispatchResult {
    bool handled = false;
    std::optional<ScrollUpdate> update;
    std::optional<ActionRequest> action;
    std::optional<NodeHandle> focus;
};

class ScrollbarDispatcher final {
public:
    ScrollbarDispatcher(std::string id, std::string scroll_id, float keyboard_step = 16)
        : id_(std::move(id)), scroll_id_(std::move(scroll_id)), step_(keyboard_step) {}
    Result<ScrollbarDispatchResult> dispatch(const HitTestInput&, const InputEvent&,
                                             std::optional<NodeHandle> focused = {});
    // At every accepted replacement, including removal. update is the canonical
    // layout-clamped host offset, never an action or a second scroll state.
    Result<ScrollbarDispatchResult> refresh(const HitTestInput&);
    bool dragging() const noexcept { return drag_.has_value(); }
    void reset() noexcept { drag_.reset(); }

private:
    Result<std::optional<ScrollbarGeometry>> reconcile_geometry(const HitTestInput&);
    struct Drag {
        PointerId pointer;
        float grab;
        float thumb_size;
        float track_size;
        float maximum;
        ScrollbarAxis axis;
        std::string action;
    };
    std::string id_, scroll_id_;
    float step_;
    std::optional<Drag> drag_;
};

} // namespace tessera
