#pragma once

#include <tessera/input/focus.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/draw_list.hpp>
#include <tessera/semantics/semantics.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tessera {

// Prototype in-process recording; not a serialized format or stable API. The replay version is
// distinct from document and semantic versions, and only the prototype version is accepted.
inline constexpr std::uint32_t replay_prototype_version = 0;
inline constexpr std::size_t max_replay_steps = 10000;

// New logical viewport applied at an update point.
struct ReplayResize {
    Size viewport;
    bool operator==(const ReplayResize&) const = default;
};
// Replaces the document at an update point. styles[i] belongs to the new tree's preorder node i.
// The new tree has a new identity, so pointer state is cleared as by snapshot replacement.
struct ReplayReload {
    UiDocument document;
    std::vector<ResolvedStyle> styles;
    bool operator==(const ReplayReload&) const = default;
};
// Host programmatic focus. The target is resolved by resolve_target in the current generation and focused
// through FocusDispatcher::focus.
struct ReplayFocus {
    InspectionTarget target;
};
// Semantic action invocation, e.g. from an accessibility or automation client. The target is resolved by
// resolve_target in the current generation and invoked through request_semantic_action.
struct ReplaySemanticAction {
    InspectionTarget target;
    std::string binding = "activate";
};
// Input steps must be pointer, scroll, or logical focus commands (FocusNext/FocusPrevious/Navigate/Activate/
// Cancel); they share one non-decreasing timestamp stream. A scroll step is routed by route_scroll and
// applied at an update point; a command goes through FocusDispatcher. Keys are host-translated, not replayed.
// Focus and semantic action steps are untimed.
using ReplayStep = std::variant<InputEvent, ReplayResize, ReplayReload, ReplayFocus, ReplaySemanticAction>;

// Host focus policies that playback applies on the host's behalf; both are off by default.
struct ReplayFocusPolicy {
    bool press_focus = false;  // A primary press that records a focusable press owner also focuses it.
    bool reveal_focus = false; // Focus moved to another node by a command or focus step is scrolled into view.
};

struct ReplayRecording {
    std::uint32_t version = replay_prototype_version;
    ValidationContext context; // Host action names declared for every document.
    ReplayFocusPolicy policy;
    Size viewport;
    UiDocument document;
    std::vector<ResolvedStyle> styles; // One per node in tree preorder; no stylesheet resolution.
    std::vector<ReplayStep> steps;
};

// Observations own values and fixture identities only; no NodeHandle or tree identity escapes.
struct ReplayBox {
    std::uint32_t node = 0; // Tree preorder index within its generation.
    std::uint32_t parent = no_layout_parent;
    Rect border_box;
    Edges border;
    Edges padding;
    bool visible = true;
    std::optional<Rect> clip;
    std::optional<ScrollGeometry> scroll;
    bool operator==(const ReplayBox&) const = default;
};
// Full-tree layout and paint for one snapshot: the initial one, one per resize/reload/scroll step, and one per
// focus reveal that changes an offset.
struct ReplayGeneration {
    std::optional<std::size_t> step; // Producing step; absent for the initial snapshot.
    std::vector<ReplayBox> boxes;
    UiDrawList paint;
    bool operator==(const ReplayGeneration&) const = default;
};
// Owned semantic entry; preorder indices replace the snapshot's handles.
struct ReplaySemanticNode {
    std::uint32_t node = 0;                    // Tree preorder index within its generation.
    std::optional<std::string> id;             // Author ID, when present.
    std::uint32_t parent = no_semantic_parent; // Index into ReplaySemantics::nodes.
    SemanticRole role = SemanticRole::root;
    std::string name;
    NameSource name_source = NameSource::none;
    std::optional<std::uint32_t> labelled_by; // Referenced node's preorder index.
    bool enabled = true;
    bool focusable = false;
    bool focused = false;
    std::vector<SemanticAction> actions;
    bool operator==(const ReplaySemanticNode&) const = default;
};
// Semantic projection of one generation with the focus held at an observation point: after each generation
// settles, after each command or focus step, and after a pointer step that moves focus.
struct ReplaySemantics {
    std::optional<std::size_t> step; // Observing step; absent for the initial snapshot.
    std::size_t generation = 0;
    std::optional<std::uint32_t> focused; // Focused node's preorder index, whether or not it has an entry.
    std::vector<ReplaySemanticNode> nodes;
    bool operator==(const ReplaySemantics&) const = default;
};
struct ReplayAction {
    std::size_t step = 0;       // Input or semantic action step whose dispatch requested it.
    std::size_t generation = 0; // Snapshot the request's target belongs to.
    std::string binding;
    std::string action;
    std::uint32_t node = 0;        // Binding owner's tree preorder index.
    std::optional<std::string> id; // Binding owner's author ID, when it has one.
    bool operator==(const ReplayAction&) const = default;
};
struct ReplayOutput {
    std::vector<ReplayGeneration> generations;
    std::vector<ReplaySemantics> semantics; // In observation order.
    std::vector<ReplayAction> actions;      // In request order.
    bool operator==(const ReplayOutput&) const = default;
};

// Plays the steps through UiTree::create, compute_layout, build_paint_list, PointerDispatcher,
// FocusDispatcher, scroll routing, build_semantic_tree, and inspection target resolution, with the
// host-injected shaper. Failures are located under the
// recording ("" for the initial snapshot, /steps/<index> for a step) and return no partial output; semantic
// warnings are located the same way and returned with the output. Renderer-free.
Result<ReplayOutput> play_replay(const ReplayRecording&, TextShaper&);

namespace detail {
class ReplayPlayer;
}

// Incremental playback for in-process consumers and controlled host fixtures, without a window or renderer.
// It plays a recording as play_replay does and then accepts further steps one at a time; recording() holds
// the accepted steps, so replaying it reproduces output(). A step rejected by validation, ordering, target
// resolution, or eligibility leaves the session unchanged and unrecorded; a failure after a step has changed
// the session closes it, and later calls fail as closed_session.
class ReplaySession final {
public:
    // Fails, with no session, as play_replay fails; warnings accompany the session.
    static Result<ReplaySession> open(ReplayRecording, TextShaper&);
    ReplaySession(ReplaySession&&) noexcept;
    ReplaySession& operator=(ReplaySession&&) noexcept;
    ~ReplaySession();

    // Applies the step as /steps/<n>, n being the number of accepted steps, and returns n with the step's
    // semantic warnings. Diagnostics are located as for play_replay.
    Result<std::size_t> apply(ReplayStep);
    // Focus or invoke a node by a handle from the current tree, e.g. one resolved in capture(). The step is
    // recorded with the node's author ID, else its child-index path. A handle from another tree is
    // stale_target at /steps/<n>/target.
    Result<std::size_t> focus(NodeHandle target);
    Result<std::size_t> invoke(NodeHandle target, std::string binding = "activate");
    // Coherent inspection of the current generation with the current focus; its handles stay valid until a
    // reload replaces the tree. `sources` must describe the current document.
    Result<InspectionSnapshot> capture(const DocumentSourceMap* sources = nullptr) const;

    bool closed() const noexcept;
    const ReplayRecording& recording() const noexcept;
    const ReplayOutput& output() const noexcept;

private:
    explicit ReplaySession(std::unique_ptr<detail::ReplayPlayer>);
    std::unique_ptr<detail::ReplayPlayer> player_;
};

// One replay_mismatch diagnostic per difference, located in the expected output. Box rectangles,
// edges, and scroll geometry match within geometry_tolerance logical units; all other values, including
// semantic observations, must be equal.
std::vector<Diagnostic> compare_replay(const ReplayOutput& expected, const ReplayOutput& actual,
                                       float geometry_tolerance = 0);

} // namespace tessera
