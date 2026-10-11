#include <tessera/commands/gestures.hpp>
#include <tessera/reactive/runtime.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <thread>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
CommandRegistry registry() {
    CommandDescriptor edit; edit.id = "inventory.amount"; edit.label = "Amount";
    CommandParameter item; item.name = "item"; item.type = CommandParameterType::object_id;
    CommandParameter amount; amount.name = "amount"; amount.type = CommandParameterType::number;
    amount.minimum = 0; amount.maximum = 100;
    edit.parameters = {item, amount};
    CommandDescriptor inspect; inspect.id = "inventory.inspect"; inspect.label = "Inspect";
    return *CommandRegistry::create({edit, inspect}, {{"edit", edit.id}}).value;
}
CommandSnapshot snapshot(const CommandRegistry& r, std::uint64_t generation = 1, bool enabled = true) {
    return *CommandSnapshot::publish(r, generation, CommandMode::production,
        {{"inventory.amount", {enabled}}, {"inventory.inspect", {true}}}, {"item-7"}).value;
}
CommandInvocation invocation(const CommandSnapshot& s, std::uint64_t sequence, double value = 2,
    CommandSource source = CommandSource::pointer) {
    return *s.resolve({s.generation(), "inventory.amount", {{"item", CommandObjectId{"item-7"}}, {"amount", value}}, source, sequence}).value;
}
EditGesture gesture(std::uint64_t id = 1) {
    return {id, PointerId{7}, PointerButton::primary, {"Change amount", "amount", {"item-7"}, {}}};
}
struct Host final : TransactionHost {
    double amount = 1, saved = 0;
    int begins = 0, edits = 0, commits = 0, inspections = 0;
    std::vector<double> history;
    std::vector<TransactionCancel> cancellations;
    std::optional<TransactionRecord> opening;
    std::function<CommandOutcome(std::string_view)> operation;
    CommandOutcome outcome(std::string_view phase) { return operation ? operation(phase) : CommandOutcome{}; }
    CommandOutcome begin(const TransactionRecord& record) override {
        ++begins; saved = amount; opening = record; return outcome("begin");
    }
    CommandOutcome execute_edit(const CommandInvocationRecord& record, const TransactionRecord& transaction) override {
        check(transaction == opening, "All updates retain the opening application transaction");
        ++edits; amount = std::get<double>(record.arguments.at("amount")); return outcome("edit");
    }
    CommandOutcome execute(const CommandInvocationRecord&) override { ++inspections; return {}; }
    CommandOutcome commit(const TransactionRecord& record) override {
        check(record == opening, "Release closes the opening transaction");
        ++commits; history.push_back(saved); return outcome("commit");
    }
    CommandOutcome rollback(const TransactionRecord& record, TransactionCancel reason) override {
        check(record == opening, "Cancellation closes the opening transaction");
        cancellations.push_back(reason); amount = saved; return outcome("rollback");
    }
};
struct Panel {
    UiDocument document;
    ValidationContext context{{"edit"}};
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Panel() {
        document.root.id = "panel";
        UiNode button; button.id = "amount"; button.properties["focusable"] = true; button.events["activate"] = "edit";
        document.root.children = {button};
        tree = std::move(*UiTree::create(document, context).value);
        styles.resize(tree->size()); styles[1].height = Dimension::points(32);
        layout = *compute_layout({tree.get(), styles, {240, 80}, &text}).value;
    }
    SemanticInput ui() const { return {tree.get(), styles, &layout}; }
    CommandInvocation resolve(const CommandSnapshot& s, std::uint64_t sequence) const {
        const auto input = ui();
        const auto action = request_semantic_action(input, *tree->find("amount"), "activate");
        return *s.resolve_action(input, *action.value, s.generation(),
            {{"item", CommandObjectId{"item-7"}}, {"amount", 2.0}}, CommandSource::accessibility, sequence).value;
    }
};
void continuous_presentation_and_release() {
    const auto r = registry(); auto s = snapshot(r); Host host; CommandTransactions transactions; CommandGestures gestures(transactions);
    ReactiveRuntime runtime; const auto value = runtime.signal(runtime.root(), "amount", host.amount);
    PlaceholderTextShaper text;
    for (std::uint64_t seq = 1; seq <= 3; ++seq) {
        s = snapshot(r, seq);
        const auto result = gestures.edit(gesture(), s, invocation(s, seq, double(seq + 1)), host);
        check(result && result.value->outcome.status == CommandOutcomeStatus::success && gestures.active() == 1,
            "Accepted updates begin once and join across generations");
        runtime.batch([&] { value.write(host.amount); });
        check(runtime.flush().empty(), "Edit settles before full-tree presentation");
        runtime.check_settled();
        const auto settled = runtime.snapshot(value);
        UiDocument document; document.root.kind = NodeKind::text;
        document.root.properties["text"] = "Amount: " + std::to_string(int(settled));
        const auto tree = UiTree::create(document); const std::vector<ResolvedStyle> styles((*tree.value)->size());
        const auto layout = compute_layout({tree.value->get(), styles, {240, 80}, &text});
        const auto semantics = build_semantic_tree({tree.value->get(), styles, &*layout.value});
        const auto paint = build_paint_list({tree.value->get(), styles, &*layout.value, &text});
        check(semantics && paint && semantics.value->nodes[0].name == std::get<std::string>(document.root.properties.at("text")),
            "Reactive edits agree with full-tree text, semantic name and paint");
        check(host.begins == 1 && host.commits == 0 && host.history.empty(), "Batches do not commit application history");
    }
    const auto inspect = s.resolve({s.generation(), "inventory.inspect", {}, CommandSource::agent, 4});
    check(bool(transactions.execute(s, *inspect.value, host)) && host.inspections == 1 && gestures.active(),
        "Non-editing commands share sequences without opening or closing a gesture");
    for (const auto event : {InputEvent{1us, PointerMove{{7}, {5, 5}}},
        InputEvent{2us, PointerUp{{8}, {5, 5}}}, InputEvent{3us, PointerUp{{7}, {5, 5}, PointerButton::secondary}}}) {
        const auto ignored = gestures.handle_input(s, event, host);
        check(ignored && !ignored.value->consumed && !ignored.value->closure && gestures.active(), "Unrelated input cannot complete another pointer/button");
    }
    const auto released = gestures.handle_input(s, {4us, PointerUp{{7}, {500, 500}}}, host);
    check(released && released.value->consumed && released.value->closure->status == CommandOutcomeStatus::success &&
        !gestures.active() && host.history == std::vector<double>{1} && host.amount == 4 && host.commits == 1,
        "Captured pointer release commits once even outside the original hit box");
    check(!gestures.handle_input(s, {3us, Cancel{}}, host) && host.commits == 1, "Out-of-order input changes no closure");
    const auto duplicate = gestures.handle_input(s, {4us, PointerUp{{7}, {500, 500}}}, host);
    check(duplicate && !duplicate.value->consumed && !duplicate.value->closure, "Repeated release enters no host boundary");
    check(has(gestures.edit(gesture(), s, invocation(s, 5), host).diagnostics, "stale_gesture", "/gesture/id"), "Late edit cannot reopen a completed gesture");
}
void cancellation_and_lifecycle() {
    const auto r = registry(); const auto s = snapshot(r);
    for (int mode = 0; mode < 3; ++mode) {
        Host host; CommandTransactions transactions; CommandGestures gestures(transactions);
        auto declaration = gesture();
        if (mode != 0) declaration.pointer.reset();
        const auto source = mode == 0 ? CommandSource::pointer : mode == 1 ? CommandSource::keyboard : CommandSource::gamepad;
        for (std::uint64_t seq = 1; seq <= 3; ++seq)
            check(bool(gestures.edit(declaration, s, invocation(s, seq, double(seq + 1), source), host)), "Each source joins accepted edits");
        const InputEvent event = mode == 0 ? InputEvent{1us, PointerCancel{{7}}} :
            mode == 1 ? InputEvent{1us, KeyDown{Key::escape}} : InputEvent{1us, Cancel{}};
        const auto cancelled = gestures.handle_input(s, event, host);
        const auto reason = mode == 0 ? TransactionCancel::capture_lost : TransactionCancel::user;
        check(cancelled && cancelled.value->consumed && cancelled.value->closure->status == CommandOutcomeStatus::success &&
            host.amount == 1 && host.history.empty() && host.cancellations == std::vector<TransactionCancel>{reason},
            "Capture loss, Escape and logical gamepad cancellation each restore all updates once");
        check(!gestures.handle_input(s, {0us, Cancel{}}, host), "Rejected input cannot roll back twice");
        check(!gestures.cancel(1, reason, host) && host.cancellations.size() == 1, "Explicit late cancellation cannot repeat rollback");
        check(bool(gestures.edit(gesture(2), s, invocation(s, 4), host)), "A fresh identity starts a later gesture");
        check(bool(gestures.cancel(2, TransactionCancel::owner_changed, host)), "Owner disposal explicitly closes before teardown");
    }
    for (int mode = 0; mode < 6; ++mode) {
        Host host; CommandTransactions transactions; CommandGestures gestures(transactions); Panel panel, replacement; auto ui = panel.ui();
        check(bool(gestures.edit(gesture(), s, panel.resolve(s, 1), host, &ui)), "Node-owned gesture starts through ordinary eligibility");
        auto next = mode == 0 ? snapshot(r, 2, false) : mode == 1 ? snapshot(registry(), 2) : snapshot(r, 2);
        auto next_ui = mode == 2 ? replacement.ui() : ui;
        if (mode == 4) panel.styles[1].visibility = Visibility::hidden;
        if (mode == 5) next = *CommandSnapshot::publish(r, 2, CommandMode::production, {{"inventory.amount", {true}}}).value;
        const auto invalidated = gestures.refresh(next, host, mode == 3 ? nullptr : &next_ui);
        check(!invalidated && !gestures.active() && host.commits == 0 && host.amount == 1 &&
            host.cancellations == std::vector<TransactionCancel>{TransactionCancel::owner_changed},
            "Update without input detects command, registry, tree, UI, visibility or object invalidation before publication");
        check(gestures.refresh(next, host).value == false && host.cancellations.size() == 1, "Invalidation closure cannot repeat on another update");
    }
}
CommandOutcome failed() { return {CommandOutcomeStatus::error, {}, {{"storage_failed", Severity::error, "/amount", "Write failed.", {}}}}; }
void rejection_and_host_failures() {
    const auto s = snapshot(registry()); Host host, other; CommandTransactions transactions; CommandGestures gestures(transactions);
    auto bad = gesture(0);
    check(has(gestures.edit(bad, s, invocation(s, 1), host).diagnostics, "invalid_gesture", "/gesture/id"), "Zero identities reject without host entry");
    bad = gesture(); bad.button = static_cast<PointerButton>(255);
    check(has(gestures.edit(bad, s, invocation(s, 1), host).diagnostics, "unknown_value", "/gesture/button"), "Malformed pointer declarations reject");
    check(host.begins == 0 && bool(gestures.edit(gesture(), s, invocation(s, 1), host)), "Valid first edit opens automatically");
    check(has(gestures.edit(gesture(2), s, invocation(s, 2), host).diagnostics, "nested_gesture", "/gesture/id"), "Nested gestures do not consume another edit");
    bad = gesture(); bad.pointer = PointerId{8};
    check(has(gestures.edit(bad, s, invocation(s, 2), host).diagnostics, "gesture_mismatch", "/gesture"), "Pointer ownership cannot drift");
    bad = gesture(); bad.metadata.label = "Other edit";
    check(has(gestures.edit(bad, s, invocation(s, 2), host).diagnostics, "gesture_mismatch", "/gesture"), "Opening metadata cannot drift");
    check(has(gestures.complete(2, s, host).diagnostics, "stale_gesture", "/gesture/id"), "Wrong terminal identity cannot close the edit");
    check(has(gestures.refresh(s, other).diagnostics, "foreign_host", "/transaction/host") && gestures.active(), "Foreign host cannot trigger lifecycle rollback");
    const InputEvent malformed{1us, PointerUp{{7}, {std::numeric_limits<float>::quiet_NaN(), 0}}};
    check(has(gestures.handle_input(s, malformed, host).diagnostics, "invalid_number", "/position/x") && gestures.active(), "Malformed terminal input preserves gesture and clock");
    check(bool(gestures.handle_input(s, {0us, KeyUp{Key::enter}}, host)) && gestures.active(), "Rejected input does not advance the accepted stream clock");
    check(has(gestures.cancel(1, static_cast<TransactionCancel>(255), host).diagnostics, "unknown_value", "/transaction/reason"), "Invalid cancellation reason preserves edit");
    std::vector<Diagnostic> wrong_thread;
    std::thread worker([&] { wrong_thread = gestures.cancel(1, TransactionCancel::user, host).diagnostics; }); worker.join();
    check(has(wrong_thread, "wrong_thread", "/gesture") && gestures.active(), "Wrong-thread entry enters no host boundary");
    host.operation = [&](std::string_view phase) {
        if (phase == "edit") check(has(gestures.cancel(1, TransactionCancel::user, host).diagnostics,
            "reentrant_gesture", "/gesture"), "Recursive host entry rejects before closure");
        return CommandOutcome{};
    };
    check(bool(gestures.edit(gesture(), s, invocation(s, 2), host)), "Outer edit survives recursive rejection");
    host.operation = {};
    check(bool(gestures.complete(1, s, host)), "Pointer-free explicit completion is available to every source");
    for (const std::string phase : {"begin", "edit", "commit", "rollback"}) {
        Host failing; CommandTransactions session; CommandGestures adapter(session);
        failing.operation = [&](std::string_view operation) { return operation == phase ? failed() : CommandOutcome{}; };
        const auto edited = adapter.edit(gesture(), s, invocation(s, 1), failing);
        if (phase == "begin") {
            check(!edited && !adapter.active() && failing.edits == 0, "Failed begin opens no gesture");
        } else if (phase == "edit") {
            check(edited && edited.value->outcome.status == CommandOutcomeStatus::error && !adapter.active() && failing.cancellations.size() == 1,
                "Command failure rolls back and closes the gesture");
        } else {
            const auto closed = phase == "commit" ? adapter.complete(1, s, failing) : adapter.cancel(1, TransactionCancel::user, failing);
            check(closed && closed.value->status == CommandOutcomeStatus::error && !adapter.active(), "Failed closure consumes gesture once");
            check(!adapter.cancel(1, TransactionCancel::user, failing), "Failed closure cannot be retried");
        }
        check(has(adapter.edit(gesture(), s, invocation(s, 2), failing).diagnostics, "stale_gesture", "/gesture/id"), "Entered identities are never reused after failure");
    }
    { Host failing; CommandTransactions session; CommandGestures adapter(session);
      check(bool(adapter.edit(gesture(), s, invocation(s, 1), failing)), "Edit before lifecycle failure succeeds");
      failing.operation = [](std::string_view phase) { return phase == "rollback" ? failed() : CommandOutcome{}; };
      const auto result = adapter.refresh(snapshot(registry(), 2), failing);
      check(!result && has(result.diagnostics, "stale_registry", "/registry_revision") &&
          has(result.diagnostics, "storage_failed", "/transaction/rollback/host/amount") && !adapter.active(), "Lifecycle rollback failure remains located and closes once"); }
    { Host failing; CommandTransactions session; CommandGestures adapter(session);
      failing.operation = [](std::string_view phase) -> CommandOutcome { if (phase == "edit") throw std::bad_alloc(); return {}; };
      bool threw = false; try { (void)adapter.edit(gesture(), s, invocation(s, 1), failing); } catch (const std::bad_alloc&) { threw = true; }
      check(threw && adapter.active() == 1, "Allocation failure preserves gesture identity for explicit recovery");
      failing.operation = {}; check(bool(adapter.cancel(1, TransactionCancel::user, failing)), "Host can roll back after allocation failure"); }
}
void replayed_owner_and_completion() {
    const auto r = registry(); const auto s = snapshot(r); Panel panel; auto ui = panel.ui();
    ReplayRecording recording; recording.context = panel.context; recording.document = panel.document;
    recording.styles = panel.styles; recording.viewport = {240, 80};
    recording.steps = {InputEvent{1us, PointerDown{{7}, {10, 10}}}, InputEvent{2us, PointerUp{{7}, {10, 10}}}};
    const auto encoded = save_replay(recording); const auto restored = load_replay(*encoded.value);
    const auto played = play_replay(*restored.value, panel.text);
    check(played && played.value->actions.size() == 1, "Serialized ordinary pointer replay yields one edit owner");
    const auto& action = played.value->actions[0];
    const ActionRequest request{action.binding, action.action, *panel.tree->find(*action.id)};
    const auto resolved = s.resolve_action(ui, request, s.generation(),
        {{"item", CommandObjectId{"item-7"}}, {"amount", 4.0}}, CommandSource::replay, 1);
    Host host; CommandTransactions transactions; CommandGestures gestures(transactions);
    check(bool(gestures.edit(gesture(), s, *resolved.value, host, &ui)), "Replay command uses ordinary gesture execution eligibility");
    const auto event = std::get<InputEvent>(restored.value->steps.back());
    const auto completed = gestures.handle_input(s, event, host, &ui);
    check(completed && completed.value->consumed && host.commits == 1 && host.history.size() == 1 && host.amount == 4,
        "Normalized recorded release closes one application edit without a token schema");
}
} // namespace
int main() {
    try {
        continuous_presentation_and_release(); cancellation_and_lifecycle(); rejection_and_host_failures(); replayed_owner_and_completion();
        std::cout << "Command gesture boundary checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
