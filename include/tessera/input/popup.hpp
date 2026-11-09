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

struct MenuDispatchResult {
    bool dismiss = false;
    bool consumed = false;
    std::optional<NodeHandle> focus;
    std::optional<Point> open_at; // Host publishes an open ContextMenu at this root-logical position.
    bool operator==(const MenuDispatchResult&) const = default;
};

// Single-level Menu/ContextMenu routing before ordinary pointer/focus dispatch.
// Item activation stays on the ordinary command path; close after accepted execution.
class MenuDispatcher final {
public:
    explicit MenuDispatcher(std::string id) : id_(std::move(id)), popup_(id_) {}
    Result<MenuDispatchResult> refresh(const HitTestInput&);
    Result<MenuDispatchResult> dispatch(const HitTestInput&, const InputEvent&, std::optional<NodeHandle> focused = {});

private:
    std::string id_;
    PopupDispatcher popup_;
};

struct ComboBoxDispatchResult {
    bool dismiss = false; // Host closes the list and publishes the closed view.
    bool consumed = false;
    std::optional<NodeHandle> focus; // Apply through FocusDispatcher in this snapshot.
    bool open = false;               // Host opens the list, as its toggle command would.
    bool operator==(const ComboBoxDispatchResult&) const = default;
};

// One select-only ComboBox, identified by its trigger's stable author ID; its list is <id>-list.
// Refresh on every accepted snapshot and dispatch before ordinary pointer/focus routing. Opening
// requests the selected option's focus, else the first eligible option. Option and trigger
// activation stay on the ordinary command path; close after an accepted selection.
class ComboBoxDispatcher final {
public:
    explicit ComboBoxDispatcher(std::string id) : id_(std::move(id)), popup_(id_ + "-list") {}
    Result<ComboBoxDispatchResult> refresh(const HitTestInput&);
    Result<ComboBoxDispatchResult> dispatch(const HitTestInput&, const InputEvent&,
                                            std::optional<NodeHandle> focused = {});

private:
    std::string id_;
    PopupDispatcher popup_;
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
