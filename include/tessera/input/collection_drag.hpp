#pragma once

#include <tessera/input/pointer.hpp>
#include <string>

namespace tessera {

// Where a dragged item would land relative to the row `key`.
enum class DropPlacement : std::uint8_t { before, after };
struct CollectionDropTarget {
    std::string key;
    DropPlacement placement = DropPlacement::before;
    bool operator==(const CollectionDropTarget&) const = default;
};

// The insertion point in the ListView or TreeView with stable ID `id` for a root-logical position
// inside its visible (clipped) border box: the displayed <id>-item-<key> row containing the
// position, else the vertically nearest one, before its center or after it. Positions outside the
// collection, a hidden or disabled collection, or one without displayed rows return no target.
Result<std::optional<CollectionDropTarget>> collection_drop_target(const HitTestInput&, std::string_view id,
                                                                   Point position);

struct CollectionDragOptions {
    float threshold = 4; // Logical units a press travels before it drags; shorter presses stay clicks.
    bool operator==(const CollectionDragOptions&) const = default;
};

struct CollectionDragDispatchResult {
    bool consumed = false;
    bool started = false;                       // Reset ordinary pointer presses so the release activates nothing.
    std::optional<std::string> source;          // The dragged item's key while dragging.
    std::optional<CollectionDropTarget> target; // The current insertion point while dragging.
    std::optional<CollectionDropTarget> drop;   // Released over `target`: request the host's move command.
    bool ended = false;                         // The drag ended with this event or replacement.
    bool canceled = false;                      // Ended without a drop.
    bool operator==(const CollectionDragDispatchResult&) const = default;
};

// Host-owned single-pointer item dragging within one ListView or TreeView, identified by its stable
// author ID. Dispatch before ordinary pointer routing. A primary press on an eligible item row is
// observed, not consumed, so ordinary press, focus and click activation continue. Once the pointer
// has travelled `threshold` units from the press, the drag starts: moves report the insertion point
// under the pointer and the primary release reports it as `drop`. Escape or logical Cancel and
// matching PointerCancel cancel a drag; other keyboard, text and logical input cancels it
// unconsumed. Scroll, KeyUp and other pointers pass through; refresh recomputes the insertion point
// at the last pointer position. The dispatcher never moves items: the host validates the drop and
// executes its own command. Retains only the pointer, the item key and pointer positions.
class CollectionDragDispatcher final {
public:
    explicit CollectionDragDispatcher(std::string id, CollectionDragOptions options = {})
        : id_(std::move(id)), options_(options) {}
    Result<CollectionDragDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Call at every accepted replacement. Ends the drag when the collection or the dragged item
    // disappears or becomes ineligible; otherwise reports the insertion point at the last position.
    Result<CollectionDragDispatchResult> refresh(const HitTestInput&);
    bool pressed() const noexcept { return drag_.has_value(); }
    bool dragging() const noexcept { return drag_ && drag_->dragging; }
    std::optional<std::string> source() const { return dragging() ? std::optional{drag_->key} : std::nullopt; }
    void reset() noexcept { drag_.reset(); }

private:
    struct Drag {
        PointerId pointer;
        std::string key;
        Point origin;
        Point position;
        bool dragging = false;
    };
    std::string id_;
    CollectionDragOptions options_;
    std::optional<Drag> drag_;
};

} // namespace tessera
