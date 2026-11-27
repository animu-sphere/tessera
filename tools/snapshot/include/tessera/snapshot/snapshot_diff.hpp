#pragma once

// Development tooling above tessera::core: comparison of two loaded snapshot bundles.
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tessera {

// The comparison report has its own version, separate from the bundle version.
inline constexpr std::uint32_t snapshot_diff_version = 1;

struct SnapshotDiffOptions {
    double geometry_tolerance = 0;    // Logical units: layout numbers that differ by at most this much are equal.
    std::uint8_t pixel_threshold = 0; // A pixel differs when a channel changes by more than this.
    std::uint64_t pixel_count = 0;    // Frames differ when more pixels than this differ.
};

enum class SnapshotDiffStatus : std::uint8_t { equal, different, incompatible };
enum class SnapshotChange : std::uint8_t { added, removed, changed };

// A node matched across bundles: by author ID, or, for an unnamed node, by its preorder position among the
// unnamed nodes whose nearest identified ancestor is `within` (absent: no identified ancestor).
struct SnapshotNodeKey {
    std::optional<std::string> id;
    std::optional<std::string> within;
    std::uint32_t ordinal = 0;
    auto operator<=>(const SnapshotNodeKey&) const = default;
};

// A changed field. Absent fields compare and report as null; references to other nodes report their keys.
struct SnapshotFieldChange {
    std::string field;
    JsonValue before;
    JsonValue after;
};

// A condition that must match before bundles are judged: bundle_version, environment, text_service,
// capture_backend and glyphs for the bundles, and viewport and scale for each generation pair.
struct SnapshotCondition {
    std::string condition;
    std::optional<std::size_t> pair;
    JsonValue before;
    JsonValue after;
};

struct SnapshotPair {
    std::uint64_t before = 0;
    std::uint64_t after = 0;
};

// A semantic entry or layout node that was added, removed or changed within one generation pair.
struct SnapshotNodeChange {
    std::size_t pair = 0;
    SnapshotNodeKey node;
    std::optional<std::uint32_t> before; // Preorder index, absent when added.
    std::optional<std::uint32_t> after;  // Preorder index, absent when removed.
    SnapshotChange change = SnapshotChange::changed;
    std::vector<SnapshotFieldChange> fields; // Changed fields only, by name.
};

// An entry of the run's requested actions, compared by position.
struct SnapshotActionChange {
    std::size_t index = 0;
    SnapshotChange change = SnapshotChange::changed;
    JsonValue before; // null when added.
    JsonValue after;  // null when removed.
};

struct SnapshotPaintChange {
    std::size_t pair = 0;
    std::vector<SnapshotFieldChange> fields; // commands, glyphs, time_dependent and kinds.<kind>.
};

struct SnapshotPixelComparison {
    std::size_t pair = 0;
    std::optional<CaptureExtent> before; // Absent when that bundle has no frame for the generation.
    std::optional<CaptureExtent> after;
    std::uint64_t compared = 0;  // Pixels compared; 0 unless both frames exist with equal extents.
    std::uint64_t differing = 0; // Pixels with a channel delta above the threshold.
    std::uint8_t max_delta = 0;  // Largest channel delta over the compared pixels.
    std::optional<std::array<std::uint32_t, 4>> bounds; // x, y, width, height enclosing the differing pixels.
    bool different = false;
};

struct SnapshotDiff {
    SnapshotDiffStatus status = SnapshotDiffStatus::equal;
    // The most semantic level that differs: semantic, layout, actions, paint or pixels.
    std::optional<std::string> first_difference;
    SnapshotDiffOptions options;
    // Generation pairs: every listed generation when both bundles list the same ones, otherwise their last ones.
    std::vector<SnapshotPair> pairs;
    std::vector<SnapshotCondition> conditions; // Mismatched conditions only. Any one makes the status incompatible.
    // The judged levels, in order. They stay empty when the status is incompatible.
    std::vector<SnapshotNodeChange> semantic;
    std::vector<SnapshotNodeChange> layout;
    std::vector<SnapshotActionChange> actions;
    std::vector<SnapshotPaintChange> paint;
    std::vector<SnapshotPixelComparison> pixels; // One per pair.
};

// Compares two bundles from load_snapshot_bundle. A geometry tolerance that is negative or not finite is
// invalid_diff_option at /options/geometry_tolerance.
Result<SnapshotDiff> diff_snapshot_bundles(const LoadedSnapshotBundle& before, const LoadedSnapshotBundle& after,
                                           const SnapshotDiffOptions& options = {});

// The report as "tessera-snapshot-diff" JSON v1 with a trailing newline. Judged levels are null when the
// status is incompatible.
std::string save_snapshot_diff(const SnapshotDiff&);

} // namespace tessera
