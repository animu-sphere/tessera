#pragma once

#include <tessera/semantics/semantics.hpp>
#include <map>
#include <memory>
#include <set>
#include <variant>

namespace tessera {

inline constexpr std::size_t max_commands = 1024;
inline constexpr std::size_t max_command_parameters = 64;
inline constexpr std::size_t max_command_string_bytes = 4096;

// Object references are distinct from strings; the host publishes the permitted IDs.
struct CommandObjectId {
    std::string value;
    bool operator==(const CommandObjectId&) const = default;
};
using CommandValue = std::variant<bool, double, std::string, CommandObjectId>;
using CommandArguments = std::map<std::string, CommandValue, std::less<>>;
enum class CommandParameterType : std::uint8_t { boolean, number, string, enumeration, object_id };
struct CommandParameter {
    std::string name; // [a-z][a-z0-9_]*
    CommandParameterType type = CommandParameterType::boolean;
    bool required = true;
    std::optional<CommandValue> default_value;
    std::optional<double> minimum; // Inclusive number bounds; absent means unbounded.
    std::optional<double> maximum;
    std::size_t max_bytes = max_command_string_bytes; // String/object ID bound.
    std::vector<std::string> choices; // Nonempty, unique UTF-8 values for enumeration only.
    bool operator==(const CommandParameter&) const = default;
};
enum class CommandAvailability : std::uint8_t { both, development, production };
enum class CommandMode : std::uint8_t { development, production };
enum class CommandSource : std::uint8_t { pointer, keyboard, gamepad, accessibility, agent, replay };
struct CommandDescriptor {
    std::string id; // Dot-separated [a-z][a-z0-9_]* segments; at least two.
    std::string label;
    std::string description;
    std::string category;
    CommandAvailability availability = CommandAvailability::both;
    std::vector<CommandParameter> parameters;
    bool operator==(const CommandDescriptor&) const = default;
};
struct CommandState {
    bool enabled = false; // Missing observations and query errors fail closed.
    bool checked = false;
    std::string disabled_reason;
    std::optional<std::string> query_error;
    bool operator==(const CommandState&) const = default;
};
using CommandStates = std::map<std::string, CommandState, std::less<>>;
using CommandBindings = std::map<std::string, std::string, std::less<>>; // Action -> exact command ID.
using CommandObjectIds = std::set<std::string, std::less<>>;
struct CommandInfo {
    CommandDescriptor descriptor;
    CommandState state;
    bool operator==(const CommandInfo&) const = default;
};

namespace detail { struct CommandRegistryData; struct CommandSnapshotData; }

// Immutable owned descriptors/mappings. Copies share a revision, with no callbacks or global registry.
class CommandRegistry final {
public:
    static Result<CommandRegistry> create(std::vector<CommandDescriptor>, CommandBindings = {});
    std::uint64_t revision() const noexcept;
private:
    explicit CommandRegistry(std::shared_ptr<const detail::CommandRegistryData>);
    std::shared_ptr<const detail::CommandRegistryData> data_;
    friend class CommandSnapshot;
};

struct CommandRequest {
    std::uint64_t generation = 0; // Host update sequence; nonzero, never reused in a session.
    std::string command;
    CommandArguments arguments;
    CommandSource source = CommandSource::pointer;
    std::uint64_t sequence = 0; // Nonzero host logical request sequence.
};
struct CommandInvocationRecord {
    std::uint64_t generation = 0;
    std::uint64_t registry_revision = 0;
    std::string command;
    CommandArguments arguments; // Includes validated defaults.
    CommandSource source = CommandSource::pointer;
    std::uint64_t sequence = 0;
    std::optional<ActionRequest> owner; // Handle only; never dereferenced after tree expiry.
    bool operator==(const CommandInvocationRecord&) const = default;
};
// Only successful resolution creates this immutable owned value. Retains the observation's lifetime.
class CommandInvocation final {
public:
    const CommandInvocationRecord& record() const noexcept { return record_; }
private:
    CommandInvocation(CommandInvocationRecord, std::shared_ptr<const detail::CommandSnapshotData>);
    CommandInvocationRecord record_;
    std::shared_ptr<const detail::CommandSnapshotData> snapshot_;
    friend class CommandSnapshot;
};

// Host publishes queries/object IDs at an update point, before building the UI generation.
class CommandSnapshot final {
public:
    static Result<CommandSnapshot> publish(const CommandRegistry&, std::uint64_t generation,
        CommandMode, CommandStates, CommandObjectIds = {});
    std::uint64_t generation() const noexcept;
    std::uint64_t registry_revision() const noexcept;
    std::vector<CommandInfo> discover() const; // Available descriptors, sorted by ID.
    Result<CommandInfo> lookup(std::string_view id) const;
    // Copy the authored document and compose command eligibility into activate owners' disabled property.
    // Unmapped actions are unchanged. Always lower from the authored document, not an earlier lowered copy.
    Result<UiDocument> apply_eligibility(const UiDocument&, const ValidationContext&) const;
    Result<CommandInvocation> resolve(const CommandRequest&) const; // Node-free request.
    // Borrow a coherent UI snapshot for this generation; verify the returned action's live owner/eligibility.
    Result<CommandInvocation> resolve_action(const SemanticInput&, const ActionRequest&,
        std::uint64_t generation, CommandArguments, CommandSource, std::uint64_t sequence) const;
    // Host MUST call immediately before executing after dispatch. No callback, mutation, or transaction here.
    // Node-owned invocations require the current UI snapshot, including on an unchanged command generation.
    std::vector<Diagnostic> recheck(const CommandInvocation&, const SemanticInput* current_ui = nullptr) const;
private:
    explicit CommandSnapshot(std::shared_ptr<const detail::CommandSnapshotData>);
    std::shared_ptr<const detail::CommandSnapshotData> data_;
};

} // namespace tessera
