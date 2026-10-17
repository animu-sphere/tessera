#include "host.hpp"

namespace diagnostics {
using namespace tessera;
namespace {
Result<View> prepare(authoring::Session& session, const Snapshot& snapshot, const Disclosure& disclosure,
    const CommandSnapshot& commands, Point offset, TextShaper& text) {
    auto authored = session.lower(Panel(snapshot, disclosure), context);
    if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
    auto document = commands.apply_eligibility(authored.value->document, context);
    if (!document) return {std::nullopt, std::move(document.diagnostics)};
    auto tree = UiTree::create(*document.value, context);
    if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
    const auto sheet = panel_styles();
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    std::vector<Point> offsets((*tree.value)->size());
    offsets[(*tree.value)->find("renderer-diagnostics")->index] = offset;
    auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text, offsets});
    if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
    auto inspection = capture_inspection({tree.value->get(), *styles.value, &*layout.value, &authored.value->sources});
    if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
    auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
    return {View{commands, std::move(*tree.value), std::move(*styles.value), std::move(*layout.value),
        std::move(*inspection.value), std::move(*paint.value)}, {}};
}
Result<bool> completed(Result<CommandExecution> execution) {
    if (!execution) return {std::nullopt, std::move(execution.diagnostics)};
    if (execution.value->outcome.status != CommandOutcomeStatus::success)
        return {std::nullopt, std::move(execution.value->outcome.diagnostics)};
    return {true, {}};
}
}

Result<std::unique_ptr<Host>> Host::open(Snapshot snapshot, TextShaper& text) {
    auto registry = CommandRegistry::create({
        {actions[0], "Toggle CPU timings"}, {actions[1], "Toggle GPU timings"}, {actions[2], "Toggle counters"}},
        {{actions[0], actions[0]}, {actions[1], actions[1]}, {actions[2], actions[2]}});
    if (!registry) return {std::nullopt, std::move(registry.diagnostics)};
    auto host = std::unique_ptr<Host>(new Host(std::move(*registry.value)));
    auto staged = host->stage(snapshot, host->disclosure_, {}, {}, false, host->eligible_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    host->accept(std::move(*staged.value), std::move(snapshot), host->disclosure_, {}, host->eligible_);
    return {std::move(host), {}};
}

Result<View> Host::stage(const Snapshot& snapshot, const Disclosure& disclosure, Point offset,
    FocusDispatcher focus, bool reveal, std::array<bool, 3> eligible, TextShaper& text) {
    auto errors = validate(snapshot);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    // Attempt numbers are never reused, including candidates rejected by later stages.
    // The accepted command snapshot itself changes only in accept().
    auto commands = CommandSnapshot::publish(registry_, ++generation_, CommandMode::production,
        {{actions[0], {eligible[0]}}, {actions[1], {eligible[1]}}, {actions[2], {eligible[2]}}});
    if (!commands) return {std::nullopt, std::move(commands.diagnostics)};
    auto staged = prepare(session_, snapshot, disclosure, *commands.value, offset, text);
    if (!staged) return staged;
    auto recovered = focus.refresh(staged.value->hit());
    if (!recovered) return {std::nullopt, std::move(recovered.diagnostics)};
    if (reveal && recovered.value->focused) {
        auto updates = scroll_into_view(staged.value->hit(), *recovered.value->focused);
        if (!updates) return {std::nullopt, std::move(updates.diagnostics)};
        if (!updates.value->empty()) staged = prepare(session_, snapshot, disclosure, *commands.value, updates.value->front().offset, text);
    }
    return staged;
}

void Host::accept(View view, Snapshot snapshot, Disclosure disclosure, FocusDispatcher focus, std::array<bool, 3> eligible) {
    offset_ = view.layout.boxes[view.tree->find("renderer-diagnostics")->index].scroll->offset;
    snapshot_ = std::move(snapshot); disclosure_ = disclosure; eligible_ = eligible;
    accepted_ = std::move(view);
    focus_ = std::move(focus);
    (void)focus_.refresh(accepted_->hit());
    (void)pointers_.refresh(accepted_->hit()); // A replacement cancels every outstanding pointer press.
    session_.publish();
}

std::optional<std::string> Host::focused() const {
    auto copy = focus_;
    const auto refreshed = copy.refresh(view().hit());
    if (!refreshed || !refreshed.value->focused) return {};
    return view().tree->get(*refreshed.value->focused)->id;
}

Result<bool> Host::refresh(Snapshot snapshot, TextShaper& text) {
    auto staged = stage(snapshot, disclosure_, offset_, focus_, true, eligible_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), std::move(snapshot), disclosure_, focus_, eligible_);
    return {true, {}};
}
Result<bool> Host::eligibility(std::array<bool, 3> eligible, TextShaper& text) {
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible);
    return {true, {}};
}
Result<CommandInvocation> Host::request(const std::string& id, CommandSource source) {
    const auto owner = view().tree->find(id);
    if (!owner) return {std::nullopt, {{"telemetry_target", Severity::error, "/target/" + id, "Expected a live disclosure owner"}}};
    auto action = request_semantic_action(view().semantic(), *owner, "activate");
    if (!action) return {std::nullopt, std::move(action.diagnostics)};
    return view().commands.resolve_action(view().semantic(), *action.value, view().commands.generation(), {}, source, ++sequence_);
}

Result<CommandExecution> Host::execute(const CommandInvocation& invocation, TextShaper& text) {
    struct Handler final : CommandHost {
        Host& host;
        TextShaper& text;
        Disclosure disclosure;
        std::optional<View> candidate;
        Handler(Host& host, TextShaper& text) : host(host), text(text) {}
        CommandOutcome execute(const CommandInvocationRecord& call) override {
            ++host.disclosure_requests_;
            disclosure = host.disclosure_;
            for (std::size_t i = 0; i < groups.size(); ++i)
                if (call.command == actions[i]) disclosure.expanded[i] = !disclosure.expanded[i];
            auto staged = host.stage(host.snapshot_, disclosure, host.offset_, host.focus_, true, host.eligible_, text);
            if (!staged) return {CommandOutcomeStatus::error, {}, std::move(staged.diagnostics)};
            candidate = std::move(*staged.value);
            return {};
        }
    } handler(*this, text);
    // All borrowed old-view inputs remain alive through execution and outcome validation.
    const auto current = view().semantic();
    auto execution = executor_.execute(view().commands, invocation, handler, &current);
    if (execution && execution.value->outcome.status == CommandOutcomeStatus::success)
        accept(std::move(*handler.candidate), snapshot_, handler.disclosure, focus_, eligible_);
    return execution;
}

Result<bool> Host::invoke(const std::string& id, CommandSource source, TextShaper& text) {
    auto invocation = request(id, source);
    if (!invocation) return {std::nullopt, std::move(invocation.diagnostics)};
    return completed(execute(*invocation.value, text));
}
Result<bool> Host::operate(const ActionRequest& action, CommandSource source, TextShaper& text) {
    auto invocation = view().commands.resolve_action(view().semantic(), action, view().commands.generation(), {}, source, ++sequence_);
    if (!invocation) return {std::nullopt, std::move(invocation.diagnostics)};
    return completed(execute(*invocation.value, text));
}

Result<bool> Host::input(const InputEvent& event, CommandSource source, TextShaper& text) {
    auto errors = tessera::validate(event);
    if (timestamp_ && event.timestamp < *timestamp_)
        errors.push_back({"telemetry_timestamp", Severity::error, "/timestamp", "Expected a nondecreasing normalized event timestamp"});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    timestamp_ = event.timestamp;
    if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
        auto updates = route_scroll(view().hit(), *scroll);
        if (!updates) return {std::nullopt, std::move(updates.diagnostics)};
        if (updates.value->updates.empty()) return {false, {}};
        auto staged = stage(snapshot_, disclosure_, updates.value->updates.front().offset, focus_, false, eligible_, text);
        if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
        accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_);
        return {true, {}};
    }
    if (std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<KeyUp>(event.data) ||
        std::holds_alternative<tessera::TextInput>(event.data)) return {false, {}};
    if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
        std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
        auto pointers = pointers_;
        auto dispatched = pointers.dispatch(view().hit(), event);
        if (!dispatched) return {std::nullopt, std::move(dispatched.diagnostics)};
        for (const auto& action : dispatched.value->actions) return operate(action, source, text);
        pointers_ = std::move(pointers);
        return {dispatched.value->target.has_value() || !dispatched.value->pointers.empty(), {}};
    }
    auto focus = focus_;
    auto dispatched = focus.dispatch(view().hit(), event);
    if (!dispatched) return {std::nullopt, std::move(dispatched.diagnostics)};
    for (const auto& action : dispatched.value->actions) return operate(action, source, text);
    auto staged = stage(snapshot_, disclosure_, offset_, focus, true, eligible_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), snapshot_, disclosure_, std::move(focus), eligible_);
    return {true, {}};
}
} // namespace diagnostics
