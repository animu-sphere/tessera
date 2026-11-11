#pragma once

#include <tessera/input/pointer.hpp>
#include <string>

namespace tessera {

// Horizontal pointer scrubbing over NumberInput fields and VectorInput components.
struct NumberDragOptions {
    float threshold = 3; // Horizontal logical units a press travels before it scrubs; shorter presses only focus.
    float distance = 4;  // Horizontal logical units per step.
    bool operator==(const NumberDragOptions&) const = default;
};

// The eligible, focusable number_input owner under `position` when the topmost pointer hit is the
// owner itself or lies in its generated <id>-label or <id>-field part. Step parts, other controls and
// misses return no target.
Result<std::optional<NodeHandle>> number_drag_target(const HitTestInput&, Point position);

struct NumberDragDispatchResult {
    bool consumed = false;
    std::optional<NodeHandle> input;     // The captured number_input owner in this snapshot.
    std::optional<NodeHandle> focus;     // Apply through FocusDispatcher in this snapshot.
    bool started = false;                // The press became a scrub: record the origin value first.
    std::optional<int> steps;            // Whole steps from the origin value; execute `action` with them.
    std::optional<ActionRequest> action; // The input's own command for `steps`.
    bool ended = false;                  // Capture ended with this event or replacement.
    bool canceled = false;               // Escape/Cancel during a scrub; any `steps` are 0, the origin.
    bool operator==(const NumberDragDispatchResult&) const = default;
};

// Host-owned single-pointer capture for every number_input in a snapshot. Dispatch before ordinary
// pointer routing. A primary press on a drag target captures the pointer and focuses the input; once
// the pointer has moved `threshold` units horizontally from the press, moves and the release request
// whole steps, truncated toward zero, of `distance` units from the press position. A request is made
// only when the step count changes; the host maps it to its own value from the origin it recorded
// at `started`. Escape or logical Cancel while scrubbing requests 0 steps, restoring the origin, and
// ends capture. PointerCancel ends capture keeping the last request. Other keyboard, text and logical
// events end capture unconsumed; KeyUp, Scroll and other pointers pass through. Retains only the
// pointer, the owner's author ID and command and the press position across replacements.
class NumberDragDispatcher final {
public:
    explicit NumberDragDispatcher(NumberDragOptions options = {}) : options_(options) {}
    Result<NumberDragDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Call at every accepted replacement. Ends capture (`ended`) when the owner disappeared, changed
    // command, stopped being a number_input or became ineligible; the last request stays applied.
    Result<NumberDragDispatchResult> refresh(const HitTestInput&);
    bool pressed() const noexcept { return drag_.has_value(); }
    bool scrubbing() const noexcept { return drag_ && drag_->scrubbing; }
    // The captured owner's author ID, if any.
    std::optional<std::string> captured() const { return drag_ ? std::optional{drag_->id} : std::nullopt; }
    // Ends capture without a request, for example after the host rejected a step.
    void reset() noexcept { drag_.reset(); }

private:
    struct Drag {
        PointerId pointer;
        std::string id;
        std::string action;
        float origin = 0;
        int steps = 0;
        bool scrubbing = false;
    };
    std::optional<NodeHandle> reconcile(const HitTestInput&, NumberDragDispatchResult&);
    NumberDragOptions options_;
    std::optional<Drag> drag_;
};

} // namespace tessera
