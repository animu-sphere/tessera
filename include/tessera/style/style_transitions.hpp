#pragma once

#include <tessera/style/resolved_style.hpp>
#include <tessera/ui/tree.hpp>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace tessera {

// Host-owned presentation state that eases the paint-only fields named by each target style's
// `transition` from what was presented toward a newly accepted resolved generation.
//
// Time is the host's clock in seconds, the same clock and origin it passes to every call; the host
// owns pausing and scheduling. Layout, input targeting, semantics and inspection keep consuming the
// target styles; only paint consumes presented ones. Output is a pure function of the accepted
// targets and the times passed in, so equal inputs present equal styles.
class StyleTransitions {
public:
    // Accepts the resolved styles of a new generation (one per node by NodeHandle::index) at `now`.
    // Nodes match the previous generation by author ID. A field the target's transition names starts
    // a transition when its target value changed, from the value presented at `now`, so an
    // interrupted transition retargets without a jump. Nodes without an ID or without a match, and
    // fields the transition does not name, present their target immediately.
    // Rejects without changing state for a style count that differs from the tree's node count
    // (`transition_style_count` at /styles), a non-finite time or one earlier than the previous
    // accepted update (`transition_time` at /now), or an invalid target style (/styles/<index>).
    std::vector<Diagnostic> update(const UiTree&, std::span<const ResolvedStyle> targets, double now);

    // Presented styles at `now` for the last accepted generation, by NodeHandle::index. Equal to the
    // targets once every transition has ended or before any update. Times before a transition's start
    // present its start value; non-finite times present the targets.
    std::vector<ResolvedStyle> present(double now) const;

    // Host time when the last transition ends; empty when none was started by the last updates.
    std::optional<double> settles_at() const;
    // Whether a presentation at `now` differs from the targets, i.e. the host should schedule a frame.
    bool running(double now) const;

    // Ends every transition: later presentation returns the targets until the next change.
    void settle();
    // Forgets every node, so the next update presents its targets immediately.
    void clear();

private:
    static constexpr std::size_t property_count = 7;
    struct Track {
        double start = 0;
        double duration = 0;
        Easing easing = Easing::linear;
    };
    struct Node {
        ResolvedStyle from; // Start values of the tracked fields.
        std::array<std::optional<Track>, property_count> tracks;
    };
    std::vector<ResolvedStyle> targets_;
    // By NodeHandle::index of the accepted tree; empty for nodes without running tracks.
    std::vector<std::optional<Node>> nodes_;
    std::unordered_map<std::string, std::size_t> ids_; // Author ID to index in the accepted tree.
    std::optional<double> last_;
};

} // namespace tessera
