#pragma once

#include <tessera/input/pointer.hpp>
#include <tessera/render/draw_list.hpp>
#include <cstddef>
#include <cstdint>
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
// Input steps must be pointer events; they share one non-decreasing timestamp stream.
using ReplayStep = std::variant<InputEvent, ReplayResize, ReplayReload>;

struct ReplayRecording {
    std::uint32_t version = replay_prototype_version;
    ValidationContext context; // Host action names declared for every document.
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
    bool operator==(const ReplayBox&) const = default;
};
// Full-tree layout and paint for one snapshot: the initial one and one per resize/reload step.
struct ReplayGeneration {
    std::optional<std::size_t> step; // Producing step; absent for the initial snapshot.
    std::vector<ReplayBox> boxes;
    UiDrawList paint;
    bool operator==(const ReplayGeneration&) const = default;
};
struct ReplayAction {
    std::size_t step = 0;       // Input step whose dispatch requested it.
    std::size_t generation = 0; // Snapshot the request's target belongs to.
    std::string binding;
    std::string action;
    std::uint32_t node = 0;        // Binding owner's tree preorder index.
    std::optional<std::string> id; // Binding owner's author ID, when it has one.
    bool operator==(const ReplayAction&) const = default;
};
struct ReplayOutput {
    std::vector<ReplayGeneration> generations;
    std::vector<ReplayAction> actions; // In request order.
    bool operator==(const ReplayOutput&) const = default;
};

// Plays the steps through UiTree::create, compute_layout, build_paint_list, and PointerDispatcher,
// with the host-injected shaper. Failures are located under the recording ("" for the initial
// snapshot, /steps/<index> for a step) and return no partial output. Renderer-free.
Result<ReplayOutput> play_replay(const ReplayRecording&, TextShaper&);

// One replay_mismatch diagnostic per difference, located in the expected output. Box rectangles
// and edges match within geometry_tolerance logical units; all other values must be equal.
std::vector<Diagnostic> compare_replay(const ReplayOutput& expected, const ReplayOutput& actual,
                                       float geometry_tolerance = 0);

} // namespace tessera
