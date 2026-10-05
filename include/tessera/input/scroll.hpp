#pragma once

#include <tessera/input/pointer.hpp>
#include <optional>
#include <vector>

namespace tessera {

// New requested offset for one scroll box. The host stores it in LayoutInput::scroll_offsets and recomputes
// layout at its next update point; Tessera keeps no scroll state between snapshots.
struct ScrollUpdate {
    NodeHandle container;
    Point offset;
    bool operator==(const ScrollUpdate&) const = default;
};

struct ScrollRouteResult {
    std::optional<NodeHandle> target;  // Hit at the event's position.
    std::vector<ScrollUpdate> updates; // Innermost scroll box first; only boxes whose offset changes.
    Point remaining;                   // Delta that no scroll box on the target's ancestry consumed.
    bool operator==(const ScrollRouteResult&) const = default;
};

// Routes a Scroll event from its hit target toward the root. Each axis's delta moves the eligible scroll
// boxes in turn, each up to its limits, and continues outward with what is left. Stateless.
Result<ScrollRouteResult> route_scroll(const HitTestInput&, const Scroll&);

// Offsets that reveal the target's border box in each scroll box on its ancestry, innermost first, moving
// each as little as possible. A box larger than a viewport aligns its leading edge. Visibility, disabled
// state, and focusability do not matter; display-none targets have no geometry and are rejected.
Result<std::vector<ScrollUpdate>> scroll_into_view(const HitTestInput&, NodeHandle target);

} // namespace tessera
