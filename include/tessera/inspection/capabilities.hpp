#pragma once

#include <tessera/commands/commands.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <span>

namespace tessera {

inline constexpr std::uint32_t capability_version = 1;
inline constexpr std::size_t max_capability_text_bytes = 256;
inline constexpr std::size_t max_serialized_capability_bytes = std::size_t(16) << 20;

// Fixed operation vocabulary. Every manifest reports each operation once, in this order, so an
// operation the adapter does not offer is explicit rather than absent.
enum class CapabilityOperation : std::uint8_t {
    inspect_tree,        // Semantic and layout observations of one generation.
    inspect_properties,  // Property, style and source records of one generation.
    semantic_action,     // Activate or expand/collapse a resolved target through semantic eligibility.
    invoke_command,      // Request a command through ordinary resolution and host execution.
    replay_input,        // Normalized input or Replay steps.
    capture_frame,       // Completed pixels of one generation.
    evaluate_invariants, // UI invariant reports.
    edit_source,         // Edit authored sources through a host-authorized source adapter.
    reload,              // Replace the document and publish a new generation.
    compare_snapshots,   // Compare two captured observations.
};
inline constexpr std::size_t capability_operation_count = 10;
enum class CapabilityPrecondition : std::uint8_t {
    generation, // A request names the manifest's generation and is rejected once it is replaced.
    target,     // A request names a target resolved in that generation.
};
enum class CapabilityStatus : std::uint8_t {
    supported,
    not_declared, // The adapter does not offer the operation.
    unavailable,  // Declared for another mode, such as a development-only operation in production.
};

// One operation the adapter offers. Strings are opaque identities of 1-256 UTF-8 bytes without ASCII
// controls: how the adapter exposes the operation and the request/result schemas it uses.
struct CapabilityDeclaration {
    CapabilityOperation operation = CapabilityOperation::inspect_tree;
    CommandAvailability availability = CommandAvailability::both;
    std::string entry_point;
    std::string request;
    std::string result;
    std::vector<CapabilityPrecondition> preconditions; // Unique.
    bool operator==(const CapabilityDeclaration&) const = default;
};

// Borrowed for one capture. Commands and semantics belong to the same host generation.
struct CapabilityInput {
    std::string_view producer; // Host identity, 1-256 UTF-8 bytes without ASCII controls.
    const CommandSnapshot* commands = nullptr;
    const SemanticSnapshot* semantics = nullptr;
    std::span<const CapabilityDeclaration> operations;
};

struct CapabilityOperationRecord {
    CapabilityOperation operation = CapabilityOperation::inspect_tree;
    CapabilityStatus status = CapabilityStatus::not_declared;
    std::string entry_point; // Empty unless supported.
    std::string request;
    std::string result;
    std::vector<CapabilityPrecondition> preconditions; // Sorted; empty unless supported.
    bool operator==(const CapabilityOperationRecord&) const = default;
};
struct CapabilityAction {
    std::string binding; // "activate", or the applicable tree "expand"/"collapse".
    std::string action;
    std::optional<std::string> command; // The explicitly mapped command, when it is available in this mode.
    bool operator==(const CapabilityAction&) const = default;
};
// A semantic entry that can be focused or exposes an eligible action.
struct CapabilityTarget {
    std::uint32_t node = 0; // Document preorder index, as in Semantic JSON v1.
    std::optional<std::string> id;
    SemanticRole role = SemanticRole::root;
    std::string name;
    bool focusable = false;
    std::vector<CapabilityAction> actions;
    bool operator==(const CapabilityTarget&) const = default;
};

// Owned projection of one generation's commands and semantics with the adapter's operation inventory.
// Only capture_capabilities creates one; it borrows nothing and grants no invocation authority.
class CapabilityManifest final {
public:
    std::uint32_t version() const noexcept { return version_; }
    const std::string& producer() const noexcept { return producer_; }
    CommandMode mode() const noexcept { return mode_; }
    std::uint64_t generation() const noexcept { return generation_; }
    // Every operation in declaration-enum order.
    std::span<const CapabilityOperationRecord> operations() const noexcept { return operations_; }
    const CapabilityOperationRecord& operation(CapabilityOperation operation) const {
        return operations_.at(static_cast<std::size_t>(operation));
    }
    // Commands available in this mode, ordered by ID, exactly as CommandSnapshot::discover reports them.
    std::span<const CommandInfo> commands() const noexcept { return commands_; }
    const CommandObjectIds& objects() const noexcept { return objects_; }
    std::span<const CapabilityTarget> targets() const noexcept { return targets_; }
    bool operator==(const CapabilityManifest&) const = default;

private:
    CapabilityManifest() = default;
    std::uint32_t version_ = capability_version;
    std::string producer_;
    CommandMode mode_ = CommandMode::production;
    std::uint64_t generation_ = 0;
    std::vector<CapabilityOperationRecord> operations_;
    std::vector<CommandInfo> commands_;
    CommandObjectIds objects_;
    std::vector<CapabilityTarget> targets_;
    friend Result<CapabilityManifest> capture_capabilities(const CapabilityInput&);
};

Result<CapabilityManifest> capture_capabilities(const CapabilityInput&);
// Canonical tessera-capabilities JSON v1, ending with a newline.
Result<std::string> save_capabilities(const CapabilityManifest&);

} // namespace tessera
