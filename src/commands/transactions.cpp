#include <tessera/commands/transactions.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <atomic>
#include <limits>
#include <stdexcept>

namespace tessera {
namespace {
using detail::Checker;
void text(Checker& check, const std::string& s, const std::string& path, bool nonempty) {
    if (nonempty && s.empty()) check.error("empty_text", path, "Supply a nonempty UTF-8 value.");
    if (s.size() > max_command_string_bytes) check.error("out_of_range", path, "Reduce the value to at most 4096 bytes.");
    if (!detail::valid_utf8(s)) check.error("invalid_utf8", path, "Supply valid UTF-8.");
}
std::uint64_t next_session() {
    static std::atomic<std::uint64_t> next{1};
    auto identity = next.load(std::memory_order_relaxed);
    do {
        if (identity == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Transaction session identities exhausted.");
    } while (!next.compare_exchange_weak(identity, identity + 1, std::memory_order_relaxed));
    return identity;
}
} // namespace

TransactionSession::TransactionSession() : identity_(next_session()) {}

std::vector<Diagnostic> TransactionSession::check(const TransactionToken& token, std::uint64_t sequence,
                                                  bool allow_rolled_back) const {
    std::vector<Diagnostic> out;
    Checker check(out);
    if (sequence == 0 || sequence <= last_sequence_)
        check.error("transaction_sequence", "/sequence",
                    "Use a nonzero logical sequence greater than " + std::to_string(last_sequence_) + ".");
    if (token.session != identity_)
        check.error("foreign_transaction", "/token/session", "Use a token issued by this transaction session.");
    else if (token.id == 0 || token.id >= next_id_)
        check.error("unknown_transaction", "/token/id", "Use a token returned by begin.");
    else if (open_ != token && !(allow_rolled_back && token.id == rolled_back_))
        check.error("transaction_closed", "/token/id",
                    "Transaction " + std::to_string(token.id) + " is closed; a late request cannot reopen it.");
    return out;
}

Result<TransactionRecord> TransactionSession::begin(TransactionMetadata metadata, std::uint64_t sequence) {
    std::vector<Diagnostic> out;
    Checker check(out);
    if (sequence == 0 || sequence <= last_sequence_)
        check.error("transaction_sequence", "/sequence",
                    "Use a nonzero logical sequence greater than " + std::to_string(last_sequence_) + ".");
    if (open_)
        check.error("transaction_nested", "/token",
                    "Commit or roll back transaction " + std::to_string(open_->id) + " first; transactions are single-level.");
    text(check, metadata.label, "/metadata/label", true);
    check.enumeration(metadata.source, CommandSource::replay, "/metadata/source");
    if (metadata.timestamp && metadata.timestamp->count() < 0)
        check.error("out_of_range", "/metadata/timestamp", "Expected a host timestamp >= 0.");
    text(check, metadata.merge_hint, "/metadata/merge_hint", false);
    if (metadata.objects.size() > max_transaction_objects) {
        check.error("out_of_range", "/metadata/objects", "Supply at most 10000 affected object IDs.");
    } else {
        std::size_t index = 0;
        for (const auto& object : metadata.objects)
            text(check, object, "/metadata/objects/" + std::to_string(index++), true);
    }
    if (!out.empty()) return {{}, std::move(out)};
    if (next_id_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Transaction identities exhausted.");
    TransactionRecord record{{identity_, next_id_}, TransactionPhase::begin, sequence, std::move(metadata), 0, {}};
    open_metadata_ = record.metadata; // May throw before any state changes.
    ++next_id_;
    last_sequence_ = sequence;
    open_ = record.token;
    open_edits_ = 0;
    rolled_back_ = 0;
    return {std::move(record), {}};
}

std::vector<Diagnostic> TransactionSession::edit(const TransactionToken& token, std::uint64_t sequence) {
    auto out = check(token, sequence, false);
    if (out.empty()) { last_sequence_ = sequence; ++open_edits_; }
    return out;
}

Result<TransactionRecord> TransactionSession::commit(const TransactionToken& token, std::uint64_t sequence) {
    auto out = check(token, sequence, false);
    if (!out.empty()) return {{}, std::move(out)};
    TransactionRecord record{token, TransactionPhase::commit, sequence, open_metadata_, open_edits_, {}};
    last_sequence_ = sequence;
    open_.reset();
    return {std::move(record), {}};
}

Result<std::optional<TransactionRecord>> TransactionSession::rollback(
    const TransactionToken& token, std::uint64_t sequence, RollbackReason reason) {
    auto out = check(token, sequence, true);
    Checker(out).enumeration(reason, RollbackReason::command_failed, "/reason");
    if (!out.empty()) return {{}, std::move(out)};
    if (open_ != token) { // Already rolled back: request rollback only once.
        last_sequence_ = sequence;
        return {std::optional<TransactionRecord>{}, {}};
    }
    TransactionRecord record{token, TransactionPhase::rollback, sequence, open_metadata_, open_edits_, reason};
    last_sequence_ = sequence;
    open_.reset();
    rolled_back_ = token.id;
    return {std::optional<TransactionRecord>(std::move(record)), {}};
}

} // namespace tessera
