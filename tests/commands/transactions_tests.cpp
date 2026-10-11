#include <tessera/commands/transactions.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
TransactionMetadata metadata(std::string label) {
    TransactionMetadata m;
    m.label = std::move(label);
    m.source = CommandSource::pointer;
    m.timestamp = 5us;
    m.merge_hint = "drag";
    m.objects = {"item-7"};
    return m;
}

void discrete_and_continuous() {
    TransactionSession session;
    check(session.identity() != 0 && !session.open(), "A session starts without an open transaction");
    const auto begun = session.begin(metadata("Rename item"), 1);
    check(begun && begun.value->phase == TransactionPhase::begin && begun.value->token.session == session.identity() &&
          begun.value->token.id == 1 && session.open() == begun.value->token, "Begin opens one token");
    const auto token = begun.value->token;
    check(session.edit(token, 2).empty(), "A discrete command joins its own token");
    const auto committed = session.commit(token, 3);
    check(committed && committed.value->phase == TransactionPhase::commit && committed.value->edits == 1 &&
          committed.value->metadata == metadata("Rename item") && committed.value->sequence == 3 && !session.open(),
          "Commit closes the token and reports begin metadata and edit count");

    // A 100-update continuous drag is one transaction.
    const auto drag = session.begin(metadata("Move item"), 10);
    check(drag && drag.value->token.id == 2, "Token identities are never reused");
    for (std::uint64_t update = 0; update < 100; ++update)
        check(session.edit(drag.value->token, 11 + update).empty(), "Each update joins the gesture token");
    const auto done = session.commit(drag.value->token, 200);
    check(done && done.value->edits == 100, "A continuous gesture commits once");
}

void rollback_once_and_late_requests() {
    TransactionSession session;
    const auto token = session.begin(metadata("Scrub quantity"), 1).value->token;
    check(session.edit(token, 2).empty() && session.edit(token, 3).empty(), "Edits join before cancellation");
    const auto escape = session.rollback(token, 4, RollbackReason::cancelled);
    check(escape && escape.value->has_value() && (*escape.value)->phase == TransactionPhase::rollback &&
          (*escape.value)->reason == RollbackReason::cancelled && (*escape.value)->edits == 2 && !session.open(),
          "The first rollback request closes the token with its reason");
    const auto capture = session.rollback(token, 5, RollbackReason::capture_lost);
    check(capture && !capture.value->has_value(), "A repeated rollback request produces no second rollback");
    check(has(session.edit(token, 6), "transaction_closed", "/token/id"), "A rolled-back token rejects edits");
    check(has(session.commit(token, 7).diagnostics, "transaction_closed", "/token/id"),
          "A late completion cannot commit a rolled-back token");

    const auto next = session.begin(metadata("Rename item"), 8).value->token;
    check(has(session.rollback(token, 9, RollbackReason::reload).diagnostics, "transaction_closed", "/token/id"),
          "After another begin, an old rollback is a late request");
    check(session.open() == next, "A late request does not affect the open token");
    check(session.commit(next, 10) && has(session.rollback(next, 11, RollbackReason::owner_removed).diagnostics,
          "transaction_closed", "/token/id"), "A committed token cannot be rolled back");
}

void rejected_requests_change_nothing() {
    TransactionSession session, other;
    const auto token = session.begin(metadata("Move item"), 5).value->token;
    const auto nested = session.begin(metadata("Nested"), 6);
    check(!nested && has(nested.diagnostics, "transaction_nested", "/token") && session.open() == token,
          "Nested begin fails without opening a second token");
    check(has(session.edit(token, 5), "transaction_sequence", "/sequence") &&
          has(session.edit(token, 0), "transaction_sequence", "/sequence"), "Sequences strictly increase");
    const auto foreign = other.begin(metadata("Other"), 1).value->token;
    check(has(session.edit(foreign, 7), "foreign_transaction", "/token/session") &&
          has(other.edit(token, 2), "foreign_transaction", "/token/session"), "Tokens belong to one session");
    check(has(session.edit({session.identity(), 9}, 8), "unknown_transaction", "/token/id") &&
          has(session.edit({session.identity(), 0}, 8), "unknown_transaction", "/token/id"),
          "Unissued tokens are unknown");
    check(has(session.rollback(token, 8, static_cast<RollbackReason>(99)).diagnostics, "unknown_value", "/reason"),
          "Rollback reasons are declared values");
    // Rejected requests consumed neither sequences nor edits.
    const auto done = session.commit(token, 6);
    check(done && done.value->edits == 0, "Rejected requests change no state");

    TransactionMetadata bad;
    bad.source = static_cast<CommandSource>(42);
    bad.timestamp = -1us;
    bad.merge_hint = std::string("\xff", 1);
    bad.objects = {""};
    const auto invalid = session.begin(bad, 7);
    check(!invalid && has(invalid.diagnostics, "empty_text", "/metadata/label") &&
          has(invalid.diagnostics, "unknown_value", "/metadata/source") &&
          has(invalid.diagnostics, "out_of_range", "/metadata/timestamp") &&
          has(invalid.diagnostics, "invalid_utf8", "/metadata/merge_hint") &&
          has(invalid.diagnostics, "empty_text", "/metadata/objects/0"), "Metadata failures are located");
    TransactionMetadata many = metadata("Many");
    for (std::size_t i = 0; i <= max_transaction_objects; ++i) many.objects.insert("o" + std::to_string(i));
    check(has(session.begin(many, 7).diagnostics, "out_of_range", "/metadata/objects"), "Affected objects are bounded");
    const auto after = session.begin(metadata("After"), 7);
    check(after && after.value->token.id == 2, "Rejected begins allocate no token");
}
} // namespace

int main() {
    try {
        discrete_and_continuous();
        rollback_once_and_late_requests();
        rejected_requests_change_nothing();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Transaction checks passed\n";
}
