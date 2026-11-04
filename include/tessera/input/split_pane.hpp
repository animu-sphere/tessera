#pragma once

#include <tessera/input/pointer.hpp>
#include <string>

namespace tessera {

struct SplitPaneGeometry {
    NodeHandle handle;
    Rect bounds;
    Point first_origin;
    FlexDirection direction = FlexDirection::row;
    float size = 0, minimum = 0, maximum = 0;
    bool enabled = false;
};
// Missing/hidden generated parts mean the control is absent, not an error.
Result<std::optional<SplitPaneGeometry>> split_pane_geometry(const HitTestInput&, std::string_view id);

struct SplitPaneDispatchResult {
    bool handled = false;
    std::optional<float> size;
    std::optional<ActionRequest> action;
    std::optional<NodeHandle> focus;
};

// Host-owned single-handle capture. Retains only pointer identity, grab offset
// and binding values across tree replacements, never borrowed tree handles.
class SplitPaneDispatcher final {
public:
    explicit SplitPaneDispatcher(std::string id, float keyboard_step = 8) : id_(std::move(id)), step_(keyboard_step) {}
    Result<SplitPaneDispatchResult> dispatch(const HitTestInput&, const InputEvent&,
                                             std::optional<NodeHandle> focused = {});
    // Call at every accepted replacement, including removal. Returns the
    // resolved first-pane size for the host's canonical state.
    Result<SplitPaneDispatchResult> refresh(const HitTestInput&);
    bool dragging() const noexcept { return drag_.has_value(); }
    void reset() noexcept { drag_.reset(); }

private:
    struct Drag {
        PointerId pointer;
        float grab;
        FlexDirection direction;
        float handle_size;
        std::string action;
    };
    std::string id_;
    float step_;
    std::optional<Drag> drag_;
};
} // namespace tessera
