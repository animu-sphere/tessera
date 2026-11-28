#pragma once

#include <tessera/input/pointer.hpp>
#include <string>
#include <vector>

namespace tessera {

// What a drop does with the dragged payload. Tessera never applies it: the host's command does.
enum class DragOperation : std::uint8_t { move, copy, link };

struct DragOperations {
    bool move = false;
    bool copy = false;
    bool link = false;
    bool contains(DragOperation) const noexcept;
    bool empty() const noexcept { return !move && !copy && !link; }
    bool operator==(const DragOperations&) const = default;
};

// The operation a drop performs among `allowed`, as Windows modifiers request it: Control+Shift
// requests link, Control copy and Shift move; otherwise the first allowed of move, copy and link.
// Other modifiers are ignored. A requested operation outside `allowed` returns none.
std::optional<DragOperation> drag_operation(DragOperations allowed, Modifiers) noexcept;

// A node that starts drags of one typed payload. The host keeps the payload value; Tessera sees only
// its type ID, the allowed operations and the source's stable author ID.
struct DragSource {
    std::string id;   // A primary press on this node or a descendant starts the drag.
    std::string type; // MIME-like payload type ID: lowercase "type/subtype", such as "application/x-color".
    DragOperations operations;
    bool operator==(const DragSource&) const = default;
};

// A node that accepts drops of the listed payload types with the listed operations.
struct DropTarget {
    std::string id;
    std::vector<std::string> types;
    DragOperations operations;
    bool operator==(const DropTarget&) const = default;
};

// The sources and targets one snapshot presents. IDs absent from the snapshot are ignored, so the
// host can list every possible participant.
struct DragDropScope {
    std::vector<DragSource> sources;
    std::vector<DropTarget> targets;
    bool operator==(const DragDropScope&) const = default;
};

std::vector<Diagnostic> validate(const DragDropScope&);

// An accepting target under the pointer and the operation a drop there performs.
struct DragOver {
    std::string target;
    DragOperation operation = DragOperation::move;
    bool operator==(const DragOver&) const = default;
};

// The nearest eligible target on the ancestry of the topmost pointer target at a root-logical
// position that accepts `source`'s type with an operation `drag_operation` selects for the
// modifiers. Targets that do not accept are skipped, so an enclosing target can accept instead.
Result<std::optional<DragOver>> drop_target_at(const HitTestInput&, const DragDropScope&, const DragSource& source,
                                               Point position, Modifiers = {});

struct DragDropOptions {
    float threshold = 4; // Logical units a press travels before it drags; shorter presses stay clicks.
    bool operator==(const DragDropOptions&) const = default;
};

struct DragDropDispatchResult {
    bool consumed = false;
    bool started = false;             // Reset ordinary pointer presses so the release activates nothing.
    std::optional<DragSource> source; // The dragged source while dragging and at its end.
    std::optional<Point> position;    // The pointer while dragging, for host-drawn previews.
    std::optional<DragOver> target;   // The accepting target under the pointer while dragging.
    std::optional<DragOver> drop;     // Released over `target`: request the host's command for it.
    bool ended = false;               // The drag ended with this event or replacement.
    bool canceled = false;            // Ended without a drop.
    bool operator==(const DragDropDispatchResult&) const = default;
};

// Host-owned single-pointer typed drag and drop between the sources and targets of a scope, passed
// with every call so it can follow each replacement. Dispatch before ordinary pointer routing. A
// primary press on an eligible source (the innermost when sources nest) is observed, not consumed,
// so ordinary press, focus and click activation continue. Once the pointer has travelled
// `threshold` units, the drag starts: moves report the accepting target under the pointer with the
// operation their modifiers select, and the primary release reports the release's target as
// `drop`, or cancels where nothing accepts. Escape or logical Cancel and matching PointerCancel
// cancel a drag; other keyboard, text and logical input cancels it unconsumed. Scroll, KeyUp,
// secondary buttons and other pointers pass through. The dispatcher never applies payloads.
// Retains only the pointer, the source, pointer positions and the last modifiers.
class DragDropDispatcher final {
public:
    explicit DragDropDispatcher(DragDropOptions options = {}) : options_(options) {}
    Result<DragDropDispatchResult> dispatch(const HitTestInput&, const DragDropScope&, const InputEvent&);
    // Call at every accepted replacement. Ends the drag when its source disappears, becomes
    // ineligible or leaves the scope; otherwise reports the target at the last position.
    Result<DragDropDispatchResult> refresh(const HitTestInput&, const DragDropScope&);
    bool pressed() const noexcept { return drag_.has_value(); }
    bool dragging() const noexcept { return drag_ && drag_->dragging; }
    std::optional<DragSource> source() const { return dragging() ? std::optional{drag_->source} : std::nullopt; }
    void reset() noexcept { drag_.reset(); }

private:
    struct Drag {
        PointerId pointer;
        DragSource source;
        Point origin;
        Point position;
        Modifiers modifiers;
        bool dragging = false;
    };
    DragDropOptions options_;
    std::optional<Drag> drag_;
};

} // namespace tessera
