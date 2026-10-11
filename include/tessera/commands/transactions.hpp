#pragma once

#include <tessera/commands/execution.hpp>

namespace tessera {

inline constexpr std::size_t max_transaction_objects = 64;

struct TransactionMetadata {
    std::string label; // Nonempty UTF-8, at most 4096 bytes.
    std::string merge_hint; // Host-owned history policy; no merging in core.
    CommandObjectIds affected_objects; // At most 64 permitted IDs.
    std::optional<std::int64_t> timestamp; // Optional host timestamp; no core clock.
    bool operator==(const TransactionMetadata&) const = default;
};
struct TransactionRecord {
    std::uint64_t session = 0;
    std::uint64_t sequence = 0; // Opening command request, never reused for begin.
    CommandInvocationRecord opening;
    TransactionMetadata metadata;
    bool operator==(const TransactionRecord&) const = default;
};
enum class TransactionCancel : std::uint8_t {
    user, capture_lost, owner_changed, command_failed
};
namespace detail { struct TransactionState; }

// Copyable weak capability, issued only after a successful host begin.
class TransactionToken final {
public:
    bool active() const noexcept;
private:
    explicit TransactionToken(const std::shared_ptr<detail::TransactionState>&);
    std::weak_ptr<detail::TransactionState> state_;
    friend class CommandTransactions;
};

// The same host instance owns the application token/data through closure.
// Begin must be atomic: any non-success/exception leaves no open application edit.
// Boundary outcomes use CommandOutcome with void success; no callbacks are stored.
class TransactionHost : public CommandHost {
public:
    virtual CommandOutcome begin(const TransactionRecord&) = 0;
    virtual CommandOutcome execute_edit(const CommandInvocationRecord&, const TransactionRecord&) = 0;
    virtual CommandOutcome commit(const TransactionRecord&) = 0;
    virtual CommandOutcome rollback(const TransactionRecord&, TransactionCancel) = 0;
};

// One single-level edit session and shared command sequence stream, on one UI
// thread. Dispose explicitly before destroying the session/host; no destructor
// invokes application code. A token never keeps either object alive.
class CommandTransactions final {
public:
    CommandTransactions();
    ~CommandTransactions();
    CommandTransactions(const CommandTransactions&) = delete;
    CommandTransactions& operator=(const CommandTransactions&) = delete;
    CommandTransactions(CommandTransactions&&) = delete;
    CommandTransactions& operator=(CommandTransactions&&) = delete;

    Result<TransactionToken> begin(const CommandSnapshot&, const CommandInvocation&,
        const TransactionMetadata&, TransactionHost&, const SemanticInput* current_ui = nullptr);
    // Explicit token joining; no implicit begin/commit. Ordinary rejection or a
    // failed/cancelled command requests rollback once. Rollback failure diagnostics
    // accompany rejection, or turn an entered execution into an error outcome.
    Result<CommandExecution> execute_edit(const CommandSnapshot&, const CommandInvocation&,
        const TransactionToken&, TransactionHost&, const SemanticInput* current_ui = nullptr);
    // Non-editing commands never begin or join an edit, including undo/redo.
    Result<CommandExecution> execute(const CommandSnapshot&, const CommandInvocation&,
        CommandHost&, const SemanticInput* current_ui = nullptr);
    // Rechecks original command/owner eligibility against the new generation;
    // invalidation closes via rollback, returning the invalidation as an error.
    Result<CommandOutcome> commit(const CommandSnapshot&, const TransactionToken&,
        TransactionHost&, const SemanticInput* current_ui = nullptr);
    Result<CommandOutcome> rollback(const TransactionToken&, TransactionCancel, TransactionHost&);
private:
    std::vector<Diagnostic> entry() const;
    std::vector<Diagnostic> token_check(const TransactionToken&, const TransactionHost&) const;
    std::vector<Diagnostic> refresh(const CommandSnapshot&, const SemanticInput*) const;
    CommandOutcome close(TransactionHost&, bool commit, TransactionCancel);
    CommandExecutor executor_;
    const std::thread::id thread_;
    const std::uint64_t session_;
    std::uint64_t last_begin_ = 0;
    std::shared_ptr<detail::TransactionState> active_;
    bool entering_ = false;
};

} // namespace tessera
