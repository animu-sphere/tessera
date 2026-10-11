#pragma once

#include <tessera/inspection/inspection.hpp>

namespace tessera {
inline constexpr std::size_t max_ui_invariants = 256;
inline constexpr std::size_t max_invariant_text_bytes = 4096;

struct VisibleInvariant {
    InspectionTarget target;
    bool expected = true;
    bool operator==(const VisibleInvariant&) const = default;
};
// These predicates require an exposed semantic entry; absence is an error.
struct EnabledInvariant {
    InspectionTarget target;
    bool expected = true;
    bool operator==(const EnabledInvariant&) const = default;
};
struct FocusableInvariant {
    InspectionTarget target;
    bool expected = true;
    bool operator==(const FocusableInvariant&) const = default;
};
struct SemanticNameInvariant {
    InspectionTarget target;
    std::string expected;
    bool operator==(const SemanticNameInvariant&) const = default;
};
// Compare full logical border boxes, without clipping or paint transforms.
struct InsideViewportInvariant {
    InspectionTarget target;
    float tolerance = 0;
    bool operator==(const InsideViewportInvariant&) const = default;
};
struct NoOverlapInvariant {
    InspectionTarget target;
    InspectionTarget other;
    float tolerance = 0; // Pass if penetration on either axis is <= tolerance.
    bool operator==(const NoOverlapInvariant&) const = default;
};
using InvariantPredicate = std::variant<VisibleInvariant, EnabledInvariant, FocusableInvariant,
    SemanticNameInvariant, InsideViewportInvariant, NoOverlapInvariant>;
struct UiInvariant {
    std::string name; // Unique report identity, 1..256 UTF-8 bytes, no controls.
    InvariantPredicate predicate;
    std::optional<NodeHandle> scope; // Ordinary inspection subtree scope for both targets.
    bool operator==(const UiInvariant&) const = default;
};

enum class InvariantStatus : std::uint8_t { passed, failed, error };
struct InvariantObservation {
    NodeHandle target;
    std::optional<bool> state;
    std::optional<std::string> semantic_name;
    std::optional<Rect> bounds;
    std::optional<Rect> clip;
    std::optional<NodeSource> source;
    bool operator==(const InvariantObservation&) const = default;
};
struct InvariantResult {
    UiInvariant declaration;
    InvariantStatus status = InvariantStatus::error;
    std::vector<InvariantObservation> observations; // Target, then other when resolved.
    std::vector<Diagnostic> diagnostics;
    bool operator==(const InvariantResult&) const = default;
};
struct InvariantReport {
    std::uint64_t generation = 0;
    std::optional<Size> viewport; // Host-declared conditions of this generation.
    std::string source_file;
    std::vector<InvariantResult> results; // Declaration order, including errors.
    bool passed() const noexcept;
    bool operator==(const InvariantReport&) const = default;
};
struct InvariantInput {
    const InspectionSnapshot* snapshot = nullptr;
    std::span<const UiInvariant> invariants;
    std::optional<Size> viewport; // Required by InsideViewportInvariant; origin is (0,0).
};

// Read-only, bounded in-process tooling. Invalid declarations/snapshot structure
// return no report. A valid evaluation returns a report even when predicates fail
// or targets/observations are unavailable; inspect report.passed(), not Result's bool.
Result<InvariantReport> evaluate_invariants(const InvariantInput&);
} // namespace tessera
