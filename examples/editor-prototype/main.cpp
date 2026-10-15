#include <tessera/commands/gestures.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/text_edit.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
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
struct FixtureRejection final : std::runtime_error {
    std::vector<Diagnostic> diagnostics;
    FixtureRejection(std::string message, std::vector<Diagnostic> details)
        : std::runtime_error(std::move(message)), diagnostics(std::move(details)) {}
};
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message = "Editor fixture rejected";
        for (const auto& diagnostic : result.diagnostics)
            message += "\n" + diagnostic.code + " at " + diagnostic.path + ": " + diagnostic.message;
        throw FixtureRejection(std::move(message), std::move(result.diagnostics));
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

struct EditorHost final : TransactionHost {
    InspectionSnapshot accepted;
    std::optional<std::string> selection;
    std::optional<InspectorField> field;
    std::vector<CommandSource> sources;
    std::optional<TextEditState> editing, saved_edit;
    std::vector<std::pair<std::string, std::string>> history;
    std::uint64_t edit_owner = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        const auto& id = std::get<CommandObjectId>(invocation.arguments.at("node")).value;
        const auto target = take(resolve_target(accepted, AuthorIdTarget{id}));
        auto candidate = inspect_string(accepted, target);
        if (selection != id || !editing) editing = take(make_text_edit_state(++edit_owner, candidate.value));
        selection = id; field = std::move(candidate); sources.push_back(invocation.source);
        return {};
    }
    CommandOutcome begin(const TransactionRecord&) override {
        check(editing && !saved_edit, "One host-owned label gesture"); saved_edit = editing; return {};
    }
    CommandOutcome execute_edit(const CommandInvocationRecord& invocation, const TransactionRecord&) override {
        check(selection == std::get<CommandObjectId>(invocation.arguments.at("node")).value, "Edit targets selected owner");
        auto candidate = take(edit_text(*editing, {editing->owner, editing->revision,
            TextEditSelection{0, editing->text.size()}}));
        candidate = take(edit_text(candidate, {candidate.owner, candidate.revision,
            TextEditInsert{std::get<std::string>(invocation.arguments.at("value"))}}));
        const auto anchor = std::get<double>(invocation.arguments.at("anchor"));
        const auto caret = std::get<double>(invocation.arguments.at("caret"));
        check(anchor == static_cast<std::size_t>(anchor) && caret == static_cast<std::size_t>(caret), "Integral selection offsets");
        candidate = take(edit_text(candidate, {candidate.owner, candidate.revision,
            TextEditSelection{static_cast<std::size_t>(anchor), static_cast<std::size_t>(caret)}}));
        editing = std::move(candidate); return {};
    }
    CommandOutcome commit(const TransactionRecord&) override {
        check(saved_edit && editing, "Commit requires open host edit");
        history.emplace_back(saved_edit->text, editing->text); saved_edit.reset(); return {};
    }
    CommandOutcome rollback(const TransactionRecord&, TransactionCancel) override {
        check(saved_edit && editing, "Rollback requires open host edit");
        // Restore bytes/selection while advancing revision; old requests stay stale.
        auto restored = *saved_edit; restored.revision = editing->revision + 1;
        editing = std::move(restored); saved_edit.reset(); return {};
    }
};
struct Presentation {
    BoundComponent component;
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    std::vector<ComponentInvocation> calls;
    std::vector<ComponentInvocationMount> mounts;
    std::vector<std::string> order;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput ui() const { return {tree.get(), styles, &layout}; }
};

// Declared synthetic raster input for placeholder scalars. This is a compact
// deterministic image fixture, not evidence about installed font rendering.
struct FixtureGlyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};
OffscreenRun capture(const Presentation& presentation, const ValidationContext& context) {
    ReplayRecording recording; recording.context = context; recording.viewport = {760, 320};
    recording.document = presentation.component.document; recording.styles = presentation.styles;
    recording.steps = {ReplayFocus{AuthorIdTarget{"row-subtitle"}},
        ReplaySemanticAction{AuthorIdTarget{"row-subtitle"}}, ReplayTick{10us}};
    PlaceholderTextShaper shaper; FixtureGlyphs glyphs; GlyphCache cache(glyphs);
    CpuFrameCapture frames(&cache);
    return take(run_offscreen({&recording, &shaper, frames.backend(), &frames}));
}

struct Editor {
    EditorHost host; // Outlives all callbacks and their explicit shutdown delivery.
    ReactiveRuntime runtime;
    ReactiveOwner scope = runtime.owner(runtime.root(), "editor");
    ComponentTreeCollection mounted{scope};
    FocusDispatcher focus;
    CommandTransactions transactions;
    CommandGestures gestures{transactions};
    PlaceholderTextShaper shaper;
    SourcedDocument preview;
    UiNode authored_label;
    std::string source;
    CommandRegistry registry;
    ValidationContext actions{{"select-headline", "select-subtitle", "edit-label"}};
    std::map<std::string, ComponentSetup> rows;
    std::optional<AsyncValue> asset;
    std::vector<AsyncAttempt> detached;
    std::uint64_t generation = 0, sequence = 0;
    std::vector<std::size_t> created;

    static const UiNode* label(const UiDocument& document, std::string_view id) {
        for (const auto& node : document.root.children) if (node.id == id) return &node;
        return nullptr;
    }
    static bool selectable(const UiDocument& document, std::string_view id) {
        const auto* node = label(document, id);
        return node && node->kind == NodeKind::text;
    }
    static std::string preview_kind(const UiDocument& document) {
        const auto* node = label(document, "headline");
        return !node ? "preview-removed" : node->kind == NodeKind::text ? "preview-text" : "preview-box";
    }
    void mount(std::span<const ComponentInvocation> calls, std::span<const ComponentInvocationMount> mounts,
               const std::vector<std::string>& order, bool preview_active) {
        const auto update = take(mounted.reconcile(runtime, calls, mounts));
        created = update.created;
        rows.clear();
        for (std::size_t i = 0; i < order.size(); ++i) rows.emplace(order[i], update.instances[i + 5]);
        if (preview_active) asset = take(update.instances[3].async("asset"));
        else asset.reset();
        check(runtime.flush().empty(), "Editor reactive evaluation failed");
    }

    static CommandRegistry commands() {
        CommandDescriptor select; select.id = "editor.select"; select.label = "Select preview node";
        CommandParameter node; node.name = "node"; node.type = CommandParameterType::object_id;
        select.parameters = {node};
        CommandDescriptor edit; edit.id = "editor.label"; edit.label = "Edit selected string";
        CommandParameter value; value.name = "value"; value.type = CommandParameterType::string;
        CommandParameter anchor; anchor.name = "anchor"; anchor.type = CommandParameterType::number;
        anchor.minimum = 0; anchor.maximum = double(max_text_edit_bytes);
        auto caret = anchor; caret.name = "caret";
        edit.parameters = {node, value, anchor, caret};
        return take(CommandRegistry::create({select, edit},
            {{"select-headline", select.id}, {"select-subtitle", select.id}, {"edit-label", edit.id}}));
    }
    Editor() : registry(commands()) {
        std::ifstream file(TESSERA_EDITOR_FIXTURE, std::ios::binary);
        check(file.good(), "Cannot open preview fixture");
        source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        preview = take(load_document_with_sources(source, TESSERA_EDITOR_FIXTURE));
        authored_label = *label(preview.document, "headline");
        check(take(load_document(take(save_document(preview.document)))) == preview.document,
            "Runtime loading must preserve editor document and node metadata");
    }

    Presentation present(std::vector<std::string> order = {"headline", "subtitle"}, bool enabled = true,
                         const SourcedDocument* candidate = nullptr, bool staging = false,
                         bool preview_active = true, std::optional<SourceSpan> changed_origin = {},
                         TextShaper* candidate_shaper = nullptr) {
        const auto& displayed = candidate ? *candidate : preview;
        std::erase_if(order, [&](const auto& id) { return !label(displayed.document, id); });
        std::set<std::string, std::less<>> targets;
        for (const auto& id : order) if (selectable(displayed.document, id)) targets.insert(id);
        const bool selected = preview_active && host.selection && targets.contains(*host.selection);
        const bool retained_preview = preview_active && preview_kind(displayed.document) == preview_kind(preview.document);
        auto& text_shaper = candidate_shaper ? *candidate_shaper : static_cast<TextShaper&>(shaper);
        auto commands = take(CommandSnapshot::publish(registry, generation + 1, CommandMode::production,
            {{"editor.select", {enabled && preview_active}}, {"editor.label", {enabled && selected}}}, targets));
        if (!staging) {
            // Edits are node-free author-ID transactions. Every publication
            // rechecks command eligibility before deriving the Inspector draft.
            const auto refresh = gestures.refresh(commands, host);
            check(bool(refresh) || !gestures.active(), "Invalidation must close an edit");
            if (!preview_active) { host.selection.reset(); host.field.reset(); host.editing.reset(); }
        }
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
        if (targets.contains("headline")) action(toolbar, "inspect-selection", "select-headline");
        auto hierarchy = component("hierarchy", box("hierarchy", {text("hierarchy-title", "Hierarchy"), box("rows")}));
        hierarchy.slots = {{"rows", "rows", NodeKinds::text}};
        auto central = component(preview_active ? preview_kind(displayed.document) : "empty-preview", box("preview"));
        if (preview_active) {
            central.document = displayed.document; central.sources = displayed.sources;
            central.async = {{"asset", PropertyType::string}};
            central.props = {{"status", PropertyType::string, Property{std::string("Idle")}}};
            central.properties = {{"status", "asset-status", "text"}};
        } else {
            StyleDeclarations empty; empty.width = Dimension::points(20); empty.height = Dimension::points(20);
            style(central, "preview", empty);
        }
        auto inspector = component("inspector", box("inspector", {text("inspector-title", "Inspector"),
            text("inspector-property", selected && host.field ? host.field->property + " (string)" : "Select a node"),
            text("inspector-value", selected && host.editing ? host.editing->text : ""),
            text("inspector-selection", selected && host.editing ? std::to_string(host.editing->anchor) + ":" +
                std::to_string(host.editing->caret) : ""),
            text("inspector-origin", selected && host.field ? "preview.json:" +
                std::to_string(changed_origin.value_or(host.field->span).begin) : "")}));
        inspector.document.root.children[2].properties["focusable"] = true;
        inspector.document.root.children[2].properties["disabled"] = false;
        inspector.actions = {{"edit", "edit-label"}};
        inspector.events = {{"edit", "inspector-value", "activate"}};
        std::vector<ComponentInvocation> calls{{std::move(root), {}, {}}, {std::move(toolbar), {}, {}},
            {std::move(hierarchy), {}, {}}, {std::move(central), {}, {}}, {std::move(inspector), {}, {}}};
        calls[0].invocation_slots = {{"toolbar", {1}}, {"hierarchy", {2}}, {"preview", {3}}, {"inspector", {4}}};
        std::vector<ComponentInvocationMount> mounts{{"editor"}, {"toolbar"}, {"hierarchy"}, {"preview"}, {"inspector"}};
        if (preview_active) mounts[3].async = {{"asset", [&](const AsyncAttempt& attempt) { detached.push_back(attempt); }}};
        for (const auto& id : order) {
            const bool editable = selectable(displayed.document, id);
            auto row = component(editable ? "hierarchy-row-text" : "hierarchy-row-box", text("row-" + id, id));
            row.local_state = {{"pinned", PropertyType::boolean, Property{false}}};
            if (editable) action(row, "row-" + id, "select-" + id);
            StyleDeclarations row_style; row_style.height = Dimension::points(32);
            style(row, "row-" + id, row_style);
            calls[2].invocation_slots["rows"].push_back(calls.size());
            calls.push_back({std::move(row), {}, {}}); mounts.push_back({id});
        }
        if (!staging) {
            mount(calls, mounts, order, preview_active);
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            StyleDeclarations row_style; row_style.height = Dimension::points(32);
            const auto retained = rows.find(order[i]);
            const bool reuse = retained != rows.end() && retained->second.instance().kind == calls[i + 5].component.kind;
            row_style.background = (reuse && take(retained->second.state<bool>("pinned")).read())
                ? Color{0.2f, 0.4f, 0.8f, 1} : Color{0.9f, 0.9f, 0.9f, 1};
            calls[i + 5].arguments.styles["row-" + order[i]] = row_style;
        }
        const auto observation = asset && (!staging || retained_preview) ? asset->read() : AsyncObservation{};
        std::string status = "Idle";
        if (observation.status == AsyncStatus::loading) status = "Loading";
        if (observation.status == AsyncStatus::failed) status = "Failed: " + observation.failure->message;
        if (observation.status == AsyncStatus::ready) status = std::get<std::string>(observation.value->value);
        if (preview_active) calls[3].arguments.props["status"] = Property{status};
        auto lowered = take(lower_component_tree(runtime, calls, 0, actions));
        lowered.document = take(commands.apply_eligibility(lowered.document, actions));
        auto tree = take(UiTree::create(lowered.document, actions));
        const StyleSheet sheet;
        auto styles = take(resolve_styles({tree.get(), &sheet, {}, lowered.overrides}));
        auto layout = take(compute_layout({tree.get(), styles, {760, 320}, &text_shaper}));
        auto candidate_focus = focus;
        const auto focused = take(candidate_focus.refresh({tree.get(), styles, &layout})).focused;
        auto inspection = take(capture_inspection({tree.get(), styles, &layout, lowered.sources ? &*lowered.sources : nullptr, focused}));
        std::vector<UiInvariant> invariants;
        for (const auto& id : {"inspect-selection", "hierarchy", "preview", "inspector"}) {
            invariants.push_back({std::string(id) + "-visible", VisibleInvariant{AuthorIdTarget{id}}});
            invariants.push_back({std::string(id) + "-inside", InsideViewportInvariant{AuthorIdTarget{id}}});
        }
        const auto report = take(evaluate_invariants({&inspection, invariants, Size{760, 320}}));
        if (!report.passed()) throw FixtureRejection("Editor panel invariant failed",
            {{"editor_invariant", Severity::error, "/root", "Candidate must preserve visible panels inside the viewport.", {}}});
        take(build_paint_list({tree.get(), styles, &layout, &text_shaper}));
        if (!staging) {
            ++generation; focus = std::move(candidate_focus);
            check(runtime.publish() == 0 && runtime.deliver_cleanups().empty(), "Editor publication failed");
            host.accepted = inspection;
        }
        return {std::move(lowered), std::move(commands), std::move(tree), std::move(styles), std::move(layout),
            std::move(inspection), std::move(calls), std::move(mounts), std::move(order)};
    }
    void operate(const Presentation& presentation, const ActionRequest& action_request, CommandSource source_kind) {
        const std::string target = action_request.action == "select-headline" ? "headline" : "subtitle";
        if (host.selection != target && gestures.active())
            take(gestures.cancel(*gestures.active(), TransactionCancel::owner_changed, host));
        const auto ui = presentation.ui();
        const auto invocation = take(presentation.commands.resolve_action(ui, action_request, presentation.commands.generation(),
            {{"node", CommandObjectId{target}}}, source_kind, ++sequence));
        check(take(transactions.execute(presentation.commands, invocation, host, &ui)).outcome.status == CommandOutcomeStatus::success,
            "Selection command failed");
    }
    Result<TextEditState> edit(const Presentation& presentation, TextEditRequest request, std::uint64_t gesture_id) {
        check(host.editing && host.selection && host.accepted.generation == presentation.inspection.generation,
            "Editing requires a current selected observation");
        const auto target = *presentation.tree->find("inspector-value");
        const auto ui = presentation.ui();
        auto action_request = request_semantic_action(ui, target, "activate");
        if (!action_request) return {std::nullopt, std::move(action_request.diagnostics)};
        auto candidate = edit_text(*host.editing, request);
        if (!candidate) return candidate;
        if (candidate.value->text == host.editing->text) {
            host.editing = *candidate.value; return candidate; // Caret changes create no history.
        }
        // The host edit targets a permitted author ID, not a transient display
        // handle. The Inspector action above gates UI eligibility; the ordinary
        // node-free command keeps its transaction through full-tree replacement.
        const auto invocation = take(presentation.commands.resolve({presentation.commands.generation(), "editor.label",
            {{"node", CommandObjectId{*host.selection}}, {"value", candidate.value->text},
                {"anchor", double(candidate.value->anchor)}, {"caret", double(candidate.value->caret)}}, CommandSource::keyboard, ++sequence}));
        const EditGesture gesture{gesture_id, {}, PointerButton::primary, {"Edit label", "label", {*host.selection}, {}}};
        check(take(gestures.edit(gesture, presentation.commands, invocation, host, &ui)).outcome.status == CommandOutcomeStatus::success,
            "Ordinary label transaction failed");
        return {*host.editing, {}};
    }
    Result<bool> edit_input(const Presentation& presentation, const InputEvent& event, std::uint64_t gesture_id) {
        if (auto diagnostics = validate(event); !diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
        if (!host.editing) return {false, {}};
        if (take(focus.refresh(presentation.hit())).focused != presentation.tree->find("inspector-value")) return {false, {}};
        std::optional<TextEditRequest> request;
        if (const auto* input = std::get_if<TextInput>(&event.data))
            request = TextEditRequest{host.editing->owner, host.editing->revision, TextEditInsert{input->text}};
        if (const auto* key = std::get_if<KeyDown>(&event.data)) {
            if (key->modifiers.control || key->modifiers.alt || key->modifiers.super) return {false, {}};
            std::optional<TextMove> move;
            switch (key->key) {
            case Key::left: move = TextMove::left; break;
            case Key::right: move = TextMove::right; break;
            case Key::home: move = TextMove::home; break;
            case Key::end: move = TextMove::end; break;
            case Key::backspace:
            case Key::delete_key:
                request = TextEditRequest{host.editing->owner, host.editing->revision,
                    TextEditErase{key->key == Key::backspace ? TextErase::backward : TextErase::forward}}; break;
            default: break;
            }
            if (move) request = TextEditRequest{host.editing->owner, host.editing->revision, TextEditMove{*move, key->modifiers.shift}};
        }
        if (!request) return {false, {}};
        auto result = edit(presentation, std::move(*request), gesture_id);
        if (!result) return {std::nullopt, std::move(result.diagnostics)};
        return {true, {}}; // The host suppresses shortcut/focus routing for consumed input.
    }
    // Only the selected property's string bytes change. Runtime serialization is
    // the escaping authority; no private JSON encoder or editor property schema.
    std::string edited_source() const {
        UiDocument literal; literal.root = text("literal", host.editing->text);
        const auto json = take(save_document(literal));
        const auto sourced = take(load_document_with_sources(json, "literal"));
        const auto span = sourced.sources.nodes[0].properties.at("text");
        auto changed = source;
        changed.replace(host.field->span.begin, host.field->span.end - host.field->span.begin,
            json.substr(span.begin, span.end - span.begin));
        return changed;
    }
    Result<Presentation> reload_label(const Presentation& current, std::string changed, std::uint64_t gesture_id) {
        auto candidate = load_document_with_sources(changed, TESSERA_EDITOR_FIXTURE);
        if (!candidate) return {std::nullopt, std::move(candidate.diagnostics)};
        // This consumer admits a compatible one-string edit only, not arbitrary
        // topology/type migration. Stage ordinary full-tree output before commit.
        auto expected = preview.document;
        auto tree = take(UiTree::create(expected));
        const auto selected = *tree->find(*host.selection);
        auto replacement = take(UiTree::create(candidate.value->document));
        const auto replacement_target = replacement->find(*host.selection);
        auto unsupported = [&]() -> Result<Presentation> {
            Diagnostic diagnostic{"unsupported_reload", Severity::error, "/root",
                "This fixture admits only its selected compatible string edit.", {}};
            diagnostic.source = DiagnosticSource{{TESSERA_EDITOR_FIXTURE, candidate.value->sources.nodes[0].span}, DiagnosticOrigin::node};
            return {std::nullopt, {std::move(diagnostic)}};
        };
        if (!replacement_target || replacement->get(*replacement_target)->kind != tree->get(selected)->kind)
            return unsupported();
        const auto& properties = replacement->get(*replacement_target)->properties;
        const auto changed_property = properties.find(host.field->property);
        if (changed_property == properties.end() || !std::holds_alternative<std::string>(changed_property->second)) return unsupported();
        const auto& value = changed_property->second;
        const auto pointer = candidate.value->sources.nodes[replacement_target->index].properties.at(host.field->property);
        // The fixture's preview children are its authored label targets.
        for (auto& node : expected.root.children) if (node.id == host.selection) node.properties[host.field->property] = value;
        if (expected != candidate.value->document || std::get<std::string>(value) != host.editing->text) return unsupported();
        auto staged = present({"headline", "subtitle"}, true, &*candidate.value, true, true, pointer);
        auto selected_field = inspect_string(staged.inspection,
            take(resolve_target(staged.inspection, AuthorIdTarget{*host.selection})));
        check(selected_field.span == pointer, "Reload joins new source bytes");
        const auto ui = current.ui();
        check(take(gestures.complete(gesture_id, current.commands, host, &ui)).status == CommandOutcomeStatus::success,
            "Label gesture commits once");
        source = std::move(changed); preview = std::move(*candidate.value);
        generation = staged.commands.generation(); host.field = std::move(selected_field);
        take(focus.refresh(staged.hit())); host.accepted = staged.inspection;
        check(runtime.publish() == 0, "Reload publication failed");
        return {std::move(staged), {}};
    }
    // Exact fixture compatibility, with no migration or new runtime transaction.
    // Presentation is prepared using retained weak state and fresh defaults;
    // only acceptance enters ordinary owner reconciliation.
    Result<Presentation> reload_structure(const Presentation& current, std::string changed,
                                         TextShaper* candidate_shaper = nullptr) {
        auto candidate = load_document_with_sources(changed, TESSERA_EDITOR_FIXTURE);
        if (!candidate) return {std::nullopt, std::move(candidate.diagnostics)};
        auto expected = preview.document;
        const auto* before = label(expected, "headline");
        const auto* after = label(candidate.value->document, "headline");
        if (before && !after) {
            std::erase_if(expected.root.children, [](const auto& node) { return node.id == "headline"; });
        } else if (before && after && before->kind == NodeKind::text && after->kind == NodeKind::box) {
            auto& node = expected.root.children[0];
            node.kind = NodeKind::box; node.properties.erase("text");
        } else if ((!before || before->kind == NodeKind::box) && after && *after == authored_label) {
            if (before) expected.root.children[0] = authored_label;
            else expected.root.children.insert(expected.root.children.begin(), authored_label);
        }
        const auto located = [&](std::vector<Diagnostic> diagnostics) -> Result<Presentation> {
            for (auto& diagnostic : diagnostics) if (!diagnostic.source)
                diagnostic.source = DiagnosticSource{{TESSERA_EDITOR_FIXTURE,
                    candidate.value->sources.nodes[0].span}, DiagnosticOrigin::node};
            return {std::nullopt, std::move(diagnostics)};
        };
        if (expected == preview.document || expected != candidate.value->document)
            return located({{"unsupported_reload", Severity::error, "/root",
                "Change only headline: remove it, replace Text with an empty Box, or restore its authored Text.", {}}});
        const bool clear_selection = host.selection && !selectable(candidate.value->document, *host.selection);
        std::optional<SourceSpan> selected_origin;
        if (host.selection && !clear_selection) {
            const auto candidate_tree = take(UiTree::create(candidate.value->document));
            const auto target = *candidate_tree->find(*host.selection);
            selected_origin = candidate.value->sources.nodes[target.index].properties.at(host.field->property);
        }
        std::optional<Presentation> staged;
        try {
            staged = present({"headline", "subtitle"}, true, &*candidate.value, true, true, selected_origin, candidate_shaper);
        } catch (const FixtureRejection& failure) {
            return located(failure.diagnostics);
        }
        // Cancel against the accepted snapshot before retiring an editing owner.
        if (clear_selection && gestures.active())
            take(gestures.cancel(*gestures.active(), TransactionCancel::owner_changed, host));
        mount(staged->calls, staged->mounts, staged->order, true);
        if (clear_selection) { host.selection.reset(); host.field.reset(); host.editing.reset(); }
        else if (host.selection) host.field = inspect_string(staged->inspection,
            take(resolve_target(staged->inspection, AuthorIdTarget{*host.selection})));
        source = std::move(changed); preview = std::move(*candidate.value);
        generation = staged->commands.generation();
        take(focus.refresh(staged->hit())); host.accepted = staged->inspection;
        check(runtime.publish() == 0 && runtime.deliver_cleanups().empty(), "Source replacement publication failed");
        check(generation == current.commands.generation() + 1, "Source replacement publishes one generation");
        return {std::move(*staged), {}};
    }
};

// Exercise both presentation failure points through the existing host interface.
struct RejectingShaper final : TextShaper {
    PlaceholderTextShaper fallback;
    bool reject_measure;
    explicit RejectingShaper(bool layout) : reject_measure(layout) {}
    Result<TextMetrics> measure(std::string_view value, const TextStyle& style, const TextConstraints& constraints) override {
        if (reject_measure) return {std::nullopt, {{"fixture_measure", Severity::error, "/text", "Synthetic layout rejection.", {}}}};
        return fallback.measure(value, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view, const TextStyle&, const TextConstraints&) override {
        return {std::nullopt, {{"fixture_shape", Severity::error, "/text", "Synthetic paint rejection.", {}}}};
    }
};

void run_source_replacement(bool type_change) {
    Editor editor;
    auto initial = editor.present();
    editor.operate(initial, take(request_semantic_action(initial.ui(), *initial.tree->find("row-headline"), "activate")), CommandSource::agent);
    const auto headline_owner = editor.rows.at("headline");
    const auto subtitle_owner = editor.rows.at("subtitle");
    take(headline_owner.state<bool>("pinned")).write(true);
    auto subtitle_pinned = take(subtitle_owner.state<bool>("pinned")); subtitle_pinned.write(true);
    std::size_t retained_cleanup = 0;
    editor.runtime.on_cleanup(subtitle_owner.instance().owner, "retained-fixture", [&] { ++retained_cleanup; });
    auto accepted = editor.present();
    // Type change starts on a row; removal starts in the Inspector. Both recover
    // through the ordinary copied FocusDispatcher before owner replacement.
    const auto focused = *accepted.tree->find(type_change ? "row-headline" : "inspector-value");
    take(editor.focus.focus(accepted.hit(), focused));
    accepted = editor.present();
    const auto old_asset = *editor.asset;
    const auto pending = take(editor.asset->begin("label-resource"));
    accepted = editor.present();
    const auto original_source = editor.source;
    const auto old_edit = *editor.host.editing;
    const auto before_generation = editor.generation;
    const auto before_snapshot = editor.host.accepted;
    const auto before_observation = editor.asset->read();
    const auto before_image = capture(accepted, editor.actions);
    auto replacement = editor.preview.document;
    if (type_change) {
        replacement.root.children[0].kind = NodeKind::box;
        replacement.root.children[0].properties.erase("text");
    } else replacement.root.children.erase(replacement.root.children.begin());
    const auto changed = take(save_document(replacement));
    const auto rejected = [&](Result<Presentation> result) {
        check(!result && !result.diagnostics.empty(), "Candidate must reject with diagnostics");
        for (const auto& diagnostic : result.diagnostics)
            check(diagnostic.source && diagnostic.source->location.file == TESSERA_EDITOR_FIXTURE,
                "Rejected source/presentation diagnostics must locate the candidate");
        check(editor.source == original_source && editor.preview.document == accepted.calls[3].component.document &&
            editor.generation == before_generation && editor.host.accepted == before_snapshot &&
            editor.host.editing == old_edit && editor.host.selection == "headline" &&
            editor.rows.at("headline").instance().lifetime == headline_owner.instance().lifetime &&
            take(headline_owner.state<bool>("pinned")).read() && subtitle_pinned.read() &&
            editor.asset->read() == before_observation && editor.detached.empty() &&
            take(editor.focus.refresh(accepted.hit())).focused == accepted.tree->find(type_change ? "row-headline" : "inspector-value"),
            "Rejected staging preserves source/generation, state, selection, focus and owner lifetimes");
        check(editor.runtime.deliver_cleanups().empty() && editor.detached.empty() && retained_cleanup == 0,
            "Rejected staging queues no owner cleanup");
    };
    rejected(editor.reload_structure(accepted, "{"));
    auto invalid = changed;
    const auto type = invalid.find("\"Box\"");
    invalid.replace(type, 5, "\"Invalid\"");
    rejected(editor.reload_structure(accepted, invalid));
    auto unsupported = replacement;
    unsupported.root.children.back().properties["text"] = std::string("Unrelated change");
    rejected(editor.reload_structure(accepted, take(save_document(unsupported))));
    RejectingShaper layout_failure(true), paint_failure(false);
    rejected(editor.reload_structure(accepted, changed, &layout_failure));
    rejected(editor.reload_structure(accepted, changed, &paint_failure));
    check(capture(accepted, editor.actions) == before_image, "Rejected candidates preserve exact deterministic observations and pixels");

    // An open draft is host work too: reject it unchanged, then cancel it only
    // when the source removal/type replacement actually accepts.
    take(editor.edit(accepted, {old_edit.owner, old_edit.revision, TextEditInsert{" draft"}}, 100));
    const auto draft = *editor.host.editing;
    const auto reject_draft = editor.reload_structure(accepted, changed, &paint_failure);
    check(!reject_draft && editor.host.editing == draft && editor.gestures.active() == 100 &&
        editor.host.saved_edit == old_edit && editor.host.history.empty() && editor.detached.empty(),
        "Rejected presentation leaves the draft gesture and accepted owner intact");
    auto replaced = take(editor.reload_structure(accepted, changed));
    check(editor.generation == before_generation + 1 && editor.source == changed &&
        editor.preview.document == replacement && editor.host.accepted == replaced.inspection &&
        !editor.host.selection && !editor.host.field && !editor.host.editing && !editor.host.saved_edit &&
        !editor.gestures.active() && editor.host.history.empty(), "Source replacement accepts once and retires selected editing work");
    check(editor.rows.at("subtitle").instance().lifetime == subtitle_owner.instance().lifetime && subtitle_pinned.read() &&
        replaced.styles[replaced.tree->find("row-subtitle")->index].background == Color{0.2f, 0.4f, 0.8f, 1},
        "Unaffected keyed row lifetime/state/style survive source replacement");
    check(editor.created == (type_change ? std::vector<std::size_t>{5, 3} : std::vector<std::size_t>{3}),
        "Only the changed preview owner and replaced hierarchy row recreate");
    bool expired = false;
    try { take(headline_owner.state<bool>("pinned")).read(); }
    catch (const ReactiveError&) { expired = true; }
    check(expired && retained_cleanup == 0, "Affected old state expires without disposing retained owners");
    check(take(editor.focus.refresh(replaced.hit())).focused == replaced.tree->find("row-subtitle") &&
        !request_semantic_action(replaced.ui(), *accepted.tree->find("row-headline"), "activate") &&
        !request_semantic_action(replaced.ui(), *accepted.tree->find("inspector-value"), "activate") &&
        !replaced.commands.resolve({replaced.commands.generation(), "editor.select", {{"node", CommandObjectId{"headline"}}},
            CommandSource::agent, ++editor.sequence}), "Focus recovers and old/current incompatible targets reject");
    check(type_change ? (replaced.tree->get(*replaced.tree->find("headline"))->kind == NodeKind::box &&
        !take(editor.rows.at("headline").state<bool>("pinned")).read() &&
        !request_semantic_action(replaced.ui(), *replaced.tree->find("row-headline"), "activate"))
        : (!replaced.tree->find("headline") && !replaced.tree->find("row-headline")), "Source topology reaches preview and hierarchy");
    check(editor.detached.size() == 1 && editor.detached[0].request() == pending.attempt.request() &&
        editor.detached[0].sequence() == pending.attempt.sequence() &&
        !old_asset.complete(pending.attempt, std::string("Late")) &&
        editor.asset->read().status == AsyncStatus::idle &&
        editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 1,
        "Changed preview owner detaches once, rejects old completion and starts fresh");
    const auto image = capture(replaced, editor.actions);
    check(image == capture(replaced, editor.actions) && image.frames[0].image &&
        image.frames[0].image != before_image.frames[0].image &&
        image.output.generations[0].boxes.size() == replaced.layout.boxes.size(),
        "Accepted source replacement reproduces replay/CPU output and changes pixels");
    for (std::size_t i = 0; i < replaced.layout.boxes.size(); ++i)
        check(image.output.generations[0].boxes[i].border_box == replaced.layout.boxes[i].border_box,
            "Source replacement replay geometry equals accepted geometry");

    const auto replaced_work = take(editor.asset->begin("label-resource"));
    const auto replaced_asset = *editor.asset;
    editor.operate(replaced, take(request_semantic_action(replaced.ui(), *replaced.tree->find("row-subtitle"), "activate")), CommandSource::agent);
    const auto subtitle_edit = *editor.host.editing;
    auto restored = take(editor.reload_structure(replaced, original_source));
    check(editor.rows.at("headline").instance().lifetime != headline_owner.instance().lifetime &&
        !take(editor.rows.at("headline").state<bool>("pinned")).read() && subtitle_pinned.read() &&
        editor.rows.at("subtitle").instance().lifetime == subtitle_owner.instance().lifetime &&
        editor.created == std::vector<std::size_t>{5, 3} && editor.detached.size() == 2 &&
        editor.detached[1].request() == replaced_work.attempt.request() &&
        editor.detached[1].sequence() == replaced_work.attempt.sequence() &&
        !replaced_asset.complete(replaced_work.attempt, std::string("Late replacement")),
        "Source restoration recreates affected lifetimes while retaining unrelated state");
    check(editor.host.selection == "subtitle" && editor.host.editing == subtitle_edit &&
        editor.host.field == inspect_string(restored.inspection, *restored.tree->find("subtitle")) &&
        editor.source.substr(editor.host.field->span.begin, editor.host.field->span.end - editor.host.field->span.begin) ==
            "\"Shared runtime preview\"" &&
        std::get<std::string>(restored.tree->get(*restored.tree->find("inspector-origin"))->properties.at("text")) ==
            "preview.json:" + std::to_string(editor.host.field->span.begin),
        "Compatible Inspector selection/editor survives and joins replacement source origins");
    editor.operate(restored, take(request_semantic_action(restored.ui(), *restored.tree->find("row-headline"), "activate")), CommandSource::agent);
    check(editor.host.editing->owner != old_edit.owner &&
        !editor.edit(restored, {old_edit.owner, old_edit.revision, TextEditInsert{"old owner"}}, 101),
        "Restored authored label rejects old editing owner through the ordinary edit boundary");
    const auto fresh = take(editor.asset->begin("label-resource"));
    check(fresh.attempt.sequence() == pending.attempt.sequence() &&
        !editor.asset->complete(pending.attempt, std::string("Foreign lifetime")) &&
        static_cast<bool>(editor.asset->complete(fresh.attempt, std::string("Fresh"))),
        "Fresh observation accepts its own attempt and rejects reused old request/sequence");
    auto final = editor.present();
    check(capture(final, editor.actions) == capture(final, editor.actions), "Restored source capture reproduces exactly");
    editor.scope.dispose(); editor.runtime.flush();
    check(editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 2 && retained_cleanup == 1,
        "Terminal restoration shutdown adds no detachment and closes retained owner once");
}

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

    // Drive the schema-selected string through the core edit transform and the
    // same command/gesture boundary used by other application edits.
    const auto editor_node = *ready.tree->find("inspector-value");
    take(editor.focus.focus(ready.hit(), editor_node));
    const auto accepted_source = editor.source;
    const auto selected_owner = editor.host.editing->owner;
    const auto before = *editor.host.editing;
    const auto baseline_capture = capture(ready, editor.actions);
    auto edit = [&](const Presentation& p, auto operation, std::uint64_t gesture = 1) {
        return take(editor.edit(p, {editor.host.editing->owner, editor.host.editing->revision, std::move(operation)}, gesture));
    };
    check(take(editor.edit_input(ready, {10us, KeyDown{Key::home}}, 1)) &&
        take(editor.edit_input(ready, {11us, KeyDown{Key::right, {true}}}, 1)), "Editing navigation consumes normalized keys");
    check(editor.host.editing->anchor == 0 && editor.host.editing->caret == 1 && editor.host.history.empty(),
        "Caret/selection operations create no edit history");
    check(take(editor.edit_input(ready, {12us, TextInput{"h"}}, 1)) &&
        take(editor.focus.refresh(ready.hit())).focused == ready.tree->find("inspector-value"),
        "Committed text consumes input without navigating or activating surrounding UI");
    auto draft = editor.present();
    check(editor.host.history.empty() && editor.source == accepted_source &&
        std::get<std::string>(draft.tree->get(*draft.tree->find("inspector-value"))->properties.at("text")) == "hello Tessera",
        "Draft updates Inspector while the accepted source stays intact");
    check(take(editor.edit_input(draft, {13us, KeyDown{Key::end}}, 1)) &&
        take(editor.edit_input(draft, {14us, TextInput{"日本😀\"\\"}}, 1)) &&
        take(editor.edit_input(draft, {15us, KeyDown{Key::backspace}}, 1)), "Committed Unicode and deletion use normalized input");
    edit(draft, TextEditMove{TextMove::left, true});
    edit(draft, TextEditInsert{"!"}); // Replace the quote selection.
    const auto final_value = editor.host.editing->text;
    check(final_value == "hello Tessera日本😀!", "Scalar insertion, deletion and selection replacement");
    check(!editor.edit(draft, {before.owner, before.revision, TextEditInsert{"stale"}}, 1), "Stale edit revision rejects");
    const auto draft_state = *editor.host.editing;
    check(!editor.edit(draft, {draft_state.owner, draft_state.revision, TextEditInsert{"\n"}}, 1) &&
        editor.host.editing == draft_state && editor.host.history.empty(), "Invalid text creates no command/history change");
    const auto generation_before_rejection = editor.host.accepted.generation;
    const auto command_generation_before_rejection = editor.generation;
    const auto rejected = editor.reload_label(draft, "{", 1);
    check(!rejected && !rejected.diagnostics.empty() && rejected.diagnostics[0].source &&
        editor.host.accepted.generation == generation_before_rejection && editor.source == accepted_source &&
        editor.generation == command_generation_before_rejection && editor.gestures.active() == 1,
        "Invalid source is located and leaves accepted generation and gesture usable");
    check(capture(draft, editor.actions) == capture(draft, editor.actions), "Rejected reload retains deterministic observations");
    auto incompatible = take(load_document(editor.source));
    incompatible.root.children.erase(incompatible.root.children.begin());
    check(!editor.reload_label(draft, take(save_document(incompatible)), 1) &&
        editor.host.accepted.generation == generation_before_rejection && editor.source == accepted_source,
        "Unsupported topology rejects atomically rather than retargeting the selected property");
    const auto row_lifetime = editor.rows.at("headline").instance().lifetime;
    const auto asset_observation = editor.asset->read();
    auto edited = take(editor.reload_label(draft, editor.edited_source(), 1));
    check(editor.host.history == std::vector<std::pair<std::string, std::string>>{{"Hello Tessera", final_value}} &&
        editor.host.field->value == final_value && editor.host.selection == "headline" &&
        editor.host.editing->owner == selected_owner && editor.rows.at("headline").instance().lifetime == row_lifetime &&
        pinned.read() && editor.asset->read() == asset_observation &&
        take(editor.focus.refresh(edited.hit())).focused == edited.tree->find("inspector-value"),
        "One gesture commits one history entry and compatible reload retains keys, state, focus, selection and async owner");
    check(std::get<std::string>(edited.tree->get(*edited.tree->find("headline"))->properties.at("text")) == final_value &&
        editor.generation == command_generation_before_rejection + 1 &&
        !request_semantic_action(edited.ui(), editor_node, "activate"), "Reload publishes edited property once and invalidates old targets");
    const auto authored = editor.host.field->span;
    check(editor.source.substr(authored.begin, authored.end - authored.begin) == "\"hello Tessera日本😀!\"",
        "Reload Inspector points at accepted source bytes");
    const auto edited_capture = capture(edited, editor.actions);
    const auto repeated_capture = capture(edited, editor.actions);
    check(edited_capture == repeated_capture && compare_replay(edited_capture.output, repeated_capture.output).empty(),
        "Fixed document/state/input/time/viewport/font and CPU images reproduce exactly");
    check(edited_capture.frames[0].image && baseline_capture.frames[0].image &&
        edited_capture.frames[0].image != baseline_capture.frames[0].image,
        "Accepted label source change reaches deterministic CPU pixels");
    check(edited_capture.output.actions.size() == 1 &&
        edited_capture.output.generations[0].boxes.size() == edited.layout.boxes.size(), "Replay operates and observes the accepted document");
    for (std::size_t i = 0; i < edited.layout.boxes.size(); ++i)
        check(edited_capture.output.generations[0].boxes[i].border_box == edited.layout.boxes[i].border_box,
            "Replay geometry equals accepted full-tree geometry");
    edit(edited, TextEditInsert{" temporary"}, 2);
    const auto cancellation_request = *editor.host.editing;
    const auto ui = edited.ui();
    const auto cancelled = take(editor.gestures.handle_input(edited.commands, {30us, KeyDown{Key::escape}}, editor.host, &ui));
    check(cancelled.consumed && cancelled.closure && editor.host.editing->text == final_value &&
        editor.host.history.size() == 1 && editor.source.substr(authored.begin, authored.end - authored.begin) == "\"hello Tessera日本😀!\"" &&
        !editor.edit(edited, {cancellation_request.owner, cancellation_request.revision, TextEditInsert{"late"}}, 2),
        "Escape restores accepted value, advances revision and adds no history");
    edit(edited, TextEditInsert{" selection draft"}, 3);
    editor.operate(edited, take(request_semantic_action(edited.ui(), *edited.tree->find("row-subtitle"), "activate")), CommandSource::agent);
    check(!editor.gestures.active() && !editor.host.saved_edit && editor.host.selection == "subtitle" &&
        editor.host.editing->text == "Shared runtime preview" && editor.host.history.size() == 1,
        "Selecting another owner cancels the draft before replacing the editing lifetime");

    auto disabled = editor.present({"headline", "subtitle"}, false);
    const auto disabled_row = *disabled.tree->find("row-headline");
    check(!request_semantic_action(disabled.ui(), disabled_row, "activate") &&
        !editor.focus.focus(disabled.hit(), disabled_row), "Disabled selection rejects semantic and focus requests");
    check(take(editor.focus.dispatch(disabled.hit(), {31us, Activate{}})).actions.empty(), "Disabled logical activation is inert");
    const auto bounds = disabled.inspection.elements[disabled_row.index].geometry->border_box;
    const Point disabled_point{bounds.origin.x + 4, bounds.origin.y + 4};
    take(pointer.dispatch(disabled.hit(), {32us, PointerDown{{1}, disabled_point}}));
    check(take(pointer.dispatch(disabled.hit(), {33us, PointerUp{{1}, disabled_point}})).actions.empty(), "Disabled pointer activation is inert");
    check(!disabled.commands.resolve_action(disabled.ui(), {"activate", "select-headline", disabled_row},
        disabled.commands.generation(), {{"node", CommandObjectId{"headline"}}}, CommandSource::agent, ++editor.sequence),
        "Direct command adaptation cannot bypass disabled eligibility");
    const auto disabled_edit_state = *editor.host.editing;
    check(!editor.edit(disabled, {disabled_edit_state.owner, disabled_edit_state.revision, TextEditInsert{"hidden"}}, 4) &&
        editor.host.editing == disabled_edit_state && editor.host.history.size() == 1,
        "Disabled Inspector cannot bypass ordinary action eligibility");

    const auto removed_work = take(editor.asset->begin("removed-preview"));
    const auto removed_asset = *editor.asset;
    const auto removed_editing = *editor.host.editing;
    auto removed = editor.present({"headline", "subtitle"}, true, nullptr, false, false);
    check(!editor.asset && !editor.host.selection && !editor.host.editing && editor.detached.size() == 1 &&
        editor.detached[0].sequence() == removed_work.attempt.sequence() &&
        !removed_asset.complete(removed_work.attempt, std::string("Late removed")) &&
        !request_semantic_action(removed.ui(), *disabled.tree->find("inspector-value"), "activate"),
        "Preview owner removal detaches once, clears selection/editor and rejects old targets and completion");
    auto recreated = editor.present();
    const auto fresh_work = take(editor.asset->begin("removed-preview"));
    check(fresh_work.attempt.sequence() == 1 && !editor.asset->complete(removed_work.attempt, std::string("Foreign lifetime")),
        "Recreated observation rejects old owner even with a reused request and attempt sequence");
    editor.operate(recreated, take(request_semantic_action(recreated.ui(), *recreated.tree->find("row-headline"), "activate")), CommandSource::agent);
    check(editor.host.editing->owner != removed_editing.owner &&
        !edit_text(*editor.host.editing, {removed_editing.owner, removed_editing.revision, TextEditInsert{"old owner"}}),
        "Recreated selected editor rejects old editing owner");
    check(static_cast<bool>(editor.asset->complete(fresh_work.attempt, std::string("Recreated ready"))) &&
        editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 1,
        "Fresh owner completes and removed cleanup stays once-only");

    const auto pending = take(editor.asset->begin("shutdown-resource"));
    const auto old_asset = *editor.asset;
    editor.scope.dispose(); editor.runtime.flush();
    check(editor.detached.size() == 1, "Disposal queues host work until explicit delivery");
    check(editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 2 &&
        editor.detached[1].sequence() == pending.attempt.sequence(), "Shutdown detaches the latest work once");
    check(editor.runtime.deliver_cleanups().empty() && editor.detached.size() == 2 &&
        !old_asset.complete(pending.attempt, std::string("Late")), "Late completion and duplicate cleanup reject");
    run_source_replacement(false);
    run_source_replacement(true);
    std::cout << "Editor panels: shared JSON preview + keyed hierarchy + schema string editor; "
        "selection, scalar editing, gesture history/cancellation, source label edit/removal/type replacement, async lifetimes and CPU replay checks passed.\n"
        "Core-only scripted fixture with placeholder text/synthetic glyphs; source edits and CPU images are in memory.\n";
}
} // namespace

int main() {
    try { run(); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
