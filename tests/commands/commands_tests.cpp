#include <tessera/commands/commands.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <tuple>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
CommandDescriptor descriptor() {
    CommandDescriptor d;
    d.id = "inventory.inspect";
    d.label = "Inspect selected item 選択";
    d.category = "Inventory";
    CommandParameter object;
    object.name = "item"; object.type = CommandParameterType::object_id;
    CommandParameter count;
    count.name = "count"; count.type = CommandParameterType::number;
    count.minimum = 1; count.maximum = 10; count.default_value = 1.0;
    CommandParameter verbose;
    verbose.name = "verbose"; verbose.default_value = false;
    CommandParameter mode;
    mode.name = "mode"; mode.type = CommandParameterType::enumeration;
    mode.choices = {"brief", "full"}; mode.default_value = std::string("brief");
    CommandParameter note;
    note.name = "note"; note.type = CommandParameterType::string;
    note.required = false; note.max_bytes = 8;
    d.parameters = {object, count, verbose, mode, note};
    return d;
}
CommandRegistry registry() {
    auto debug = descriptor(); debug.id = "developer.dump"; debug.availability = CommandAvailability::development;
    auto result = CommandRegistry::create({descriptor(), debug}, {{"inspect-item", "inventory.inspect"}, {"dump", "developer.dump"}});
    check(bool(result), "Fixture registry must register");
    return *result.value;
}
CommandSnapshot snapshot(const CommandRegistry& r, std::uint64_t generation = 1, bool enabled = true,
                         CommandMode mode = CommandMode::production) {
    auto result = CommandSnapshot::publish(r, generation, mode,
        {{"inventory.inspect", {enabled, true, enabled ? "" : "Select an item."}}, {"developer.dump", {true}}}, {"item-7"});
    check(bool(result), "Fixture observations must publish");
    return *result.value;
}
UiDocument authored() {
    UiDocument document;
    document.root.id = "tool-panel";
    for (const auto& [id, caption, action] : std::vector<std::tuple<std::string, std::string, std::string>>{
            {"inspect", "Inspect selected item", "inspect-item"}, {"dump", "Dump", "dump"}}) {
        UiNode button; button.id = id; button.events["activate"] = action; button.properties["focusable"] = true;
        UiNode label; label.kind = NodeKind::text; label.properties["text"] = caption;
        button.children.push_back(std::move(label));
        document.root.children.push_back(std::move(button));
    }
    return document;
}
struct Panel {
    ValidationContext context{{"inspect-item", "dump"}};
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    explicit Panel(const CommandSnapshot& s, UiDocument input = authored()) {
        const auto lowered = s.apply_eligibility(input, context);
        check(bool(lowered), "Eligibility lowering must validate authored input");
        document = *lowered.value;
        auto created = UiTree::create(document, context);
        check(bool(created), "Lowered tool panel must create"); tree = std::move(*created.value);
        styles.resize(tree->size());
        styles[1].height = Dimension::points(32);
        styles[3].height = Dimension::points(32);
        auto computed = compute_layout({tree.get(), styles, {240, 80}, &text});
        check(bool(computed), "Panel must lay out"); layout = std::move(*computed.value);
    }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
};
CommandRequest request(const CommandSnapshot& s, CommandArguments args = {{"item", CommandObjectId{"item-7"}}}) {
    return {s.generation(), "inventory.inspect", std::move(args), CommandSource::agent, 1};
}
void registration_and_discovery() {
    const auto r = registry();
    const auto s = snapshot(r);
    const auto info = s.discover();
    check(info.size() == 1 && info[0].descriptor == descriptor() && info[0].state.checked,
          "Production must expose owned metadata and eligibility without development descriptors");
    check(!s.lookup("developer.dump") && !s.lookup(descriptor().label), "Discovery must use availability and exact IDs");
    const auto dev = snapshot(r, 1, true, CommandMode::development);
    check(dev.discover().size() == 2 && dev.discover()[0].descriptor.id == "developer.dump", "Discovery must have deterministic ID order");
    auto copy = r;
    check(copy.revision() == r.revision() && registry().revision() != r.revision(), "Copies share revisions; replacements do not");
    auto d = descriptor(); d.id = "Inspect";
    check(has(CommandRegistry::create({d}).diagnostics, "invalid_identifier", "/commands/0/id"), "Malformed IDs must locate registration errors");
    check(has(CommandRegistry::create({descriptor(), descriptor()}).diagnostics, "duplicate_command", "/commands/1/id"), "Duplicate command IDs fail atomically");
    check(has(CommandRegistry::create({descriptor()}, {{"inspect", "missing.command"}}).diagnostics, "unknown_command", "/bindings/inspect"), "Unknown mappings fail");
    d = descriptor(); d.parameters.push_back(d.parameters[0]);
    check(has(CommandRegistry::create({d}).diagnostics, "duplicate_parameter", "/commands/0/parameters/5/name"), "Duplicate parameter names fail");
    d = descriptor(); d.parameters[1].default_value = 0.0;
    check(has(CommandRegistry::create({d}).diagnostics, "out_of_range", "/commands/0/parameters/1/default"), "Defaults must satisfy their schemas");
    d = descriptor(); d.parameters[1].minimum = 20;
    check(has(CommandRegistry::create({d}).diagnostics, "invalid_bounds", "/commands/0/parameters/1"), "Reversed bounds fail");
    d = descriptor(); d.parameters[3].choices.push_back("brief");
    check(has(CommandRegistry::create({d}).diagnostics, "duplicate_choice", "/commands/0/parameters/3/choices/2"), "Duplicate choices fail");
    d = descriptor(); d.label = std::string(1, char(0xff));
    check(has(CommandRegistry::create({d}).diagnostics, "invalid_utf8", "/commands/0/label"), "Malformed metadata fails");
    check(has(CommandRegistry::create(std::vector<CommandDescriptor>(max_commands + 1)).diagnostics, "out_of_range", "/commands"), "Registry bounds apply before descriptor processing");
}
void arguments_and_observations() {
    const auto r = registry(); const auto s = snapshot(r);
    const auto accepted = s.resolve(request(s));
    check(bool(accepted), "Typed request must resolve");
    check(accepted.value->record().arguments == CommandArguments{{"item", CommandObjectId{"item-7"}}, {"count", 1.0},
        {"verbose", false}, {"mode", std::string("brief")}}, "Validated defaults must materialize in the invocation");
    const auto reject = [&](CommandArguments args, const char* code, const char* path) {
        const auto result = s.resolve(request(s, std::move(args)));
        check(!result && has(result.diagnostics, code, path), "Invalid argument must fail at its field");
    };
    reject({}, "missing_argument", "/arguments/item");
    reject({{"item", std::string("item-7")}}, "argument_type", "/arguments/item");
    reject({{"item", CommandObjectId{"missing"}}}, "invalid_reference", "/arguments/item");
    auto args = request(s).arguments;
    args["count"] = std::numeric_limits<double>::infinity(); reject(args, "invalid_number", "/arguments/count");
    args["count"] = 11.0; reject(args, "out_of_range", "/arguments/count");
    args["count"] = true; reject(args, "argument_type", "/arguments/count");
    args = request(s).arguments; args["mode"] = std::string("other"); reject(args, "unknown_value", "/arguments/mode");
    args = request(s).arguments; args["note"] = std::string(9, 'x'); reject(args, "out_of_range", "/arguments/note");
    args["note"] = std::string(1, char(0xff)); reject(args, "invalid_utf8", "/arguments/note");
    args = request(s).arguments; args["private/field"] = false; reject(args, "unknown_argument", "/arguments/private~1field");
    auto req = request(s); req.sequence = 0;
    check(has(s.resolve(req).diagnostics, "invalid_sequence", "/sequence"), "Zero request sequences fail");
    req = request(s); req.source = static_cast<CommandSource>(255);
    check(has(s.resolve(req).diagnostics, "unknown_value", "/source"), "Invalid sources fail");
    check(has(CommandSnapshot::publish(r, 0, CommandMode::production, {}).diagnostics, "invalid_generation", "/generation"), "Zero generation fails");
    check(has(CommandSnapshot::publish(r, 1, CommandMode::production, {{"private.command", {true}}}).diagnostics,
        "unknown_command", "/states/private.command"), "Undeclared observations fail");
    const auto missing = CommandSnapshot::publish(r, 1, CommandMode::production, {});
    check(!missing.value->lookup("inventory.inspect").value->state.enabled && !missing.value->resolve(request(s)), "Absent queries fail closed");
    const auto failed = CommandSnapshot::publish(r, 1, CommandMode::production,
        {{"inventory.inspect", {true, true, "", "Selection query failed."}}});
    check(!failed.value->lookup("inventory.inspect").value->state.enabled && !failed.value->lookup("inventory.inspect").value->state.checked,
        "Query errors override enabled/checked and fail closed");
    const auto defaults = [&] {
        auto d = descriptor(); d.parameters[0].default_value = CommandObjectId{"item-7"};
        return *CommandRegistry::create({d}).value;
    }();
    const auto no_objects = CommandSnapshot::publish(defaults, 1, CommandMode::production, {{"inventory.inspect", {true}}});
    check(has(no_objects.value->resolve({1, "inventory.inspect", {}, CommandSource::agent, 1}).diagnostics,
        "invalid_reference", "/arguments/item"), "Object defaults must revalidate against published host IDs");
}
void input_and_replay_agreement() {
    const auto r = registry(); const auto s = snapshot(r); Panel panel(s);
    const auto ui = panel.semantic(); const auto hit = panel.hit();
    std::vector<std::pair<ActionRequest, CommandSource>> actions;
    PointerDispatcher pointer;
    check(bool(pointer.dispatch(hit, {1us, PointerDown{{1}, {10, 10}}})), "Pointer press must dispatch");
    const auto release = pointer.dispatch(hit, {2us, PointerUp{{1}, {10, 10}}});
    check(release && release.value->actions.size() == 1, "Pointer release must activate");
    actions.push_back({release.value->actions[0], CommandSource::pointer});
    for (const auto source : {CommandSource::keyboard, CommandSource::gamepad}) {
        FocusDispatcher focus;
        const auto next = source == CommandSource::keyboard ? InputEvent{1us, FocusNext{}} : InputEvent{1us, Navigate{Direction::down}};
        check(bool(focus.dispatch(hit, next)), "Normalized navigation must focus the panel command");
        const auto activate = focus.dispatch(hit, {2us, Activate{}});
        check(activate && activate.value->actions.size() == 1, "Normalized activation must request one action");
        actions.push_back({activate.value->actions[0], source});
    }
    const auto semantic = request_semantic_action(ui, *panel.tree->find("inspect"), "activate");
    check(bool(semantic), "Semantic activation must agree");
    actions.push_back({*semantic.value, CommandSource::accessibility});
    actions.push_back({*semantic.value, CommandSource::agent});
    const auto expected = s.resolve(request(s));
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& [action, source] = actions[i];
        const auto resolved = s.resolve_action(ui, action, 1, request(s).arguments, source, i + 1);
        check(resolved && resolved.value->record().arguments == expected.value->record().arguments &&
            resolved.value->record().command == expected.value->record().command && resolved.value->record().source == source,
            "Every ordinary input route must resolve the same typed command");
        check(s.recheck(*resolved.value, &ui).empty(), "Execution check must accept the current coherent owner");
    }
    ReplayRecording recording;
    recording.context = panel.context; recording.document = panel.document; recording.styles = panel.styles; recording.viewport = {240, 80};
    recording.steps = {InputEvent{1us, PointerDown{{1}, {10, 10}}}, InputEvent{2us, PointerUp{{1}, {10, 10}}},
        InputEvent{3us, FocusNext{}}, InputEvent{4us, Activate{}}, InputEvent{5us, Navigate{Direction::down}},
        InputEvent{6us, Activate{}}, ReplaySemanticAction{AuthorIdTarget{"inspect"}}};
    const auto saved = save_replay(recording);
    check(bool(saved), "Panel interaction recording must serialize");
    const auto loaded = load_replay(*saved.value);
    check(bool(loaded), "Panel recording must load");
    const auto first = play_replay(*loaded.value, panel.text);
    const auto second = play_replay(*loaded.value, panel.text);
    check(first && second && compare_replay(*first.value, *second.value).empty() && first.value->actions.size() == 4,
        "Serialized replay must reproduce pointer, keyboard, gamepad and semantic actions");
    for (const auto& action : first.value->actions) {
        check(action.id == "inspect" && action.action == "inspect-item" && action.generation == 0, "Replay must preserve the fixture's owner and binding");
        const auto result = s.resolve_action(ui, *semantic.value, 1, request(s).arguments, CommandSource::replay, action.step + 1);
        check(result && result.value->record().arguments == expected.value->record().arguments,
            "Replayed action observations must use the same declared command fixture");
    }
}
void eligibility_and_lifetime() {
    const auto r = registry(); const auto enabled = snapshot(r); const auto disabled = snapshot(r, 2, false);
    Panel panel(enabled); const auto ui = panel.semantic();
    const auto action = request_semantic_action(ui, *panel.tree->find("inspect"), "activate");
    const auto invocation = enabled.resolve_action(ui, *action.value, 1, request(enabled).arguments, CommandSource::pointer, 1);
    check(bool(invocation), "Enabled action must resolve before changes");
    check(has(disabled.recheck(*invocation.value),
        "stale_generation", "/generation"), "Eligibility changes must reject dispatched invocations");
    check(has(enabled.recheck(*invocation.value), "missing_snapshot", "/owner"), "Node invocations require a live snapshot at execution");
    const auto republished = snapshot(r); // Even accidental reuse of the host sequence cannot authorize old requests.
    check(has(republished.recheck(*invocation.value), "stale_generation", "/generation"), "Observation identity must reject reused generation sequences");
    const auto replacement = snapshot(registry());
    check(has(replacement.recheck(*invocation.value), "stale_registry", "/registry_revision"), "Registry replacement rejects old descriptors");
    Panel replaced(enabled); const auto replaced_ui = replaced.semantic();
    check(has(enabled.recheck(*invocation.value, &replaced_ui), "stale_target", "/target"), "Reused author IDs must not authorize replaced owners");
    check(!enabled.resolve_action(replaced_ui, *action.value, 1, request(enabled).arguments, CommandSource::agent, 2), "Foreign handles fail before resolution");
    Panel blocked(disabled);
    const auto blocked_ui = blocked.semantic();
    const auto semantics = build_semantic_tree(blocked_ui);
    check(semantics && !semantics.value->nodes[1].enabled && !semantics.value->nodes[1].focusable && semantics.value->nodes[1].actions.empty(),
        "Command disabled state must agree with semantic action/focus eligibility");
    StyleDeclarations disabled_style; disabled_style.background = Color{0.25f, 0.25f, 0.25f, 1};
    StyleStates disabled_state; disabled_state.disabled = true;
    const StyleSheet sheet{{{StyleSelector::of_id("inspect", disabled_state), disabled_style}}};
    const auto resolved_styles = resolve_styles({blocked.tree.get(), &sheet});
    check(resolved_styles && (*resolved_styles.value)[1].background == *disabled_style.background,
        "Command lowering must feed ordinary disabled pseudo-state styling");
    PointerDispatcher pointer; const auto hit = blocked.hit();
    check(bool(pointer.dispatch(hit, {1us, PointerDown{{1}, {10, 10}}})), "Disabled pointer press is safe");
    check(pointer.dispatch(hit, {2us, PointerUp{{1}, {10, 10}}}).value->actions.empty(), "Disabled pointer cannot request action");
    FocusDispatcher focus;
    check(!focus.dispatch(hit, {1us, FocusNext{}}).value->focused, "Unavailable/disabled commands cannot take keyboard or gamepad focus");
    check(!request_semantic_action(blocked_ui, *blocked.tree->find("inspect"), "activate"), "Disabled semantics cannot bypass dispatch");
    ReplayRecording blocked_recording;
    blocked_recording.context = blocked.context; blocked_recording.document = blocked.document;
    blocked_recording.styles = blocked.styles; blocked_recording.viewport = {240, 80};
    blocked_recording.steps = {InputEvent{1us, PointerDown{{1}, {10, 10}}}, InputEvent{2us, PointerUp{{1}, {10, 10}}},
        InputEvent{3us, FocusNext{}}, InputEvent{4us, Activate{}}, InputEvent{5us, Navigate{Direction::down}}, InputEvent{6us, Activate{}}};
    const auto blocked_output = play_replay(blocked_recording, blocked.text);
    check(blocked_output && blocked_output.value->actions.empty() && !blocked_output.value->semantics[0].nodes[1].enabled,
        "Replay must observe disabled state and suppress every ordinary input route");
    blocked_recording.steps.push_back(ReplaySemanticAction{AuthorIdTarget{"inspect"}});
    check(has(play_replay(blocked_recording, blocked.text).diagnostics, "disabled_target", "/steps/6/target"),
        "Replay semantic operation must fail through ordinary disabled eligibility");
    check(has(disabled.resolve(request(disabled)).diagnostics, "disabled_command", "/command"), "Node-free requests share command eligibility");
    check(has(enabled.resolve_action(ui, *action.value, 2, {}, CommandSource::agent, 2).diagnostics, "stale_generation", "/generation"), "Stale dispatch generation fails first");
    auto hidden_styles = panel.styles; hidden_styles[1].visibility = Visibility::hidden;
    const auto hidden_layout = compute_layout({panel.tree.get(), hidden_styles, {240, 80}, &panel.text});
    const SemanticInput hidden{panel.tree.get(), hidden_styles, &*hidden_layout.value};
    check(has(enabled.recheck(*invocation.value, &hidden), "hidden_target", "/target"), "Owner eligibility must recheck even with an unchanged command observation");
    auto authored_disabled = authored(); authored_disabled.root.properties["disabled"] = true;
    Panel ancestor(enabled, authored_disabled);
    check(!request_semantic_action(ancestor.semantic(), *ancestor.tree->find("inspect"), "activate"), "Command enabled cannot override an authored disabled ancestor");
    check(!authored().root.children[0].properties.contains("disabled"), "Lowering must preserve authored inputs");
    const auto restored = enabled.apply_eligibility(authored(), panel.context);
    check(restored && !restored.value->root.children[0].properties.contains("disabled"), "Fresh authored lowering restores enabled state");
    auto legacy = authored(); legacy.root.children[0].events["activate"] = "legacy";
    auto context = panel.context; context.actions.insert("legacy");
    const auto lowered_legacy = disabled.apply_eligibility(legacy, context);
    check(lowered_legacy && !lowered_legacy.value->root.children[0].properties.contains("disabled"), "Unmapped legacy actions retain their input contract");
    const auto unmapped_registry = CommandRegistry::create({descriptor()});
    const auto unmapped = CommandSnapshot::publish(*unmapped_registry.value, 1, CommandMode::production,
        {{"inventory.inspect", {true}}}, {"item-7"});
    check(has(unmapped.value->resolve_action(ui, *action.value, 1, request(enabled).arguments, CommandSource::agent, 2).diagnostics,
        "unmapped_action", "/action"), "Exact-looking action names require an explicit mapping");
    const auto expired_owner = [&] {
        Panel temporary(enabled); const auto temporary_ui = temporary.semantic();
        const auto temporary_action = request_semantic_action(temporary_ui, *temporary.tree->find("inspect"), "activate");
        return *enabled.resolve_action(temporary_ui, *temporary_action.value, 1, request(enabled).arguments, CommandSource::agent, 4).value;
    }();
    check(has(enabled.recheck(expired_owner, &ui), "stale_target", "/target"), "Expired owners must fail without dereferencing their destroyed tree");
    const auto owned = [&] { const auto temporary = snapshot(registry()); return *temporary.resolve(request(temporary)).value; }();
    check(owned.record().command == "inventory.inspect" && owned.record().arguments == invocation.value->record().arguments,
        "Invocation records must own values after registry and observation owners are destroyed");
}
} // namespace
int main() {
    try {
        registration_and_discovery(); arguments_and_observations(); input_and_replay_agreement(); eligibility_and_lifetime();
        std::cout << "Command registration, arguments, input/replay parity, eligibility and lifetime checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
