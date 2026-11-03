#pragma once

#include <tessera/input/focus.hpp>
#include <utility>

namespace tessera {

struct PopupDispatchResult {
    bool dismiss = false; // Host closes the controlled Popup at its next update.
    bool consumed = false;
    std::optional<NodeHandle> focus; // Apply through FocusDispatcher in this snapshot.
    bool operator==(const PopupDispatchResult&) const = default;
};

// One non-modal popup, identified by a stable author ID. Refresh on every accepted
// snapshot, including closed ones. Dispatch before ordinary pointer/focus routing.
class PopupDispatcher final {
public:
    explicit PopupDispatcher(std::string id) : id_(std::move(id)) {}
    Result<PopupDispatchResult> refresh(const HitTestInput&);
    Result<PopupDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
private:
    PopupDispatchResult update(const HitTestInput&);
    std::string id_;
    std::optional<std::string> anchor_;
    bool open_ = false;
    std::optional<std::chrono::microseconds> timestamp_;
};

struct TooltipDispatchResult {
    bool visible = false; // Host passes this to Tooltip's controlled open prop.
    bool operator==(const TooltipDispatchResult&) const = default;
};

// Immediate hover OR keyboard/gamepad focus, including anchor descendants.
// Cancel/Escape or a press suppresses it until both hover and focus have left.
class TooltipDispatcher final {
public:
    explicit TooltipDispatcher(std::string anchor) : anchor_(std::move(anchor)) {}
    Result<TooltipDispatchResult> refresh(const HitTestInput&, std::optional<NodeHandle> focused = {});
    Result<TooltipDispatchResult> dispatch(const HitTestInput&, const InputEvent&,
        std::optional<NodeHandle> focused = {});
private:
    TooltipDispatchResult update(const HitTestInput&, std::optional<NodeHandle>);
    std::string anchor_;
    std::optional<Point> pointer_;
    std::optional<PointerId> pointer_id_;
    bool suppressed_ = false;
    std::optional<std::chrono::microseconds> timestamp_;
};

} // namespace tessera
