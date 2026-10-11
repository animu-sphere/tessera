#include <tessera/commands/shortcuts.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
CommandRegistry registry() {
    std::vector<CommandDescriptor> descriptors;
    for (const auto* id : {"app.global", "app.outer", "app.inner", "app.other", "app.debug"}) {
        CommandDescriptor d; d.id = id; d.label = id;
        if (d.id == "app.debug") d.availability = CommandAvailability::development;
        CommandParameter amount; amount.name = "amount"; amount.type = CommandParameterType::number;
        amount.minimum = 0; amount.maximum = 10; amount.default_value = 2.0;
        CommandParameter item; item.name = "item"; item.type = CommandParameterType::object_id;
        item.default_value = CommandObjectId{"item-7"};
        d.parameters = {amount, item}; d.results = {amount};
        descriptors.push_back(d);
    }
    auto created = CommandRegistry::create(descriptors,
        {{"outer", "app.outer"}, {"inner", "app.inner"}, {"other", "app.other"}, {"debug", "app.debug"}});
    check(bool(created), "Fixture registry must create"); return *created.value;
}
CommandSnapshot snapshot(const CommandRegistry& registry, std::uint64_t generation = 1,
    bool enabled = true, CommandMode mode = CommandMode::development, CommandObjectIds objects = {"item-7"}) {
    auto published = CommandSnapshot::publish(registry, generation, mode,
        {{"app.global", {true}}, {"app.outer", {true}}, {"app.inner", {enabled}},
         {"app.other", {true}}, {"app.debug", {true}}}, std::move(objects));
    check(bool(published), "Fixture snapshot must publish"); return *published.value;
}
UiNode button(std::string id) {
    UiNode node; node.id = id; node.events["activate"] = id; node.properties["focusable"] = true;
    UiNode label; label.kind = NodeKind::text; label.properties["text"] = id;
    node.children.push_back(label); return node;
}
UiDocument document() {
    UiDocument d; d.root.id = "panel";
    UiNode inner; inner.id = "inner-scope"; inner.properties["focusable"] = true;
    inner.children = {button("inner"), button("debug")};
    UiNode outer; outer.id = "outer-scope"; outer.properties["focusable"] = true;
    outer.children = {button("outer"), inner};
    d.root.children = {outer, button("other")}; return d;
}
struct Panel {
    ValidationContext context{{"outer", "inner", "other", "debug"}};
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    explicit Panel(UiDocument d = document()) {
        auto created = UiTree::create(d, context);
        check(bool(created), "Fixture tree must create"); tree = std::move(*created.value);
        styles.resize(tree->size());
        for (const auto* id : {"outer", "inner", "other", "debug"})
            styles[node(id).index].height = Dimension::points(32);
        relayout();
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    ActionRequest action(std::string_view id) const { return {"activate", std::string(id), node(id)}; }
    void relayout() {
        auto result = compute_layout({tree.get(), styles, {240, 240}, &text});
        check(bool(result), "Fixture layout must compute"); layout = std::move(*result.value);
    }
    SemanticInput ui(std::optional<NodeHandle> focus = {}) const { return {tree.get(), styles, &layout, focus}; }
};
ShortcutChord chord(Key key = Key::i) { return {key, {false, true, false, false}}; }
std::vector<ShortcutScope> scopes(const Panel& panel) {
    return {{std::nullopt, {{chord(), "app.global"}}},
        {panel.node("outer-scope"), {{chord(), "app.outer", panel.action("outer")}}},
        {panel.node("inner-scope"), {{chord(), "app.inner", panel.action("inner")}}}};
}
CommandShortcuts map(const CommandSnapshot& commands, const Panel& panel, const std::vector<ShortcutScope>& input) {
    auto created = CommandShortcuts::create(commands, *panel.tree, input);
    check(bool(created), "Fixture shortcut map must create"); return *created.value;
}
ShortcutRequest request(const CommandSnapshot& s, std::uint64_t sequence = 1) {
    return {{1us, KeyDown{Key::i, chord().modifiers}}, s.generation(), sequence, {}};
}
struct Host final : CommandHost {
    int calls = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        ++calls; return {CommandOutcomeStatus::success, {{"amount", invocation.arguments.at("amount")}}, {}};
    }
};

void routing() {
    auto r = registry(); auto s = snapshot(r); Panel panel;
    auto input = scopes(panel); auto shortcuts = map(s, panel, input);
    const auto routed = [&](std::optional<NodeHandle> focus, std::string_view expected) {
        auto result = shortcuts.dispatch(s, panel.ui(focus), request(s));
        check(result && result.value->consumed && result.value->invocation &&
            result.value->invocation->record().command == expected, "Choose nearest matching scope");
        const auto& record = result.value->invocation->record();
        check(record.source == CommandSource::keyboard && record.generation == s.generation() &&
            record.registry_revision == r.revision() && record.sequence == 1 &&
            std::get<double>(record.arguments.at("amount")) == 2 &&
            std::get<CommandObjectId>(record.arguments.at("item")).value == "item-7", "Use ordinary invocation/defaults");
        check(record.owner.has_value() == (expected != "app.global"), "Retain subtree activation ownership");
    };
    routed({}, "app.global"); routed(panel.node("other"), "app.global");
    routed(panel.node("outer"), "app.outer"); routed(panel.node("inner"), "app.inner");
    routed(panel.node("inner-scope"), "app.inner");
    std::reverse(input.begin(), input.end());
    shortcuts = map(s, panel, input); routed(panel.node("inner"), "app.inner");
    // Maps own their declarations, and copying a map shares immutable routing.
    auto copied = shortcuts;
    input.front().bindings.clear(); routed(panel.node("inner"), "app.inner");
    check(copied.dispatch(s, panel.ui(panel.node("inner")), request(s)).value->invocation->record().command == "app.inner",
        "Copied map retains declarations");
    input = scopes(panel); input.back().bindings.front().chord = chord(Key::o);
    auto different = map(s, panel, input);
    check(different.dispatch(s, panel.ui(panel.node("inner")), request(s)).value->invocation->record().command == "app.outer",
        "Inner scope without the chord allows matching outer scope");
    auto event = request(s); event.event.data = KeyDown{Key::i, {}};
    auto miss = shortcuts.dispatch(s, panel.ui(), event);
    check(miss && !miss.value->consumed && !miss.value->invocation, "Modifiers match exactly");
    event.event.data = KeyDown{Key::i, {true, true}};
    check(!shortcuts.dispatch(s, panel.ui(), event).value->consumed, "Extra modifier does not match");
    for (const auto& data : {InputEvent{1us, KeyUp{Key::i, chord().modifiers}}, InputEvent{1us, TextInput{"i"}},
            InputEvent{1us, Activate{}}, InputEvent{1us, PointerCancel{}}}) {
        event.event = data;
        check(!shortcuts.dispatch(s, panel.ui(), event).value->consumed, "Other normalized events fall through");
    }
    event = request(s); event.consumed = true;
    auto blocked = shortcuts.dispatch(s, panel.ui(panel.node("inner")), event);
    check(blocked && blocked.value->consumed && !blocked.value->invocation, "Host IME/editing consumption suppresses shortcuts");
    event = request(s); std::get<KeyDown>(event.event.data).repeat = true;
    blocked = shortcuts.dispatch(s, panel.ui(panel.node("inner")), event);
    check(blocked && blocked.value->consumed && !blocked.value->invocation, "Suppressed inner repeat cannot activate global chord");
    input = scopes(panel); input.back().bindings.front().repeat = true;
    auto repeats = map(s, panel, input);
    check(repeats.dispatch(s, panel.ui(panel.node("inner")), event).value->invocation.has_value(), "Explicit repeat opt-in resolves");
    panel.styles[panel.node("inner").index].visibility = Visibility::hidden; panel.relayout();
    check(shortcuts.dispatch(s, panel.ui(panel.node("inner")), request(s)).value->invocation->record().command == "app.global",
        "Ineligible focus activates only global scope");
}

void registration() {
    auto r = registry(); auto s = snapshot(r); Panel panel; const auto good = scopes(panel);
    auto input = good; input.back().bindings.push_back(input.back().bindings.front());
    auto created = CommandShortcuts::create(s, *panel.tree, input);
    check(!created && has(created.diagnostics, "shortcut_conflict", "/scopes/2/bindings/1/chord") &&
        created.diagnostics.front().message.find("/scopes/2/bindings/0/chord") != std::string::npos, "Locate both conflicting chords");
    input = good; input.back().bindings.push_back({chord(Key::o), "app.inner", panel.action("inner")});
    check(bool(CommandShortcuts::create(s, *panel.tree, input)), "Different chords in one scope are valid");
    input = good; input.push_back(good.front());
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "duplicate_scope", "/scopes/3/root"), "Reject duplicate global scope");
    input = good; input.push_back(good.back());
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "duplicate_scope", "/scopes/3/root"), "Reject duplicate subtree scope");
    input = good; input.back().bindings.front().owner.reset();
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "missing_owner", "/scopes/2/bindings/0/owner"), "Subtree commands need live action ownership");
    input = good; input.back().bindings.front().owner = panel.action("outer");
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "outside_scope", "/scopes/2/bindings/0/owner/target"), "Reject owner outside scope");
    input = good; input.back().bindings.front().owner->action = "outer";
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "stale_binding", "/scopes/2/bindings/0/owner/action"), "Reject changed action name");
    input = good; input.back().bindings.front().command = "app.outer";
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "unmapped_action", "/scopes/2/bindings/0/owner/action"), "Require exact action-to-command agreement");
    input = good; input.front().bindings.front().command = "missing.command";
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "unknown_command", "/scopes/0/bindings/0/command"), "Reject unknown commands");
    input = good; input.front().bindings.front().command.assign(129, 'x');
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "out_of_range", "/scopes/0/bindings/0/command"), "Bound IDs before copying");
    input = good; input.front().bindings.front().chord.key = static_cast<Key>(255);
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "unknown_value", "/scopes/0/bindings/0/chord/key"), "Reject undeclared keys");
    Panel other; input = good; input.back().root = other.node("inner-scope");
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "stale_target", "/scopes/2/root"), "Reject foreign scope root");
    input = good; input.back().bindings.front().owner = other.action("inner");
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "stale_target", "/scopes/2/bindings/0/owner/target"), "Reject foreign owner");
    input.assign(max_shortcut_scopes + 1, {});
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "out_of_range", "/scopes"), "Bound scope declarations");
    input = good; input.front().bindings.resize(max_shortcut_bindings);
    check(has(CommandShortcuts::create(s, *panel.tree, input).diagnostics, "out_of_range", "/scopes"), "Bound total bindings across scopes");
    check(bool(CommandShortcuts::create(s, *panel.tree, {})), "Empty map is valid");
    check(s.lookup_action("inner").value->descriptor.id == "app.inner" &&
        has(s.lookup_action("unmapped").diagnostics, "unmapped_action", "/action"), "Inspect exact action mapping without eligibility callbacks");
}

void rejection_and_lifetime() {
    auto r = registry(); auto s = snapshot(r); Panel panel; auto shortcuts = map(s, panel, scopes(panel));
    auto event = request(s); event.generation = 999;
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "stale_generation", "/generation"), "Reject stale requests");
    event = request(s); event.sequence = 0;
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "invalid_sequence", "/sequence"), "Reject zero sequence");
    event = request(s); event.event.timestamp = -1us;
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "out_of_range", "/timestamp"), "Validate normalized event");
    event = request(s); event.arguments = {{"amount", 11.0}};
    check(has(shortcuts.dispatch(s, panel.ui(panel.node("inner")), event).diagnostics, "out_of_range", "/arguments/amount"), "Shared command argument bounds");
    event.arguments = {{"amount", true}};
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "argument_type", "/arguments/amount"), "No shortcut argument coercion");
    event.arguments = {{"unknown", 1.0}};
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "unknown_argument", "/arguments/unknown"), "Unknown arguments reject");
    event.arguments.clear(); for (std::size_t i = 0; i <= max_command_parameters; ++i) event.arguments[std::to_string(i)] = true;
    check(has(shortcuts.dispatch(s, panel.ui(), event).diagnostics, "out_of_range", "/arguments"), "Bound request fields");
    auto revoked = snapshot(r, 2, true, CommandMode::development, {});
    check(has(shortcuts.dispatch(revoked, panel.ui(), request(revoked)).diagnostics, "invalid_reference", "/arguments/item"), "Use current object permissions");
    auto disabled = snapshot(r, 2, false);
    check(has(shortcuts.dispatch(disabled, panel.ui(panel.node("inner")), request(disabled)).diagnostics, "disabled_command", "/command"),
        "Disabled inner command fails closed without activating outer/global");
    const auto lowered = disabled.apply_eligibility(document(), panel.context);
    check(bool(lowered), "Disabled command lowers into ordinary UI eligibility");
    Panel disabled_panel(*lowered.value);
    auto disabled_map = map(disabled, disabled_panel, scopes(disabled_panel));
    auto semantics = build_semantic_tree(disabled_panel.ui());
    check(bool(semantics), "Disabled semantic projection builds");
    const auto entry = std::find_if(semantics.value->nodes.begin(), semantics.value->nodes.end(),
        [](const auto& node) { return node.id == "inner"; });
    check(entry != semantics.value->nodes.end() && !entry->enabled && entry->actions.empty(), "Disabled shortcut and semantics agree");
    check(has(disabled_map.dispatch(disabled, disabled_panel.ui(disabled_panel.node("inner-scope")), request(disabled)).diagnostics,
        "disabled_target", "/target"), "Disabled owner rejects within active scope without outer fallback");
    auto disabled_conflict = scopes(disabled_panel);
    disabled_conflict.back().bindings.push_back(disabled_conflict.back().bindings.front());
    check(has(CommandShortcuts::create(disabled, *disabled_panel.tree, disabled_conflict).diagnostics,
        "shortcut_conflict", "/scopes/2/bindings/1/chord"), "Disabled bindings still participate in conflict checks");
    auto pending = shortcuts.dispatch(s, panel.ui(panel.node("inner")), request(s));
    check(pending && pending.value->invocation, "Resolve pending invocation");
    Host host; CommandExecutor executor; auto ui = panel.ui();
    check(!executor.execute(disabled, *pending.value->invocation, host, &ui) && host.calls == 0, "Generation change rejects before host entry");
    auto other_registry = registry(); auto replacement = snapshot(other_registry);
    check(has(shortcuts.dispatch(replacement, ui, request(replacement)).diagnostics, "stale_registry", "/registry_revision"), "Registry replacement requires rebuilding map");
    Panel other;
    check(has(shortcuts.dispatch(s, other.ui(), request(s)).diagnostics, "stale_target", "/scopes"), "Replacement tree cannot inherit scopes by author ID");
    auto other_ui = other.ui();
    check(!executor.execute(s, *pending.value->invocation, host, &other_ui) && host.calls == 0, "Pending owner cannot execute on replacement tree");
    panel.styles[panel.node("inner").index].visibility = Visibility::hidden; panel.relayout(); ui = panel.ui();
    check(!executor.execute(s, *pending.value->invocation, host, &ui) && host.calls == 0, "Owner is rechecked immediately before execution");
    check(has(shortcuts.dispatch(s, panel.ui(panel.node("inner-scope")), request(s)).diagnostics, "hidden_target", "/target"), "Hidden inner owner cannot fall through");
    panel.styles[panel.node("inner").index].visibility = Visibility::visible; panel.relayout(); ui = panel.ui();
    auto ran = executor.execute(s, *pending.value->invocation, host, &ui);
    check(ran && ran.value->outcome.status == CommandOutcomeStatus::success && host.calls == 1, "Earlier rejection did not consume sequence");
    check(!executor.execute(s, *pending.value->invocation, host, &ui) && host.calls == 1, "Invocation copy executes once");
    ui.focused = other.node("inner");
    check(has(shortcuts.dispatch(s, ui, request(s)).diagnostics, "stale_target", "/focused"), "Reject stale focus");
    check(!shortcuts.dispatch(s, {}, request(s)), "Reject missing UI snapshot");
    auto debug_scopes = scopes(panel); debug_scopes.back().bindings.front() = {chord(), "app.debug", panel.action("debug")};
    auto debug = map(s, panel, debug_scopes); auto production = snapshot(r, 3, true, CommandMode::production);
    check(has(debug.dispatch(production, panel.ui(panel.node("inner-scope")), request(production)).diagnostics, "unknown_command", "/command"),
        "Unavailable inner descriptor cannot fall through to global");
    check(!CommandShortcuts::create(production, *panel.tree, debug_scopes), "Unavailable descriptors reject map registration");
    // Expiring the original tree leaves only inert handles, never dangling UI pointers.
    panel.tree.reset();
    check(has(shortcuts.dispatch(s, other.ui(), request(s)).diagnostics, "stale_target", "/scopes"), "Expired map is safe to inspect with a later tree");
}

void input_parity() {
    auto r = registry(); auto s = snapshot(r); Panel panel;
    auto shortcuts = map(s, panel, scopes(panel));
    auto ui = panel.ui(panel.node("inner"));
    auto shortcut = shortcuts.dispatch(s, ui, request(s));
    check(shortcut && shortcut.value->invocation, "Shortcut resolves");
    const auto& reference = shortcut.value->invocation->record();
    const auto action = request_semantic_action(ui, panel.node("inner"), "activate");
    FocusDispatcher focus; const HitTestInput hit{panel.tree.get(), panel.styles, &panel.layout};
    check(bool(focus.focus(hit, panel.node("inner"))), "Fixture focus sets");
    const auto focused = focus.dispatch(hit, {2us, Activate{}});
    const auto box = std::find_if(panel.layout.boxes.begin(), panel.layout.boxes.end(), [&](const auto& b) { return b.node == panel.node("inner"); });
    const Point point{box->border_box.origin.x + 1, box->border_box.origin.y + 1};
    PointerDispatcher pointer;
    check(bool(pointer.dispatch(hit, {1us, PointerDown{{1}, point}})), "Fixture pointer presses");
    const auto clicked = pointer.dispatch(hit, {2us, PointerUp{{1}, point}});
    check(action && focused && clicked && focused.value->actions.size() == 1 && clicked.value->actions.size() == 1,
        "Ordinary input routes produce one action");
    Host host; CommandExecutor executor;
    check(bool(executor.execute(s, *shortcut.value->invocation, host, &ui)), "Execute shortcut after dispatch");
    std::uint64_t sequence = 1;
    for (const auto& [source, a] : std::vector<std::pair<CommandSource, ActionRequest>>{
            {CommandSource::pointer, clicked.value->actions.front()}, {CommandSource::gamepad, focused.value->actions.front()},
            {CommandSource::accessibility, *action.value}}) {
        auto invocation = s.resolve_action(ui, a, s.generation(), {}, source, ++sequence);
        check(invocation && invocation.value->record().command == reference.command &&
            invocation.value->record().arguments == reference.arguments && invocation.value->record().owner == reference.owner,
            "Shortcut and ordinary actions agree in command, arguments and owner");
        auto executed = executor.execute(s, *invocation.value, host, &ui);
        check(executed && std::get<double>(executed.value->outcome.values.at("amount")) == 2, "Equal validated host result");
    }
    ReplayRecording recording; recording.document = panel.tree->document(); recording.viewport = {240, 240};
    recording.context = panel.context;
    recording.styles = panel.styles;
    recording.steps = {ReplayFocus{AuthorIdTarget{"inner"}}, InputEvent{2us, Activate{}}};
    auto serialized = save_replay(recording); check(bool(serialized), "Ordinary UI replay encodes");
    auto decoded = load_replay(*serialized.value); check(bool(decoded), "Ordinary UI replay decodes");
    auto played = play_replay(*decoded.value, panel.text);
    check(played && played.value->actions.size() == 1 && played.value->actions.front().action == "inner", "Replay requests ordinary same action");
    const auto& observation = played.value->actions.front();
    check(observation.id.has_value(), "Replay action retains author identity");
    const ActionRequest replay_action{observation.binding, observation.action, panel.node(*observation.id)};
    auto replay = s.resolve_action(ui, replay_action, s.generation(), {}, CommandSource::replay, ++sequence);
    check(replay && replay.value->record().arguments == reference.arguments && bool(executor.execute(s, *replay.value, host, &ui)) && host.calls == 5,
        "Replay observation resolves against current owner with same execution/result boundary");
}
} // namespace

int main() {
    try { routing(); registration(); rejection_and_lifetime(); input_parity(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Shortcut scope, conflict, eligibility, lifetime and input parity checks passed\n";
}
