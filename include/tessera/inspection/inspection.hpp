#pragma once

#include <tessera/semantics/semantics.hpp>
#include <tessera/ui/serialization.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace tessera {

// Prototype in-process inspection; not a wire format, CLI, or stable API.

inline constexpr std::uint32_t no_inspection_parent = std::numeric_limits<std::uint32_t>::max();

// Effective value of one descriptor accepted by the element's kind, in descriptor order.
struct PropertyObservation {
    std::string name;
    std::optional<Property> value; // Authored value, else the absence value; empty when neither exists.
    bool authored = false;
    bool operator==(const PropertyObservation&) const = default;
};

// Layout observation of a displayed element, in root logical coordinates.
struct GeometryObservation {
    Rect border_box;
    Rect padding_box;
    Rect content_box;
    bool visible = true;
    std::optional<Rect> clip; // Recorded ancestor clip; absent when unclipped.
    std::optional<ScrollGeometry> scroll; // Present for scroll boxes.
    bool operator==(const GeometryObservation&) const = default;
};

// One authored node. Element i corresponds to tree preorder index i (NodeHandle::index).
struct InspectionElement {
    std::uint32_t parent = no_inspection_parent; // Runtime hierarchy, including display-none nodes.
    std::vector<std::uint32_t> children;          // Authored order.
    NodeKind kind = NodeKind::box;
    std::optional<std::string> id; // Document-scoped author ID.
    std::vector<std::string> classes;
    std::map<std::string, std::string, std::less<>> events;
    std::vector<PropertyObservation> properties;
    ResolvedStyle style;
    std::optional<GeometryObservation> geometry; // Absent for display-none nodes and their subtrees.
    std::optional<std::uint32_t> semantic;       // Index into InspectionSnapshot::semantics.nodes.
    std::optional<NodeSource> source;            // Absent when no source map was supplied.
    bool operator==(const InspectionElement&) const = default;
};

enum class SourceStatus : std::uint8_t { not_supplied, mapped };

// Owned observation of one coherent tree/style/layout generation. It borrows nothing; handles built from
// `generation` fail against any other tree, even when author IDs are reused.
struct InspectionSnapshot {
    std::uint64_t generation = 0; // Tree identity (NodeHandle::tree) of the captured snapshot.
    std::vector<InspectionElement> elements;
    SemanticTree semantics;
    SourceStatus sources = SourceStatus::not_supplied;
    std::string source_file; // Empty unless sources are mapped.
    std::vector<Diagnostic> diagnostics; // Warnings from the captured generation.
    bool operator==(const InspectionSnapshot&) const = default;
};

// Borrowed for one capture; use a coherent tree/styles/layout snapshot. `sources` is optional and must
// come from the document the tree was created from; `focused` is projected as in SemanticInput.
struct InspectionInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    const LayoutResult* layout = nullptr;
    const DocumentSourceMap* sources = nullptr;
    std::optional<NodeHandle> focused;
};

std::vector<Diagnostic> validate(const InspectionInput&);
Result<InspectionSnapshot> capture_inspection(const InspectionInput&);

// Target forms in preferred order. Paths and points are less stable across edits.
struct AuthorIdTarget {
    std::string id;
};
struct SemanticTarget {
    SemanticRole role = SemanticRole::button;
    std::string name; // Exact match.
};
struct PathTarget {
    std::vector<std::uint32_t> children; // Child indices from the root; empty selects the root.
};
struct PointTarget {
    Point position; // Root logical coordinates.
};
using InspectionTarget = std::variant<AuthorIdTarget, SemanticTarget, PathTarget, PointTarget>;

// Resolves exactly one element of the snapshot and returns its handle in the snapshot generation.
// Missing and ambiguous targets fail. Point targets select the topmost visible displayed element and
// include disabled ones; pointer dispatch still uses hit testing.
Result<NodeHandle> resolve_target(const InspectionSnapshot&, const InspectionTarget&);

} // namespace tessera
