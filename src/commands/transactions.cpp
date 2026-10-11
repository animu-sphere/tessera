#include <tessera/commands/transactions.hpp>
#include "outcome.hpp"
#include "../ui/json_detail.hpp"
#include <atomic>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace tessera {
namespace detail {
struct TransactionState {
    TransactionRecord record;
    const TransactionHost* host = nullptr; // Identity only; never called through this pointer.
    std::atomic<bool> open{true};
    bool edited = false;
};
} // namespace detail
namespace {
std::vector<Diagnostic> error(std::string code, std::string path, std::string message) {
    return {{std::move(code), Severity::error, std::move(path), std::move(message), {}}};
}
CommandOutcome failure(std::vector<Diagnostic> diagnostics) {
    return {CommandOutcomeStatus::error, {}, std::move(diagnostics)};
}
void append(std::vector<Diagnostic>& to, const std::vector<Diagnostic>& from) {
    to.insert(to.end(), from.begin(), from.end());
}
struct EntryGuard {
    bool& entered;
    explicit EntryGuard(bool& value) : entered(value) { entered = true; }
    ~EntryGuard() { entered = false; }
};
std::uint64_t next_session() {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    do {
        if (value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Transaction session identities exhausted.");
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return value;
}
template<class Callable>
CommandOutcome boundary(std::string_view phase, Callable&& call) {
    CommandOutcome outcome;
    bool threw = false;
    try { outcome = call(); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& e) { threw = true; outcome = detail::command_host_exception(e.what()); }
    catch (...) { threw = true; outcome = detail::command_host_exception({}); }
    auto errors = detail::validate_command_outcome(outcome);
    if (errors.empty() && outcome.status == CommandOutcomeStatus::success && !outcome.values.empty())
        errors = error("invalid_outcome", "/result/values", "Transaction boundaries require void success.");
    if (!errors.empty()) outcome = failure(std::move(errors));
    else if (outcome.status == CommandOutcomeStatus::cancelled)
        outcome = failure(error("transaction_cancelled", "/result/host", "The host cancelled the transaction boundary."));
    else if (outcome.status == CommandOutcomeStatus::error) {
        for (auto& d : outcome.diagnostics) {
            // Exceptions already carry this location; ordinary host diagnostics
            // remain relative to the boundary, as in CommandExecutor.
            if (!threw) d.path = "/result/host" + d.path;
        }
    }
    for (auto& d : outcome.diagnostics)
        d.path = "/transaction/" + std::string(phase) + d.path.substr(7); // Replace /result.
    return outcome;
}
std::vector<Diagnostic> metadata_errors(const TransactionMetadata& metadata, const CommandSnapshot& snapshot) {
    auto errors = std::vector<Diagnostic>{};
    if (metadata.label.empty() || metadata.label.size() > max_command_string_bytes || !detail::valid_utf8(metadata.label))
        append(errors, error("invalid_metadata", "/transaction/metadata/label", "Supply a nonempty UTF-8 label within 4096 bytes."));
    if (metadata.merge_hint.size() > max_command_string_bytes || !detail::valid_utf8(metadata.merge_hint))
        append(errors, error("invalid_metadata", "/transaction/metadata/merge_hint", "Supply a UTF-8 merge hint within 4096 bytes."));
    if (metadata.affected_objects.size() > max_transaction_objects)
        append(errors, error("out_of_range", "/transaction/metadata/affected_objects", "Supply at most 64 affected object IDs."));
    if (!errors.empty()) return errors;
    for (const auto& id : metadata.affected_objects) {
        if (id.empty() || id.size() > max_command_string_bytes || !detail::valid_utf8(id) ||
            !snapshot.permits_object(id)) {
            append(errors, error("invalid_reference", "/transaction/metadata/affected_objects", "Use bounded UTF-8 IDs permitted in this generation."));
            break;
        }
    }
    return errors;
}
class EditingHost final : public CommandHost {
public:
    EditingHost(TransactionHost& host, const TransactionRecord& record) : host_(host), record_(record) {}
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        return host_.execute_edit(invocation, record_);
    }
private:
    TransactionHost& host_;
    const TransactionRecord& record_;
};
} // namespace

TransactionToken::TransactionToken(const std::shared_ptr<detail::TransactionState>& state) : state_(state) {}
bool TransactionToken::active() const noexcept {
    const auto state = state_.lock();
    return state && state->open.load(std::memory_order_relaxed);
}
CommandTransactions::CommandTransactions() : thread_(std::this_thread::get_id()), session_(next_session()) {}
CommandTransactions::~CommandTransactions() = default;
std::vector<Diagnostic> CommandTransactions::entry() const {
    if (std::this_thread::get_id() != thread_)
        return error("wrong_thread", "/transaction", "Use the transaction session's creating UI thread.");
    if (entering_)
        return error("reentrant_transaction", "/transaction", "Queue operations until the host boundary returns.");
    return {};
}
std::vector<Diagnostic> CommandTransactions::token_check(const TransactionToken& token, const TransactionHost& host) const {
    const auto state = token.state_.lock();
    if (!state || !state->open.load(std::memory_order_relaxed))
        return error("closed_transaction", "/transaction/token", "Begin a new edit; closed tokens cannot be reused.");
    if (state != active_)
        return error("foreign_transaction", "/transaction/token", "Use the token issued by this session.");
    if (state->host != &host)
        return error("foreign_host", "/transaction/host", "Use the host instance that accepted begin.");
    return {};
}
std::vector<Diagnostic> CommandTransactions::refresh(const CommandSnapshot& snapshot, const SemanticInput* ui) const {
    const auto& record = active_->record.opening;
    if (record.registry_revision != snapshot.registry_revision())
        return error("stale_registry", "/registry_revision", "Roll back the edit after registry replacement.");
    auto errors = metadata_errors(active_->record.metadata, snapshot);
    if (!errors.empty()) return errors;
    if (record.owner && !ui)
        return error("missing_snapshot", "/owner", "Supply the current coherent UI snapshot for this edit.");
    const auto resolved = record.owner ?
        snapshot.resolve_action(*ui, *record.owner, snapshot.generation(), record.arguments, record.source, record.sequence) :
        snapshot.resolve({snapshot.generation(), record.command, record.arguments, record.source, record.sequence});
    if (!resolved) return resolved.diagnostics;
    if (resolved.value->record().command != record.command)
        return error("stale_binding", "/owner/action", "Roll back the edit after action remapping.");
    return {};
}
CommandOutcome CommandTransactions::close(TransactionHost& host, bool commit, TransactionCancel reason) {
    // Close before callback entry, even on exceptions or malformed host output.
    const auto state = std::move(active_);
    state->open.store(false, std::memory_order_relaxed);
    return commit ? boundary("commit", [&] { return host.commit(state->record); }) :
        boundary("rollback", [&] { return host.rollback(state->record, reason); });
}
Result<TransactionToken> CommandTransactions::begin(const CommandSnapshot& snapshot, const CommandInvocation& invocation,
    const TransactionMetadata& metadata, TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (active_) return {std::nullopt, error("nested_transaction", "/transaction", "Finish the active edit before beginning another.")};
    if (invocation.record().sequence <= last_begin_ || invocation.record().sequence <= executor_.last_sequence_)
        return {std::nullopt, error("consumed_sequence", "/sequence", "Begin with a new request sequence before executing its command.")};
    errors = snapshot.recheck(invocation, ui);
    if (errors.empty()) errors = metadata_errors(metadata, snapshot);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto state = std::make_shared<detail::TransactionState>();
    state->record = {session_, invocation.record().sequence, invocation.record(), metadata};
    state->host = &host;
    EntryGuard guard(entering_);
    last_begin_ = state->record.sequence;
    auto outcome = boundary("begin", [&] { return host.begin(state->record); });
    if (outcome.status != CommandOutcomeStatus::success) return {std::nullopt, std::move(outcome.diagnostics)};
    active_ = std::move(state);
    return {TransactionToken(active_), {}};
}
Result<CommandExecution> CommandTransactions::execute(const CommandSnapshot& snapshot, const CommandInvocation& invocation,
    CommandHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (invocation.record().sequence <= last_begin_)
        return {std::nullopt, error("consumed_sequence", "/sequence", "Use a new non-editing request sequence; begin reserves its opening request for an explicit edit.")};
    EntryGuard guard(entering_);
    return executor_.execute(snapshot, invocation, host, ui);
}
Result<CommandExecution> CommandTransactions::execute_edit(const CommandSnapshot& snapshot, const CommandInvocation& invocation,
    const TransactionToken& token, TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (errors.empty()) errors = token_check(token, host);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    EntryGuard guard(entering_);
    errors = refresh(snapshot, ui);
    const auto& opening = active_->record.opening;
    if (errors.empty() && (invocation.record().command != opening.command || invocation.record().owner != opening.owner ||
        invocation.record().sequence < opening.sequence ||
        (invocation.record().sequence == opening.sequence && invocation.record() != opening)))
        errors = error("transaction_owner_mismatch", "/transaction/token", "Join this token only with its command and live action owner, at or after its opening sequence.");
    if (!errors.empty()) {
        const auto rolled_back = close(host, false, TransactionCancel::owner_changed);
        append(errors, rolled_back.diagnostics);
        return {std::nullopt, std::move(errors)};
    }
    EditingHost editing(host, active_->record);
    auto result = executor_.execute(snapshot, invocation, editing, ui);
    if (result && result.value->outcome.status == CommandOutcomeStatus::success) {
        active_->edited = true;
        return result;
    }
    const auto rolled_back = close(host, false, TransactionCancel::command_failed);
    if (result && rolled_back.status == CommandOutcomeStatus::error) {
        result.value->outcome.status = CommandOutcomeStatus::error;
        result.value->outcome.values.clear();
        append(result.value->outcome.diagnostics, rolled_back.diagnostics);
    } else if (!result) append(result.diagnostics, rolled_back.diagnostics);
    return result;
}
Result<CommandOutcome> CommandTransactions::commit(const CommandSnapshot& snapshot, const TransactionToken& token,
    TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (errors.empty()) errors = token_check(token, host);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    EntryGuard guard(entering_);
    errors = refresh(snapshot, ui);
    if (errors.empty() && !active_->edited)
        errors = error("empty_transaction", "/transaction", "Execute an accepted edit before committing.");
    if (!errors.empty()) {
        const auto rolled_back = close(host, false, TransactionCancel::owner_changed);
        append(errors, rolled_back.diagnostics);
        return {failure(std::move(errors)), {}};
    }
    return {close(host, true, TransactionCancel::user), {}};
}
Result<CommandOutcome> CommandTransactions::rollback(const TransactionToken& token, TransactionCancel reason, TransactionHost& host) {
    auto errors = entry();
    if (errors.empty()) errors = token_check(token, host);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (static_cast<unsigned>(reason) > static_cast<unsigned>(TransactionCancel::command_failed))
        return {std::nullopt, error("unknown_value", "/transaction/reason", "Use a declared cancellation reason.")};
    EntryGuard guard(entering_);
    return {close(host, false, reason), {}};
}
} // namespace tessera
