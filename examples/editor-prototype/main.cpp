#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/ui/component_tree_mount.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace tessera;
using namespace std::chrono_literals;

namespace {
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message = "Editor fixture rejected";
        for (const auto& diagnostic : result.diagnostics)
            message += "\n" + diagnostic.code + " at " + diagnostic.path + ": " + diagnostic.message;
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
UiNode text(std::string id, std::string caption) {
    UiNode node; node.kind = NodeKind::text; node.id = std::move(id);
    node.properties["text"] = std::move(caption); return node;
}
UiNode box(std::string id, std::vector<UiNode> children = {}) {
    UiNode node; node.id = std::move(id); node.children = std::move(children); return node;
}
ComponentDescription component(std::string kind, UiNode root) {
    ComponentDescription result; result.kind = std::move(kind);
    result.document.root = std::move(root); return result;
}
void style(ComponentDescription& target, std::string id, StyleDeclarations declarations) {
    target.styles.push_back({id, declarations}); target.style_bindings.push_back({id, id});
}
void action(ComponentDescription& target, const std::string& id, const std::string& binding) {
    target.document.root.properties["focusable"] = true;
    // Author the absence value so command eligibility preserves source-map shape.
    target.document.root.properties["disabled"] = false;
    target.actions.push_back({"select", binding}); target.events.push_back({"select", id, "activate"});
}

struct InspectorField {
    std::string property, value, file;
    SourceSpan span;
    bool operator==(const InspectorField&) const = default;
};
// Join public schema, effective value and authored origin from one owned generation.
// The fixture admits one content string; it has no duplicate property/default table.
InspectorField inspect_string(const InspectionSnapshot& snapshot, NodeHandle target) {
    check(target.tree == snapshot.generation && target.index < snapshot.elements.size(), "Stale Inspector target");
    const auto& element = snapshot.elements[target.index];
    std::optional<InspectorField> result;
    for (const auto& property : element.properties) {
        const auto* descriptor = find_property_descriptor(property.name);
        if (!descriptor || descriptor->type != PropertyType::string ||
            descriptor->category != PropertyCategory::content || !property.authored || !property.value) continue;
        check(!result, "Fixture requires exactly one content string");
        check(element.source && element.source->mapped && element.source->file, "Inspector needs authored source");
        const auto origin = element.source->properties.find(property.name);
        check(origin != element.source->properties.end(), "Inspector property source unavailable");
        result = InspectorField{property.name, std::get<std::string>(*property.value),
            *element.source->file, origin->second};
    }
    check(result.has_value(), "No content string editor applies to target"); return *result;
}

struct EditorHost final : CommandHost {
    InspectionSnapshot accepted;
    std::optional<std::string> selection;
    std::optional<InspectorField> field;
    std::vector<CommandSource> sources;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        const auto& id = std::get<CommandObjectId>(invocation.arguments.at("node")).value;
        const auto target = take(resolve_target(accepted, AuthorIdTarget{id}));
        auto candidate = inspect_string(accepted, target);
        selection = id; field = std::move(candidate); sources.push_back(invocation.source);
        return {};
    }
};
struct Presentation {
    BoundComponent component;
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput ui() const { return {tree.get(), styles, &layout}; }
};

struct Editor {
    EditorHost host; // Outlives all callbacks and their explicit shutdown delivery.
    ReactiveRuntime runtime;
    ReactiveOwner scope = runtime.owner(runtime.root(), "editor");
    ComponentTreeCollection mounted{scope};
    FocusDispatcher focus;
    CommandExecutor executor;
    PlaceholderTextShaper shaper;
    SourcedDocument preview;
    std::string source;
    CommandRegistry registry;
    ValidationContext actions{{"select-headline", "select-subtitle"}};
    std::map<std::string, ComponentSetup> rows;
    std::optional<AsyncValue> asset;
    std::vector<AsyncAttempt> detached;
    std::uint64_t generation = 0, sequence = 0;

    static CommandRegistry commands() {
        CommandDescriptor select; select.id = "editor.select"; select.label = "Select preview node";
        CommandParameter node; node.name = "node"; node.type = CommandParameterType::object_id;
        select.parameters = {node};
        return take(CommandRegistry::create({select},
            {{"select-headline", select.id}, {"select-subtitle", select.id}}));
    }
    Editor() : registry(commands()) {
        std::ifstream file(TESSERA_EDITOR_FIXTURE, std::ios::binary);
        check(file.good(), "Cannot open preview fixture");
        source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        preview = take(load_document_with_sources(source, TESSERA_EDITOR_FIXTURE));
        check(take(load_document(take(save_document(preview.document)))) == preview.document,
            "Runtime loading must preserve editor document and node metadata");
    }

    Presentation present(std::vector<std::string> order = {"headline", "subtitle"}, bool enabled = true) {
        auto root = component("editor", box("editor", {box("toolbar-slot"), box("workspace", {
            box("hierarchy-slot"), box("preview-slot"), box("inspector-slot")})}));
        root.slots = {{"toolbar", "toolbar-slot"}, {"hierarchy", "hierarchy-slot"},
            {"preview", "preview-slot"}, {"inspector", "inspector-slot"}};
        StyleDeclarations workspace; workspace.direction = FlexDirection::row; workspace.gap = 8.0f;
        style(root, "workspace", workspace);
        for (const auto& id : {"hierarchy-slot", "preview-slot", "inspector-slot"}) {
            StyleDeclarations panel; panel.width = Dimension::points(240); panel.height = Dimension::points(240);
            panel.overflow = Overflow::clip; style(root, id, panel);
        }
        auto toolbar = component("toolbar", text("inspect-selection", "Inspect heading"));
        action(toolbar, "inspect-selection", "select-headline");
        auto hierarchy = component("hierarchy", box("hierarchy", {text("hierarchy-title", "Hierarchy"), box("rows")}));
        hierarchy.slots = {{"rows", "rows", NodeKinds::text}};
        auto central = component("preview", preview.document.root);
        central.document = preview.document; central.sources = preview.sources;
        central.async = {{"asset", PropertyType::string}};
        central.props = {{"status", PropertyType::string, Property{std::string("Idle")}}};
        central.properties = {{"status", "asset-status", "text"}};
        auto inspector = component("inspector", box("inspector", {text("inspector-title", "Inspector"),
            text("inspector-property", host.field ? host.field->property + " (string)" : "Select a node"),
            text("inspector-value", host.field ? host.field->value : ""),
            text("inspector-origin", host.field ? "preview.json:" + std::to_string(host.field->span.begin) : "")}));
        std::vector<ComponentInvocation> calls{{std::move(root), {}, {}}, {std::move(toolbar), {}, {}},
            {std::move(hierarchy), {}, {}}, {std::move(central), {}, {}}, {std::move(inspector), {}, {}}};
        calls[0].invocation_slots = {{"toolbar", {1}}, {"hierarchy", {2}}, {"preview", {3}}, {"inspector", {4}}};
        std::vector<ComponentInvocationMount> mounts{{"editor"}, {"toolbar"}, {"hierarchy"}, {"preview"}, {"inspector"}};
        mounts[3].async = {{"asset", [&](const AsyncAttempt& attempt) { detached.push_back(attempt); }}};
        for (const auto& id : order) {
            auto row = component("hierarchy-row", text("row-" + id, id));
            row.local_state = {{"pinned", PropertyType::boolean, Property{false}}};
            action(row, "row-" + id, "select-" + id);
            StyleDeclarations row_style; row_style.height = Dimension::points(32);
            style(row, "row-" + id, row_style);
            calls[2].invocation_slots["rows"].push_back(calls.size());
            calls.push_back({std::move(row), {}, {}}); mounts.push_back({id});
        }
        const auto update = take(mounted.reconcile(runtime, calls, mounts));
        rows.clear();
        for (std::size_t i = 0; i < order.size(); ++i) rows.emplace(order[i], update.instances[i + 5]);
        asset = take(update.instances[3].async("asset"));
        check(runtime.flush().empty(), "Editor reactive evaluation failed");
        for (std::size_t i = 0; i < order.size(); ++i) {
            StyleDeclarations row_style; row_style.height = Dimension::points(32);
            row_style.background = take(rows.at(order[i]).state<bool>("pinned")).read()
                ? Color{0.2f, 0.4f, 0.8f, 1} : Color{0.9f, 0.9f, 0.9f, 1};
            calls[i + 5].arguments.styles["row-" + order[i]] = row_style;
        }
        const auto observation = asset->read();
        std::string status = "Idle";
        if (observation.status == AsyncStatus::loading) status = "Loading";
        if (observation.status == AsyncStatus::failed) status = "Failed: " + observation.failure->message;
        if (observation.status == AsyncStatus::ready) status = std::get<std::string>(observation.value->value);
        calls[3].arguments.props["status"] = Property{status};
        auto lowered = take(lower_component_tree(runtime, calls, 0, actions));
        auto commands = take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
            {{"editor.select", {enabled}}}, {"headline", "subtitle"}));
        lowered.document = take(commands.apply_eligibility(lowered.document, actions));
        auto tree = take(UiTree::create(lowered.document, actions));
        const StyleSheet sheet;
        auto styles = take(resolve_styles({tree.get(), &sheet, {}, lowered.overrides}));
        auto layout = take(compute_layout({tree.get(), styles, {760, 320}, &shaper}));
        const auto focused = take(focus.refresh({tree.get(), styles, &layout})).focused;
        auto inspection = take(capture_inspection({tree.get(), styles, &layout, &*lowered.sources, focused}));
        std::vector<UiInvariant> invariants;
        for (const auto& id : {"inspect-selection", "hierarchy", "preview", "inspector"}) {
            invariants.push_back({std::string(id) + "-visible", VisibleInvariant{AuthorIdTarget{id}}});
            invariants.push_back({std::string(id) + "-inside", InsideViewportInvariant{AuthorIdTarget{id}}});
        }
        check(take(evaluate_invariants({&inspection, invariants, Size{760, 320}})).passed(), "Editor panel invariant failed");
        take(build_paint_list({tree.get(), styles, &layout, &shaper}));
        check(runtime.publish() == 0 && runtime.deliver_cleanups().empty(), "Editor publication failed");
        host.accepted = inspection;
        return {std::move(lowered), std::move(commands), std::move(tree), std::move(styles), std::move(layout), std::move(inspection)};
    }
    void operate(const Presentation& presentation, const ActionRequest& action_request, CommandSource source_kind) {
        const std::string target = action_request.action == "select-headline" ? "headline" : "subtitle";
        const auto ui = presentation.ui();
        const auto invocation = take(presentation.commands.resolve_action(ui, action_request, presentation.commands.generation(),
            {{"node", CommandObjectId{target}}}, source_kind, ++sequence));
        check(take(executor.execute(presentation.commands, invocation, host, &ui)).outcome.status == CommandOutcomeStatus::success,
            "Selection command failed");
    }
};

void run() {
    Editor editor;
    auto first = editor.present();
    check(!editor.host.selection && !editor.host.field, "Selection is host-owned and initially empty");
    check(*first.tree->get(*first.tree->find("preview")) == editor.preview.document.root,
        "Editor preview must compose the runtime-loaded subtree and preserve node metadata");
    const auto row = take(resolve_target(first.inspection, AuthorIdTarget{"row-headline"}));
    const auto rectangle = first.inspection.elements[row.index].geometry->border_box;
    const Point point{rectangle.origin.x + 4, rectangle.origin.y + 4};
    PointerDispatcher pointer;
    take(pointer.dispatch(first.hit(), {1us, PointerDown{{1}, point}}));
    const auto clicked = take(pointer.dispatch(first.hit(), {2us, PointerUp{{1}, point}}));
    check(clicked.actions.size() == 1, "Pointer selection must produce one ordinary action");
    editor.operate(first, clicked.actions[0], CommandSource::pointer);
    check(editor.host.selection == "headline" && editor.host.field->value == "Hello Tessera", "Pointer selects preview content");
    check(editor.host.field->file == TESSERA_EDITOR_FIXTURE &&
        editor.source.substr(editor.host.field->span.begin, editor.host.field->span.end - editor.host.field->span.begin) == "\"Hello Tessera\"",
        "Inspector property origin must address the authored source bytes");
    auto selected = editor.present();
    const auto value = take(resolve_target(selected.inspection, AuthorIdTarget{"inspector-value"}));
    check(std::get<std::string>(selected.tree->get(value)->properties.at("text")) == editor.host.field->value,
        "Inspector presents the selected property's observation");

    const auto subtitle = *selected.tree->find("row-subtitle");
    take(editor.focus.focus(selected.hit(), subtitle));
    const auto keyboard = take(editor.focus.dispatch(selected.hit(), {3us, Activate{}}));
    check(keyboard.actions.size() == 1, "Keyboard activation");
    editor.operate(selected, keyboard.actions[0], CommandSource::keyboard);
    check(editor.host.selection == "subtitle", "Keyboard selects the other preview node");
    const auto gamepad = take(editor.focus.dispatch(selected.hit(), {4us, Navigate{Direction::up}}));
    check(gamepad.focused == selected.tree->find("row-headline"), "Logical gamepad direction navigates hierarchy");
    const auto activated = take(editor.focus.dispatch(selected.hit(), {5us, Activate{}}));
    check(activated.actions.size() == 1, "Gamepad activation");
    editor.operate(selected, activated.actions[0], CommandSource::gamepad);
    const auto semantic = take(request_semantic_action(selected.ui(), *selected.tree->find("inspect-selection"), "activate"));
    editor.operate(selected, semantic, CommandSource::agent);
    check(editor.host.sources == std::vector<CommandSource>{CommandSource::pointer, CommandSource::keyboard,
        CommandSource::gamepad, CommandSource::agent}, "All selection sources use the shared command host");

    const auto lifetime = editor.rows.at("headline").instance().lifetime;
    auto pinned = take(editor.rows.at("headline").state<bool>("pinned")); pinned.write(true);
    const auto old_field = editor.host.field;
    const auto loading = take(editor.asset->begin("preview-resource"));
    auto reordered = editor.present({"subtitle", "headline"});
    check(editor.rows.at("headline").instance().lifetime == lifetime && pinned.read() && editor.host.field == old_field &&
        take(editor.focus.refresh(reordered.hit())).focused == reordered.tree->find("row-headline"),
        "Keyed reorder must retain local state, Inspector selection and focus");
    check(reordered.styles[reordered.tree->find("row-headline")->index].background == Color{0.2f, 0.4f, 0.8f, 1},
        "Retained row state must reach the full-tree presentation");
    check(editor.asset->read().status == AsyncStatus::loading && editor.asset->read().attempt == loading.attempt.sequence(),
        "Host-driven observation survives compatible presentation");
    check(!request_semantic_action(reordered.ui(), semantic.target, "activate"), "Previous-generation semantic targets reject");
    check(static_cast<bool>(editor.asset->fail(loading.attempt, {"offline", "Synthetic failure", true})), "Async failure");
    auto failed = editor.present();
    check(editor.asset->read().status == AsyncStatus::failed &&
        std::get<std::string>(failed.tree->get(*failed.tree->find("asset-status"))->properties.at("text")) == "Failed: Synthetic failure",
        "Failed observation projects through ordinary components");
    const auto retry = take(editor.asset->begin("preview-resource"));
    check(!editor.asset->complete(loading.attempt, std::string("Old")), "Superseded completion rejects");
    check(static_cast<bool>(editor.asset->complete(retry.attempt, std::string("Resource ready"))), "Async retry completes");
    auto ready = editor.present();
    const auto status = *ready.tree->find("asset-status");
    check(std::get<std::string>(ready.tree->get(status)->properties.at("text")) == "Resource ready", "Ready value appears in preview");

    auto disabled = editor.present({"headline", "subtitle"}, false);
    const auto disabled_row = *disabled.tree->find("row-headline");
    check(!request_semantic_action(disabled.ui(), disabled_row, "activate") &&
        !editor.focus.focus(disabled.hit(), disabled_row), "Disabled selection rejects semantic and focus requests");
    check(take(editor.focus.dispatch(disabled.hit(), {6us, Activate{}})).actions.empty(), "Disabled logical activation is inert");
    const auto bounds = disabled.inspection.elements[disabled_row.index].geometry->border_box;
    const Point disabled_point{bounds.origin.x + 4, bounds.origin.y + 4};
    take(pointer.dispatch(disabled.hit(), {7us, PointerDown{{1}, disabled_point}}));
    check(take(pointer.dispatch(disabled.hit(), {8us, PointerUp{{1}, disabled_point}})).actions.empty(), "Disabled pointer activation is inert");
    check(!disabled.commands.resolve_action(disabled.ui(), {"activate", "select-headline", disabled_row},
        disabled.commands.generation(), {{"node", CommandObjectId{"headline"}}}, CommandSource::agent, ++editor.sequence),
        "Direct command adaptation cannot bypass disabled eligibility");

    const auto pending = take(editor.asset->begin("shutdown-resource"));
    const auto old_asset = *editor.asset;
    editor.scope.dispose(); editor.runtime.flush();
    check(editor.detached.empty(), "Disposal queues host work until explicit delivery");
    check(editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 1 &&
        editor.detached[0].sequence() == pending.attempt.sequence(), "Shutdown detaches the latest work once");
    check(editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 1 &&
        !old_asset.complete(pending.attempt, std::string("Late")), "Late completion and duplicate cleanup reject");
    std::cout << "Editor panels: shared JSON preview + keyed hierarchy + schema Inspector; "
        "pointer/keyboard/gamepad/semantic selection, focus/state retention and host async checks passed.\n"
        "Core-only scripted fixture with placeholder text; Inspector displays source/value observations.\n";
}
} // namespace

int main() {
    try { run(); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
