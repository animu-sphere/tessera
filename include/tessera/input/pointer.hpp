#pragma once

#include <tessera/input/event.hpp>
#include <tessera/layout/layout_box.hpp>
#include <map>
#include <optional>
#include <span>

namespace tessera {

// Borrowed for a query/dispatch; use a coherent tree/styles/layout snapshot.
struct HitTestInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    const LayoutResult* layout = nullptr;
};

struct HitTestResult {
    std::optional<NodeHandle> target;
    bool operator==(const HitTestResult&) const = default;
};

std::vector<Diagnostic> validate(const HitTestInput&);
// Reverse preorder, half-open border boxes. Visibility is local; disabled is inherited.
// Transparent/zero-opacity boxes remain eligible. No implicit clips or transforms.
Result<HitTestResult> hit_test(const HitTestInput&, Point position);

struct PointerState {
    PointerId pointer;
    Point position;
    std::optional<NodeHandle> hovered; // Deepest/topmost eligible hit.
    std::optional<NodeHandle> pressed; // First eligible activate binding on the press's ancestry.
    std::optional<NodeHandle> active;  // pressed when current binding owner still matches.
    bool primary_down = false;
    bool operator==(const PointerState&) const = default;
};

struct PointerDispatchResult {
    std::optional<NodeHandle> target; // Hit at the event's position; absent for refresh/cancel.
    std::vector<ActionRequest> actions;
    std::vector<PointerState> pointers; // Sorted by PointerId; handles belong to the supplied tree.
    bool operator==(const PointerDispatchResult&) const = default;
};

// Host-owned, single-stream state. Stores values/handles only, never callbacks or borrowed inputs.
class PointerDispatcher final {
public:
    // Accepts only PointerMove/Down/Up/Cancel. Invalid input leaves state unchanged.
    // Primary release activates only the original binding owner when it still matches the hit.
    Result<PointerDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Re-target stationary pointers and cancel ineligible presses after snapshot changes.
    // A different tree identity clears all pointer state. No action is requested by refresh.
    Result<PointerDispatchResult> refresh(const HitTestInput&);
    void reset() noexcept;

private:
    std::map<std::uint32_t, PointerState> pointers_;
    std::uint64_t tree_ = 0;
    std::optional<std::chrono::microseconds> timestamp_;
};

} // namespace tessera
