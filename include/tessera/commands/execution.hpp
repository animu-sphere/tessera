#pragma once

#include <tessera/commands/commands.hpp>
#include <thread>

namespace tessera {

inline constexpr std::size_t max_command_result_diagnostics = 64;

enum class CommandOutcomeStatus : std::uint8_t { success, cancelled, error };
// Success carries declared fields; cancellation carries nothing; error carries
// bounded, located error diagnostics. The adapter validates the whole outcome.
struct CommandOutcome {
    CommandOutcomeStatus status = CommandOutcomeStatus::success;
    CommandArguments values;
    std::vector<Diagnostic> diagnostics;
    bool operator==(const CommandOutcome&) const = default;
};
struct CommandExecution {
    CommandInvocationRecord invocation;
    CommandOutcome outcome;
    bool operator==(const CommandExecution&) const = default;
};

// Application implementation, called synchronously only after dispatch returns.
// No implementation is stored in the registry, tree, or executor.
class CommandHost {
public:
    virtual ~CommandHost() = default;
    virtual CommandOutcome execute(const CommandInvocationRecord&) = 0;
};

// One host-owned session on its creating thread. Accepted request sequences must
// increase across all commands/generations; consumed sequences cannot be retried.
class CommandExecutor final {
public:
    CommandExecutor();
    CommandExecutor(const CommandExecutor&) = delete;
    CommandExecutor& operator=(const CommandExecutor&) = delete;
    CommandExecutor(CommandExecutor&&) = delete;
    CommandExecutor& operator=(CommandExecutor&&) = delete;
    // Rejection returns no value and never calls the host. Once entered, host
    // failures and invalid outcomes return an owned error execution (no rollback).
    // Node-owned invocations require current_ui; all borrowed inputs stay alive
    // until return. Allocation failures propagate, preserving sequence consumption.
    Result<CommandExecution> execute(const CommandSnapshot&, const CommandInvocation&,
        CommandHost&, const SemanticInput* current_ui = nullptr);
private:
    const std::thread::id thread_;
    std::uint64_t last_sequence_ = 0;
    bool executing_ = false;
    friend class CommandTransactions;
};

} // namespace tessera
