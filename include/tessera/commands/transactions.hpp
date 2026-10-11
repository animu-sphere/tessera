#pragma once

#include <tessera/commands/commands.hpp>
#include <chrono>

namespace tessera {

inline constexpr std::size_t max_transaction_objects = 10000;

enum class TransactionPhase : std::uint8_t { begin, commit, rollback };
enum class RollbackReason : std::uint8_t { cancelled, capture_lost, owner_removed, reload, command_failed };

struct TransactionMetadata {
    std::string label; // Nonempty UTF-8 history label.
    CommandSource source = CommandSource::pointer;
    std::optional<std::chrono::microseconds> timestamp; // Host-supplied; never read from a clock.
    std::string merge_hint; // Opaque to core; the application decides coalescing after commit.
    CommandObjectIds objects; // Affected host object IDs.
    bool operator==(const TransactionMetadata&) const = default;
};

// Identifies one transaction of one session. Values only; a session validates every use.
struct TransactionToken {
    std::uint64_t session = 0;
    std::uint64_t id = 0;
    bool operator==(const TransactionToken&) const = default;
};

// Owned boundary report for the host's application adapter, which applies it.
struct TransactionRecord {
    TransactionToken token;
    TransactionPhase phase = TransactionPhase::begin;
    std::uint64_t sequence = 0; // Logical request sequence of this boundary.
    TransactionMetadata metadata; // As supplied to begin.
    std::size_t edits = 0; // Accepted joined edits before this boundary.
    std::optional<RollbackReason> reason; // Rollback only.
    bool operator==(const TransactionRecord&) const = default;
};

// Single-level edit transactions for one host session, on the host UI thread.
// The session never mutates application data or history; it validates token
// use and returns records that the host delivers before its next published
// UI generation. Non-editing commands use none of these operations.
class TransactionSession final {
public:
    TransactionSession(); // Allocates a process-unique session identity.
    TransactionSession(const TransactionSession&) = delete;
    TransactionSession& operator=(const TransactionSession&) = delete;
    TransactionSession(TransactionSession&&) = delete;
    TransactionSession& operator=(TransactionSession&&) = delete;

    std::uint64_t identity() const noexcept { return identity_; }
    std::optional<TransactionToken> open() const noexcept { return open_; }
    // Every request carries a nonzero host logical sequence, strictly increasing
    // within the session; rejected requests change no state.
    Result<TransactionRecord> begin(TransactionMetadata, std::uint64_t sequence);
    // Joins an accepted edit to the explicitly supplied open token.
    std::vector<Diagnostic> edit(const TransactionToken&, std::uint64_t sequence);
    Result<TransactionRecord> commit(const TransactionToken&, std::uint64_t sequence);
    // The first request closes the token and returns its record. Until the next
    // begin, a repeated request for that token succeeds without a record.
    Result<std::optional<TransactionRecord>> rollback(const TransactionToken&, std::uint64_t sequence, RollbackReason);

private:
    std::vector<Diagnostic> check(const TransactionToken&, std::uint64_t sequence, bool allow_rolled_back) const;
    std::uint64_t identity_ = 0;
    std::uint64_t next_id_ = 1;
    std::uint64_t last_sequence_ = 0;
    std::optional<TransactionToken> open_;
    TransactionMetadata open_metadata_;
    std::size_t open_edits_ = 0;
    std::uint64_t rolled_back_ = 0; // Token closed by rollback since the last begin.
};

} // namespace tessera
