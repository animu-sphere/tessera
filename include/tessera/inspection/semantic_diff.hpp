#pragma once

#include <tessera/semantics/semantic_snapshot.hpp>
#include <compare>
#include <span>
#include <variant>

namespace tessera {

inline constexpr std::uint32_t semantic_diff_version = 1;
inline constexpr std::size_t max_serialized_semantic_diff_bytes = std::size_t(32) << 20;

// What changed in meaning between two semantic snapshots. A diff reports each kind at most once per entry,
// in this order after the snapshot-level focus change.
enum class SemanticChangeKind : std::uint8_t {
    focus_changed,           // The focused entry moved: before/after are entry keys, empty when nothing is focused.
    node_added,              // after is the added entry.
    node_removed,            // before is the removed entry.
    node_moved,              // before/after are placements: a new parent, or a new order among the same siblings.
    role_changed,            // SemanticRole.
    name_changed,            // Accessible name.
    name_source_changed,     // NameSource.
    enabled_changed,         // bool.
    focusable_changed,       // bool.
    selection_changed,       // selected of an entry that is not a checkbox or switch; empty when absent.
    checked_changed,         // selected of a checkbox or switch.
    expanded_changed,        // Empty when absent.
    value_changed,           // Presented value text; empty when absent.
    item_count_changed,      // Empty when absent.
    item_position_changed,   // Empty when absent.
    multiselectable_changed, // Empty when absent.
    action_added,            // after is the SemanticAction.
    action_removed,          // before is the SemanticAction.
};
inline constexpr std::size_t semantic_change_kind_count = 18;

// Identity across generations. An entry with an author ID is matched by it. An entry without one is matched
// by its role and its position among the entries without an ID of that role whose nearest identified semantic
// ancestor is `within` (absent: none), so inserting an entry of another role or scope shifts nothing.
struct SemanticNodeKey {
    std::optional<std::string> id;
    std::optional<std::string> within;      // Unset when id is present.
    SemanticRole role = SemanticRole::root; // root when id is present.
    std::uint32_t ordinal = 0;              // 0 when id is present.
    auto operator<=>(const SemanticNodeKey&) const = default;
};

// Where an entry sits in its snapshot's semantic hierarchy.
struct SemanticPlacement {
    std::optional<SemanticNodeKey> parent; // Absent for an entry without a semantic parent.
    std::uint32_t index = 0;               // Position among the parent's semantic children.
    bool operator==(const SemanticPlacement&) const = default;
};

// An added or removed entry. Its record keeps the indices of its own snapshot.
struct SemanticEntry {
    SemanticPlacement placement;
    SemanticRecord record;
    bool operator==(const SemanticEntry&) const = default;
};

// monostate stands for an absent value.
using SemanticChangeValue = std::variant<std::monostate, bool, std::uint64_t, std::string, SemanticRole, NameSource,
                                         SemanticNodeKey, SemanticPlacement, SemanticAction, SemanticEntry>;

struct SemanticChange {
    SemanticChangeKind kind = SemanticChangeKind::node_added;
    std::optional<SemanticNodeKey> target;    // The entry that changed; absent for focus_changed.
    std::optional<std::uint32_t> before_node; // Preorder index in the before snapshot, absent when not there.
    std::optional<std::uint32_t> after_node;  // Preorder index in the after snapshot, absent when not there.
    SemanticChangeValue before;
    SemanticChangeValue after;
    bool operator==(const SemanticChange&) const = default;
};

struct SemanticDiffOptions {
    // Compare only the subtree of the entry with this author ID, in each snapshot that has it.
    std::optional<std::string> scope;
    // Report only these kinds; empty reports every kind.
    std::vector<SemanticChangeKind> kinds;
};

// Owned change set between two snapshots. Only diff_semantics creates one; it borrows nothing, never touches
// the runtime and says what changed, not why.
class SemanticDiff final {
public:
    std::uint32_t version() const noexcept { return version_; }
    std::uint64_t from_generation() const noexcept { return from_generation_; }
    std::uint64_t to_generation() const noexcept { return to_generation_; }
    const std::optional<std::string>& scope() const noexcept { return scope_; }
    // The reported kinds, sorted and unique; empty when every kind is reported.
    std::span<const SemanticChangeKind> kinds() const noexcept { return kinds_; }
    // focus_changed first; then the after snapshot's entries in preorder, each with its changes in kind
    // order; then removed entries in the before snapshot's preorder.
    std::span<const SemanticChange> changes() const noexcept { return changes_; }
    bool empty() const noexcept { return changes_.empty(); }
    // The first change of this kind for the entry with this author ID, or nullptr.
    const SemanticChange* find(std::string_view id, SemanticChangeKind kind) const noexcept;
    bool operator==(const SemanticDiff&) const = default;

private:
    SemanticDiff() = default;
    std::uint32_t version_ = semantic_diff_version;
    std::uint64_t from_generation_ = 0;
    std::uint64_t to_generation_ = 0;
    std::optional<std::string> scope_;
    std::vector<SemanticChangeKind> kinds_;
    std::vector<SemanticChange> changes_;
    friend Result<SemanticDiff> diff_semantics(const SemanticSnapshot&, const SemanticSnapshot&,
                                               const SemanticDiffOptions&);
};

// Compares two Semantic JSON v1 snapshots, usually two generations of one host. Equal inputs give equal
// diffs. Invalid snapshots are rejected with their diagnostics under /before or /after.
Result<SemanticDiff> diff_semantics(const SemanticSnapshot& before, const SemanticSnapshot& after,
                                    const SemanticDiffOptions& options = {});
// Canonical tessera-semantic-diff JSON v1, ending with a newline.
Result<std::string> save_semantic_diff(const SemanticDiff&);
// The JSON spelling of a kind, such as "value_changed"; empty for an undeclared value.
std::string_view semantic_change_name(SemanticChangeKind) noexcept;

} // namespace tessera
