#pragma once

#include <tessera/input/event.hpp>
#include <tessera/layout/layout_box.hpp>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace tessera {

namespace detail {
class Targeting;
}

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
// Reverse paint order (preorder with siblings ordered by z_index), half-open border boxes. Visibility is local; disabled is inherited.
// An active Modal excludes all nodes outside its subtree.
// Transparent/zero-opacity boxes remain eligible. pointer_events:false excludes a
// subtree from pointer hits only. No implicit clips or transforms.
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

enum class PointerSnapshotPolicy : std::uint8_t { tree_identity, compatible_author_ids };

// Host-owned, single-stream state. Stores owned values only, never callbacks or borrowed inputs.
class PointerDispatcher final {
public:
    PointerDispatcher() = default;
    explicit PointerDispatcher(PointerSnapshotPolicy policy) : policy_(policy) {}

    // Accepts only PointerMove/Down/Up/Cancel. Invalid input leaves state unchanged.
    // Primary release activates only the original binding owner when it still matches the hit.
    Result<PointerDispatchResult> dispatch(const HitTestInput&, const InputEvent&);
    // Re-target stationary pointers and cancel ineligible presses after snapshot changes.
    // By default a different tree identity clears all pointer state. compatible_author_ids
    // re-resolves eligible press owners with the same author ID, kind and activation binding.
    // No action is requested by refresh.
    Result<PointerDispatchResult> refresh(const HitTestInput&);
    void reset() noexcept;

private:
    struct Press {
        std::optional<std::string> id;
        std::string action;
        NodeKind kind;
    };
    struct StoredPointer {
        PointerState state;
        std::optional<Press> press;
    };
    void reconcile(const HitTestInput&, const detail::Targeting&);
    PointerDispatchResult snapshot() const;

    PointerSnapshotPolicy policy_ = PointerSnapshotPolicy::tree_identity;
    std::map<std::uint32_t, StoredPointer> pointers_;
    std::uint64_t tree_ = 0;
    std::optional<std::chrono::microseconds> timestamp_;
};

} // namespace tessera
