#pragma once

#include <tessera/input/pointer.hpp>
#include <string>

namespace tessera {

// Pointer caret placement and drag selection over TextInput and TextEditor fields. The dispatcher
// owns pointer capture only; the host maps each reported point to a caret offset with the caret
// layout of the field's <id>-value Text and executes the selection through the field's command.

// The eligible, focusable text_input or text_editor owner under `position` when the topmost pointer
// hit is the owner or one of its generated parts other than <id>-label, whose press keeps ordinary
// label focusing. Other controls and misses return no target.
Result<std::optional<NodeHandle>> text_pointer_target(const HitTestInput&, Point position);

struct TextPointerDispatchResult {
    bool consumed = false;
    std::optional<NodeHandle> input; // The captured owner in this snapshot.
    std::optional<NodeHandle> focus; // Apply through FocusDispatcher in this snapshot.
    std::optional<NodeHandle> value; // The owner's <id>-value Text, whose caret layout maps `point`.
    std::optional<Point> point;      // Root-logical position to place the caret at.
    bool extend = false;             // Keep the observed anchor; otherwise collapse the selection at `point`.
    bool ended = false;              // Capture ended with this event or replacement.
    bool operator==(const TextPointerDispatchResult&) const = default;
};

// Host-owned single-pointer capture for every text field in a snapshot. Dispatch before ordinary
// pointer routing. A primary press on a target captures the pointer, focuses the field and reports
// its position, extending the selection when Shift is held. Each move of the captured pointer and
// its primary release report the position with `extend`, so the caret follows the pointer while the
// press position stays the anchor; positions outside the field still report, and caret lookup clamps
// them to its rows. The host skips a selection equal to the observed one, and does not request one
// while a composition is open. PointerCancel ends capture keeping the last selection. Keyboard, text
// and logical events end capture unconsumed; KeyUp, Scroll and other pointers pass through. Retains
// only the pointer and the owner's author ID and command across replacements.
class TextPointerDispatcher final {
public:
    Result<TextPointerDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Call at every accepted replacement. Ends capture (`ended`) when the owner disappeared, changed
    // command, stopped being a text field or became ineligible.
    Result<TextPointerDispatchResult> refresh(const HitTestInput&);
    bool pressed() const noexcept { return drag_.has_value(); }
    // The captured owner's author ID, if any.
    std::optional<std::string> captured() const { return drag_ ? std::optional{drag_->id} : std::nullopt; }
    // Ends capture without a report, for example after the host rejected a selection.
    void reset() noexcept { drag_.reset(); }

private:
    struct Drag {
        PointerId pointer;
        std::string id;
        std::string action;
    };
    std::optional<NodeHandle> reconcile(const HitTestInput&, TextPointerDispatchResult&);
    std::optional<Drag> drag_;
};

} // namespace tessera
