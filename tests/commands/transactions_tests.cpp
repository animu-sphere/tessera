#include <tessera/commands/transactions.hpp>
#include <tessera/reactive/runtime.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include "../check.hpp"
#include <functional>
#include <iostream>
#include <thread>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
CommandRegistry registry() {
    CommandDescriptor edit; edit.id = "inventory.set_amount"; edit.label = "Set amount";
    CommandParameter item; item.name = "item"; item.type = CommandParameterType::object_id;
    CommandParameter amount; amount.name = "amount"; amount.type = CommandParameterType::number;
    amount.minimum = 0; amount.maximum = 100;
    edit.parameters = {item, amount};
    CommandDescriptor inspect; inspect.id = "inventory.inspect"; inspect.label = "Inspect";
    return *CommandRegistry::create({edit, inspect}, {{"set-amount", edit.id}}).value;
}
CommandSnapshot snapshot(const CommandRegistry& registry, std::uint64_t generation = 1, bool enabled = true) {
    return *CommandSnapshot::publish(registry, generation, CommandMode::production,
        {{"inventory.set_amount", {enabled}}, {"inventory.inspect", {true}}}, {"item-7"}).value;
}
CommandInvocation invocation(const CommandSnapshot& snapshot, std::uint64_t sequence = 1, double amount = 2,
                             CommandSource source = CommandSource::pointer) {
    return *snapshot.resolve({snapshot.generation(), "inventory.set_amount",
        {{"item", CommandObjectId{"item-7"}}, {"amount", amount}}, source, sequence}).value;
}
TransactionMetadata metadata() { return {"Change amount 数量", "amount", {"item-7"}, 123}; }
CommandOutcome error_outcome() {
    return {CommandOutcomeStatus::error, {}, {{"storage_failed", Severity::error, "/item", "Host storage failed.", 3}}};
}
struct Host final : TransactionHost {
    double amount = 1, saved = 0;
    std::vector<double> history;
    std::vector<TransactionRecord> begins;
    std::vector<TransactionCancel> cancellations;
    int edits = 0, commits = 0, inspections = 0;
    std::function<CommandOutcome(std::string_view)> operation;
    CommandOutcome outcome(std::string_view phase) { return operation ? operation(phase) : CommandOutcome{}; }
    CommandOutcome begin(const TransactionRecord& record) override {
        begins.push_back(record); saved = amount; return outcome("begin");
    }
    CommandOutcome execute_edit(const CommandInvocationRecord& record, const TransactionRecord& token) override {
        check(token == begins.back(), "Every edit must carry the same application transaction identity");
        ++edits; amount = std::get<double>(record.arguments.at("amount")); return outcome("edit");
    }
    CommandOutcome execute(const CommandInvocationRecord&) override { ++inspections; return {}; }
    CommandOutcome commit(const TransactionRecord& record) override {
        check(record == begins.back(), "Commit must carry opening metadata");
        ++commits; history.push_back(saved); return outcome("commit");
    }
    CommandOutcome rollback(const TransactionRecord& record, TransactionCancel reason) override {
        check(record == begins.back(), "Rollback must carry opening metadata");
        cancellations.push_back(reason); amount = saved; return outcome("rollback");
    }
};
struct Panel {
    UiDocument document;
    ValidationContext context{{"set-amount"}};
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    explicit Panel(const CommandSnapshot& snapshot) {
        document.root.id = "panel";
        UiNode button; button.id = "edit"; button.properties["focusable"] = true; button.events["activate"] = "set-amount";
        document.root.children = {button};
        const auto lowered = snapshot.apply_eligibility(document, context);
        tree = std::move(*UiTree::create(*lowered.value, context).value);
        styles.resize(tree->size()); styles[1].height = Dimension::points(32);
        layout = *compute_layout({tree.get(), styles, {240, 80}, &text}).value;
    }
    SemanticInput ui() const { return {tree.get(), styles, &layout}; }
    CommandInvocation resolve(const CommandSnapshot& snapshot, std::uint64_t sequence, double amount = 2) const {
        const auto input = ui();
        const auto action = request_semantic_action(input, *tree->find("edit"), "activate");
        return *snapshot.resolve_action(input, *action.value, snapshot.generation(),
            {{"item", CommandObjectId{"item-7"}}, {"amount", amount}}, CommandSource::keyboard, sequence).value;
    }
};
void continuous_and_non_editing() {
    const auto r = registry(); auto s = snapshot(r); Host host; CommandTransactions transactions;
    ReactiveRuntime runtime; const auto observed = runtime.signal(runtime.root(), "amount", host.amount);
    int notifications = 0;
    runtime.effect<double>(runtime.root(), "amount", EffectPhase::notify, [&] { return observed.read(); },
        [&](double value) { check(value == host.amount, "Published view must agree with host edit"); ++notifications; return [] {}; });
    auto token = transactions.begin(s, invocation(s), metadata(), host);
    check(token && token.value->active() && host.begins.size() == 1, "First accepted edit begins once");
    for (std::uint64_t seq = 1; seq <= 3; ++seq) {
        s = snapshot(r, seq);
        const auto result = transactions.execute_edit(s, invocation(s, seq, double(seq + 1)), *token.value, host);
        check(result && result.value->outcome.status == CommandOutcomeStatus::success, "New generations join explicitly");
        runtime.batch([&] { observed.write(host.amount); });
        check(runtime.flush().empty() && runtime.publish() == 1 && runtime.deliver_effects(EffectPhase::notify).empty(),
            "Each edit batch settles and publishes coherently without committing the gesture");
    }
    check(host.commits == 0 && notifications == 3, "Update batches must not create history entries");
    const auto inspect = s.resolve({s.generation(), "inventory.inspect", {}, CommandSource::agent, 4});
    check(bool(transactions.execute(s, *inspect.value, host)) && host.inspections == 1 && token.value->active(),
        "Non-editing command does not join or close the active edit");
    const auto committed = transactions.commit(s, *token.value, host);
    check(committed && committed.value->status == CommandOutcomeStatus::success && !token.value->active() &&
        host.amount == 4 && host.history == std::vector<double>{1} && host.begins.size() == 1, "One gesture produces one history entry");
    check(has(transactions.rollback(*token.value, TransactionCancel::user, host).diagnostics, "closed_transaction", "/transaction/token"),
        "Closed token cannot request a second rollback");
    check(has(transactions.execute_edit(s, invocation(s, 5), *token.value, host).diagnostics, "closed_transaction", "/transaction/token") && host.edits == 3,
        "A late command cannot reopen the token");
    check(has(transactions.execute(s, *inspect.value, host).diagnostics, "consumed_sequence", "/sequence"), "Both execution routes share deduplication");
}
void cancellation_and_failures() {
    const auto s = snapshot(registry());
    for (const auto reason : {TransactionCancel::user, TransactionCancel::capture_lost, TransactionCancel::owner_changed}) {
        CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
        for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
            check(bool(transactions.execute_edit(s, invocation(s, sequence, double(sequence + 1)), *token.value, host)), "Gesture edits execute");
        const auto result = transactions.rollback(*token.value, reason, host);
        check(result && result.value->status == CommandOutcomeStatus::success && host.amount == 1 && host.history.empty() &&
            host.cancellations == std::vector<TransactionCancel>{reason} && !token.value->active(), "Cancelled multi-update edit rolls back exactly once");
    }
    for (const int failure : {0, 1, 2, 3}) {
        CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
        host.operation = [failure](std::string_view phase) -> CommandOutcome {
            if (phase != "edit") return {};
            if (failure == 0) return error_outcome();
            if (failure == 1) return {CommandOutcomeStatus::cancelled, {}, {}};
            if (failure == 2) return {CommandOutcomeStatus::success, {{"undeclared", true}}, {}};
            throw std::runtime_error("Edit failed");
        };
        const auto result = transactions.execute_edit(s, invocation(s), *token.value, host);
        check(result && result.value->outcome.status != CommandOutcomeStatus::success && host.amount == 1 &&
            host.cancellations.size() == 1 && !token.value->active(), "Cancellation, host/result failure and exceptions each roll back once");
    }
    CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
    host.operation = [](std::string_view phase) { return phase == "edit" || phase == "rollback" ? error_outcome() : CommandOutcome{}; };
    const auto failed = transactions.execute_edit(s, invocation(s), *token.value, host);
    check(failed && has(failed.value->outcome.diagnostics, "storage_failed", "/result/host/item") &&
        has(failed.value->outcome.diagnostics, "storage_failed", "/transaction/rollback/host/item") && !token.value->active(),
        "Rollback failure is reported with the command failure without claiming restoration");
}
void token_and_entry_rejection() {
    const auto s = snapshot(registry()); CommandTransactions transactions, foreign; Host host, other;
    auto bad = metadata(); bad.label.clear();
    check(has(transactions.begin(s, invocation(s), bad, host).diagnostics, "invalid_metadata", "/transaction/metadata/label") && host.begins.empty(), "Invalid metadata never enters host");
    bad = metadata(); bad.affected_objects.insert("missing");
    check(has(transactions.begin(s, invocation(s), bad, host).diagnostics, "invalid_reference", "/transaction/metadata/affected_objects"), "Affected IDs use settled permissions");
    bad = metadata(); bad.merge_hint = std::string(1, char(0xff));
    check(has(transactions.begin(s, invocation(s), bad, host).diagnostics, "invalid_metadata", "/transaction/metadata/merge_hint"), "Invalid UTF-8 cannot enter transaction metadata");
    bad = metadata(); bad.label.assign(4097, 'x');
    check(has(transactions.begin(s, invocation(s), bad, host).diagnostics, "invalid_metadata", "/transaction/metadata/label"), "Metadata strings are bounded before copying");
    bad = metadata(); for (int i = 0; i < 64; ++i) bad.affected_objects.insert(std::to_string(i));
    check(has(transactions.begin(s, invocation(s), bad, host).diagnostics, "out_of_range", "/transaction/metadata/affected_objects"), "Metadata is bounded before copy");
    auto token = transactions.begin(s, invocation(s), metadata(), host);
    check(has(transactions.begin(s, invocation(s, 2), metadata(), host).diagnostics, "nested_transaction", "/transaction"), "Nested begin is rejected");
    check(has(foreign.rollback(*token.value, TransactionCancel::user, host).diagnostics, "foreign_transaction", "/transaction/token"), "Foreign session cannot consume token");
    check(has(transactions.rollback(*token.value, TransactionCancel::user, other).diagnostics, "foreign_host", "/transaction/host") && token.value->active(), "Host replacement rejects without changing edit");
    check(has(transactions.rollback(*token.value, static_cast<TransactionCancel>(255), host).diagnostics, "unknown_value", "/transaction/reason"), "Unknown reason rejects without closure");
    std::vector<Diagnostic> wrong_thread;
    std::thread worker([&] { wrong_thread = transactions.rollback(*token.value, TransactionCancel::user, host).diagnostics; }); worker.join();
    check(has(wrong_thread, "wrong_thread", "/transaction") && token.value->active(), "Wrong-thread entry never calls host");
    host.operation = [&](std::string_view phase) {
        if (phase == "edit") check(has(transactions.rollback(*token.value, TransactionCancel::user, host).diagnostics,
            "reentrant_transaction", "/transaction"), "Host cannot recursively enter the transaction session");
        return CommandOutcome{};
    };
    check(bool(transactions.execute_edit(s, invocation(s), *token.value, host)), "Outer edit survives recursive rejection");
    check(bool(transactions.commit(s, *token.value, host)), "Commit closes successful edit");
    check(has(transactions.begin(s, invocation(s), metadata(), host).diagnostics, "consumed_sequence", "/sequence"), "Opening request cannot begin twice");
    const auto reserved = transactions.begin(s, invocation(s, 3), metadata(), host);
    check(bool(reserved), "New sequence begins a new transaction");
    check(has(transactions.execute(s, invocation(s, 3), host).diagnostics, "consumed_sequence", "/sequence"), "Opening sequence is reserved for explicit editing");
    check(bool(transactions.rollback(*reserved.value, TransactionCancel::user, host)), "Unused reserved edit can cancel");
    std::optional<TransactionToken> expired;
    { CommandTransactions temporary; expired = *temporary.begin(s, invocation(s), metadata(), other).value; }
    check(!expired->active() && other.cancellations.empty(), "Session destruction expires weak tokens without calling host");
}
void invalidation_and_closure_outcomes() {
    const auto r = registry(); const auto s = snapshot(r); Panel panel(s); auto ui = panel.ui();
    for (int mode = 0; mode < 4; ++mode) {
        CommandTransactions transactions; Host host; auto token = transactions.begin(s, panel.resolve(s, 1), metadata(), host, &ui);
        check(bool(transactions.execute_edit(s, panel.resolve(s, 1), *token.value, host, &ui)), "Live owner executes edit");
        auto next = mode == 0 ? snapshot(r, 2, false) : mode == 1 ? snapshot(registry(), 2) : snapshot(r, 2);
        Panel replacement(next); auto replaced_ui = replacement.ui();
        const auto result = transactions.commit(next, *token.value, host, mode == 2 ? &replaced_ui : mode == 3 ? nullptr : &ui);
        check(result && result.value->status == CommandOutcomeStatus::error && !token.value->active() &&
            host.cancellations == std::vector<TransactionCancel>{TransactionCancel::owner_changed} && host.commits == 0,
            "Disabled, replaced registry/owner, and missing owner snapshot invalidate and roll back before commit");
    }
    { CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
      const auto next = snapshot(r, 2); const auto result = transactions.execute_edit(next, invocation(s, 2), *token.value, host);
      check(!result && has(result.diagnostics, "stale_generation", "/generation") && host.edits == 0 && host.cancellations.size() == 1,
          "Stale dispatch rejects and rolls back before entering the edit host"); }
    { CommandTransactions transactions; Host host;
      const auto stale = transactions.begin(snapshot(r, 2), invocation(s), metadata(), host);
      check(!stale && has(stale.diagnostics, "stale_generation", "/generation") && host.begins.empty(), "Stale begin never enters the host"); }
    { CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
      const auto revoked = *CommandSnapshot::publish(r, 2, CommandMode::production, {{"inventory.set_amount", {true}}}).value;
      const auto result = transactions.commit(revoked, *token.value, host);
      check(result && has(result.value->diagnostics, "invalid_reference", "/transaction/metadata/affected_objects") && !token.value->active(),
          "Revoking an affected object invalidates the token even when the command remains enabled"); }
    { CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
      const auto result = transactions.execute_edit(s, panel.resolve(s, 2), *token.value, host, &ui);
      check(!result && has(result.diagnostics, "transaction_owner_mismatch", "/transaction/token") && host.edits == 0 && host.cancellations.size() == 1,
          "A node-free gesture cannot silently acquire another action owner"); }
    { CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
      const auto result = transactions.commit(s, *token.value, host);
      check(result && has(result.value->diagnostics, "empty_transaction", "/transaction") && host.commits == 0 && host.cancellations.size() == 1,
          "Empty edit cannot create history"); }
    for (const std::string phase : {"begin", "commit", "rollback"}) {
        for (int mode = 0; mode < 4; ++mode) {
            CommandTransactions transactions; Host host;
            host.operation = [phase, mode](std::string_view step) -> CommandOutcome {
                if (step != phase) return {};
                if (mode == 0) return error_outcome();
                if (mode == 1) return {CommandOutcomeStatus::success, {{"unexpected", true}}, {}};
                if (mode == 3) return {CommandOutcomeStatus::cancelled, {}, {}};
                throw std::runtime_error("Boundary failed");
            };
            auto token = transactions.begin(s, invocation(s), metadata(), host);
            if (phase == "begin") {
                check(!token && !token.diagnostics.empty(), "Failed begin issues no token");
                check(!transactions.begin(s, invocation(s), metadata(), host) && host.begins.size() == 1, "Entered failed begin cannot retry");
            } else {
                check(bool(transactions.execute_edit(s, invocation(s), *token.value, host)), "Edit succeeds before failed closure");
                const auto closed = phase == "commit" ? transactions.commit(s, *token.value, host) :
                    transactions.rollback(*token.value, TransactionCancel::user, host);
                check(closed && closed.value->status == CommandOutcomeStatus::error && !token.value->active(), "Failed or malformed closure consumes token once");
            }
        }
    }
    { CommandTransactions transactions; Host host; auto token = transactions.begin(s, invocation(s), metadata(), host);
      host.operation = [](std::string_view phase) -> CommandOutcome { if (phase == "edit") throw std::bad_alloc(); return {}; };
      bool threw = false; try { (void)transactions.execute_edit(s, invocation(s), *token.value, host); } catch (const std::bad_alloc&) { threw = true; }
      check(threw && token.value->active(), "Allocation failure leaves edit open for explicit host recovery");
      host.operation = {}; check(bool(transactions.rollback(*token.value, TransactionCancel::command_failed, host)), "Recovery explicitly rolls back"); }
}
void replayed_discrete_edits() {
    const auto s = snapshot(registry()); Panel panel(s); const auto ui = panel.ui();
    ReplayRecording recording; recording.context = panel.context; recording.viewport = {240, 80};
    recording.document = panel.document; recording.styles = panel.styles;
    recording.steps = {InputEvent{1us, PointerDown{{1}, {10, 10}}}, InputEvent{2us, PointerUp{{1}, {10, 10}}},
        InputEvent{3us, FocusNext{}}, InputEvent{4us, Activate{}}, InputEvent{5us, Navigate{Direction::down}},
        InputEvent{6us, Activate{}}, ReplaySemanticAction{AuthorIdTarget{"edit"}}};
    const auto encoded = save_replay(recording); check(bool(encoded), "UI input fixture serializes");
    const auto loaded = load_replay(*encoded.value); check(bool(loaded), "UI recording round trips");
    const auto first = play_replay(*loaded.value, panel.text), second = play_replay(*loaded.value, panel.text);
    check(first && second && compare_replay(*first.value, *second.value).empty() && first.value->actions.size() == 4,
        "Serialized replay deterministically observes all ordinary action routes");
    CommandTransactions transactions; Host host; std::uint64_t seq = 0;
    for (const auto& action : first.value->actions) {
        const auto owner = request_semantic_action(ui, *panel.tree->find(*action.id), action.binding);
        const auto command = s.resolve_action(ui, *owner.value, s.generation(),
            {{"item", CommandObjectId{"item-7"}}, {"amount", 7.0}}, CommandSource::replay, ++seq);
        auto token = transactions.begin(s, *command.value, metadata(), host, &ui);
        const auto edit = transactions.execute_edit(s, *command.value, *token.value, host, &ui);
        const auto committed = transactions.commit(s, *token.value, host, &ui);
        check(edit && committed && committed.value->status == CommandOutcomeStatus::success &&
            edit.value->invocation.arguments == command.value->record().arguments, "Replayed edits use ordinary eligibility and transaction boundary");
    }
    check(host.begins.size() == 4 && host.edits == 4 && host.commits == 4 && host.cancellations.empty(), "Every discrete accepted edit has one begin/commit pair");
}
void direct_sources() {
    const auto s = snapshot(registry()); CommandTransactions transactions; Host host; std::uint64_t sequence = 0;
    const auto inspect = *s.resolve({s.generation(), "inventory.inspect", {}, CommandSource::agent, ++sequence}).value;
    check(bool(transactions.execute(s, inspect, host)) && host.begins.empty(), "Non-editing commands request no transaction");
    for (const auto source : {CommandSource::pointer, CommandSource::keyboard, CommandSource::gamepad,
                             CommandSource::accessibility, CommandSource::agent, CommandSource::replay}) {
        const auto command = invocation(s, ++sequence, 7, source);
        auto token = transactions.begin(s, command, metadata(), host);
        const auto edit = transactions.execute_edit(s, command, *token.value, host);
        const auto committed = transactions.commit(s, *token.value, host);
        check(edit && edit.value->outcome.status == CommandOutcomeStatus::success && committed &&
            committed.value->status == CommandOutcomeStatus::success && host.begins.back().opening.source == source,
            "Every source shares edit execution, metadata and closure");
    }
    check(host.history.size() == 6 && host.inspections == 1 && host.cancellations.empty(), "Ordinary sources have identical transaction accounting");
}
} // namespace
int main() {
    try {
        continuous_and_non_editing(); cancellation_and_failures(); token_and_entry_rejection();
        invalidation_and_closure_outcomes(); replayed_discrete_edits(); direct_sources();
        std::cout << "Command transaction boundary checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
