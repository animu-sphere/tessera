#include <tessera/commands/execution.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <exception>
#include <new>
#include <utility>

namespace tessera {
namespace {
CommandOutcome failure(std::vector<Diagnostic> diagnostics) {
    return {CommandOutcomeStatus::error, {}, std::move(diagnostics)};
}
Result<CommandExecution> rejected(std::string code, std::string path, std::string message) {
    return {std::nullopt, {{std::move(code), Severity::error, std::move(path), std::move(message), {}}}};
}
bool pointer(std::string_view path) {
    if (!path.empty() && path.front() != '/') return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '~' && (++i == path.size() || (path[i] != '0' && path[i] != '1'))) return false;
    }
    return true;
}
std::vector<Diagnostic> validate_outcome(const CommandOutcome& outcome) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    check.enumeration(outcome.status, CommandOutcomeStatus::error, "/result/status");
    if (!errors.empty()) return errors;
    if (outcome.status != CommandOutcomeStatus::success && !outcome.values.empty())
        check.error("invalid_outcome", "/result/values", "Only success may carry result values.");
    if (outcome.status != CommandOutcomeStatus::error && !outcome.diagnostics.empty())
        check.error("invalid_outcome", "/result/diagnostics", "Only an error may carry diagnostics.");
    if (outcome.status != CommandOutcomeStatus::error) return errors;
    if (outcome.diagnostics.empty() || outcome.diagnostics.size() > max_command_result_diagnostics) {
        check.error("out_of_range", "/result/diagnostics", "Supply between 1 and 64 host error diagnostics.");
        return errors;
    }
    bool has_error = false;
    for (std::size_t i = 0; i < outcome.diagnostics.size(); ++i) {
        const auto& d = outcome.diagnostics[i];
        const auto path = "/result/diagnostics/" + std::to_string(i);
        check.enumeration(d.severity, Severity::error, path + "/severity");
        has_error = has_error || d.severity == Severity::error;
        if (d.code.empty() || d.code.size() > 128 || !detail::valid_utf8(d.code))
            check.error("invalid_diagnostic", path + "/code", "Supply a nonempty UTF-8 code within 128 bytes.");
        if (d.message.empty() || d.message.size() > max_command_string_bytes || !detail::valid_utf8(d.message))
            check.error("invalid_diagnostic", path + "/message", "Supply a nonempty UTF-8 message within 4096 bytes.");
        if (d.path.size() > max_command_string_bytes || !detail::valid_utf8(d.path) || !pointer(d.path))
            check.error("invalid_diagnostic", path + "/path", "Supply a UTF-8 JSON-pointer location within 4096 bytes.");
        if (!errors.empty()) return errors;
    }
    if (!has_error) check.error("invalid_outcome", "/result/diagnostics", "An error outcome requires at least one error diagnostic.");
    return errors;
}
} // namespace

CommandExecutor::CommandExecutor() : thread_(std::this_thread::get_id()) {}
Result<CommandExecution> CommandExecutor::execute(const CommandSnapshot& snapshot,
    const CommandInvocation& invocation, CommandHost& host, const SemanticInput* ui) {
    if (std::this_thread::get_id() != thread_)
        return rejected("wrong_thread", "/execution", "Execute on the session's creating UI thread.");
    if (executing_)
        return rejected("reentrant_execution", "/execution", "Queue commands until the current host execution returns.");
    if (invocation.record().sequence <= last_sequence_)
        return rejected("consumed_sequence", "/sequence", "Use a request sequence greater than every previously entered execution.");
    auto errors = snapshot.recheck(invocation, ui);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    // Own everything needed for validation/results before calling application code.
    // Failures here have not entered the host and do not consume the sequence.
    const auto retained = snapshot;
    CommandExecution execution{invocation.record(), {}};
    executing_ = true;
    struct Guard { bool& active; ~Guard() { active = false; } } guard{executing_};
    last_sequence_ = execution.invocation.sequence;
    try {
        execution.outcome = host.execute(execution.invocation);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& e) {
        const std::string_view message(e.what());
        execution.outcome = failure({{"host_exception", Severity::error, "/result/host",
            !message.empty() && message.size() <= max_command_string_bytes && detail::valid_utf8(message) ?
                std::string(message) : "The host command threw an exception.", {}}});
        return {std::move(execution), {}};
    } catch (...) {
        execution.outcome = failure({{"host_exception", Severity::error, "/result/host",
            "The host command threw a non-standard exception.", {}}});
        return {std::move(execution), {}};
    }
    errors = validate_outcome(execution.outcome);
    if (!errors.empty()) execution.outcome = failure(std::move(errors));
    else if (execution.outcome.status == CommandOutcomeStatus::success) {
        auto values = retained.normalize_results(execution.invocation.command, execution.outcome.values);
        if (values) execution.outcome.values = std::move(*values.value);
        else execution.outcome = failure(std::move(values.diagnostics));
    } else if (execution.outcome.status == CommandOutcomeStatus::error) {
        for (auto& d : execution.outcome.diagnostics) d.path = "/result/host" + d.path;
    }
    return {std::move(execution), {}};
}

} // namespace tessera
