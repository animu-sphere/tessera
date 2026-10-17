#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <algorithm>
#include <charconv>
#include <functional>
#include <tuple>
#include <limits>
#include <stdexcept>

using namespace tessera;
namespace ui = tessera::authoring;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T take(Result<T> result) {
    if (!result) {
        const auto& error = result.diagnostics.front();
        throw std::runtime_error(error.code + " at " + error.path + ": " + error.message);
    }
    return std::move(*result.value);
}
const ValidationContext actions{{"settings.toggle", "settings.edit", "settings.apply",
    "settings.section.account", "settings.section.feature", "settings.section.about", "settings.appearance"}};
constexpr Size viewport{480, 240};
// Opaque fixture expression identity; the runtime opens no source file.
const SourceLocation form_source{"fixture://settings-form", {0, 20}};

// Application-owned styles use the ordinary cascade, not a widget style engine.
constexpr Color apply_ink{0.15f, 0.3f, 0.55f, 1};
constexpr Color hover_border{0.2f, 0.5f, 0.8f, 1};
constexpr Color focus_border{0.7f, 0.35f, 0.1f, 1};
constexpr Color active_border{0.15f, 0.25f, 0.45f, 1};
constexpr Color disabled_border{0.6f, 0.6f, 0.6f, 1};
constexpr Color disabled_ink{0.45f, 0.45f, 0.45f, 1};
struct SettingsAppearance {
    Theme theme;
    StyleSheet sheet;
    bool operator==(const SettingsAppearance&) const = default;
};
SettingsAppearance appearance(bool compact, bool feedback = false) {
    const auto token = [](std::string property, std::string name) -> ThemeBinding {
        return {std::move(property), {std::move(name), {}}};
    };
    SettingsAppearance result;
    result.theme.variables = {
        {"body-size", ThemeValue{compact ? 14.f : 16.f}},
        {"spacing", ThemeValue{compact ? 8.f : 12.f}},
        {"control-padding", ThemeValue{compact ? Edges{2, 2, 2, 2} : Edges{4, 4, 4, 4}}},
        {"ink", ThemeValue{compact ? Color{0.1f, 0.15f, 0.2f, 1} : Color{0.2f, 0.2f, 0.25f, 1}}},
        {"surface", ThemeValue{compact ? Color{0.85f, 0.9f, 0.95f, 1} : Color{0.95f, 0.95f, 0.98f, 1}}}};
    result.theme.defaults.rules = {
        {StyleSelector::of_id("settings"), {}, {token("text/size", "body-size"), token("color", "ink"), token("gap", "spacing")}},
        {StyleSelector::of_class("control"), {.corner_radius = 1.f},
            {token("padding", "control-padding"), token("background", "surface")}}};
    result.sheet.rules = {
        {StyleSelector::of_class("control"), {.border = Edges{1, 1, 1, 1},
            .border_color = Color{0.35f, 0.4f, 0.5f, 1}, .corner_radius = 4.f}},
        {StyleSelector::of_class("section"), {.text = {.weight = std::uint16_t{600}}}},
        {StyleSelector::of_class("heading"), {.text = {.size = 20.f}}},
        {StyleSelector::of_class("status"), {.color = Color{0.25f, 0.3f, 0.4f, 1}}}};
    if (feedback) {
        result.sheet.rules.push_back({StyleSelector::of_class("control", {.hover = true}), {.border_color = hover_border}});
        result.sheet.rules.push_back({StyleSelector::of_class("control", {.focus = true}), {.border_color = focus_border}});
        result.sheet.rules.push_back({StyleSelector::of_class("control", {.active = true}), {.border_color = active_border}});
        result.sheet.rules.push_back({StyleSelector::of_class("control", {.disabled = true}),
            {.border_color = disabled_border, .color = disabled_ink}});
    }
    return result;
}

// Application authors use typed C++ props/defaults and ordinary ordered children.
struct SettingsRowProps {
    std::string label;
    std::string id;
    float gap = 12;
};
ui::Element SettingsRow(SettingsRowProps props, std::vector<ui::Element> children) {
    return ui::Row({ui::Text(std::move(props.label)), ui::Column(std::move(children))},
        {.id = std::move(props.id), .style = {.gap = props.gap}, .source = form_source});
}
ui::Element Settings(ui::Value<bool> checked, ui::Value<std::string> summary, ui::Value<bool> disabled,
    std::optional<ui::Element> username = {}, std::vector<ui::Element> footer = {}) {
    std::vector<ui::Element> rows{
        ui::Text("Settings", {.id = "heading"}),
        SettingsRow({"Feature", "feature-row"}, {
            ui::Checkbox("Enable feature", checked, "settings.toggle", {.node = {
                .id = "toggle", .style = {.padding = Edges{6, 8, 6, 8},
                    .background = Color{0.2f, 0.35f, 0.6f, 1}}, .source = form_source}, .disabled = disabled})}),
        SettingsRow({"Status", "status-row"}, {
            ui::Text(summary, {.id = "summary", .source = form_source})})};
    if (username) rows.insert(rows.begin() + 2, SettingsRow({"Account", "username-row"}, {*username}));
    for (auto& element : footer) rows.push_back(std::move(element));
    return ui::Column(std::move(rows),
        {.id = "settings", .style = {.padding = Edges{12, 12, 12, 12}, .gap = 12.f,
            .background = Color{0.94f, 0.94f, 0.96f, 1}}, .source = form_source});
}

struct SettingsHost final : CommandHost {
    ui::Session& view;
    Signal<bool> enabled;
    bool feature = false; // Application data belongs to the host.
    unsigned executions = 0;
    SettingsHost(ui::Session& session, Signal<bool> observation) : view(session), enabled(observation) {}
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        require(invocation.command == "settings.toggle", "Unexpected command");
        feature = !feature;
        ++executions;
        view.update([&] { enabled.write(feature); });
        return {};
    }
};
struct Snapshot {
    ui::Presentation authored;
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
};
Result<Snapshot> prepare_candidate(ui::Session& view, const ui::Element& form, CommandSnapshot commands, TextShaper& text,
    Size logical_viewport = viewport, const std::map<std::string, Point>& requested_offsets = {},
    const SettingsAppearance* appearance = nullptr) {
    auto lowered = view.lower(form, actions);
    if (!lowered) return {std::nullopt, std::move(lowered.diagnostics)};
    auto authored = std::move(*lowered.value);
    auto fail = [&](std::vector<Diagnostic> errors) -> Result<Snapshot> {
        // Layout/paint address preorder nodes; the source adapter addresses
        // document pointers. Translate that host boundary before locating.
        for (auto& error : errors) if (error.path.starts_with("/nodes/")) {
            const auto end = error.path.find('/', 7);
            const auto count = (end == std::string::npos ? error.path.size() : end) - 7;
            std::size_t index = 0;
            const auto parsed = std::from_chars(error.path.data() + 7, error.path.data() + 7 + count, index);
            if (parsed.ec == std::errc{} && parsed.ptr == error.path.data() + 7 + count && index < authored.sources.nodes.size())
                error.path = authored.sources.nodes[index].pointer + (end == std::string::npos ? "" : error.path.substr(end));
        }
        const auto mapping = locate_diagnostics(authored.document, authored.sources, errors);
        if (!mapping.empty()) return {std::nullopt, mapping};
        return {std::nullopt, std::move(errors)};
    };
    auto eligible = commands.apply_eligibility(authored.document, actions);
    if (!eligible) return fail(std::move(eligible.diagnostics));
    auto built = UiTree::create(*eligible.value, actions);
    if (!built) return fail(std::move(built.diagnostics));
    auto tree = std::move(*built.value);
    const StyleSheet sheet;
    auto styles = resolve_styles({tree.get(), appearance ? &appearance->sheet : &sheet, {}, authored.styles,
        appearance ? &appearance->theme : nullptr});
    if (!styles) {
        auto rejected = fail(std::move(styles.diagnostics));
        // Host-authored theme/sheet paths have no document-node address.
        for (auto& error : rejected.diagnostics)
            if (!error.source && (error.path.starts_with("/theme/") || error.path.starts_with("/sheet/")))
                error.source = DiagnosticSource{form_source, DiagnosticOrigin::argument};
        return rejected;
    }
    std::vector<Point> offsets(tree->size());
    for (const auto& [id, offset] : requested_offsets)
        if (const auto owner = tree->find(id)) offsets[owner->index] = offset;
    auto layout = compute_layout({tree.get(), *styles.value, logical_viewport, &text, offsets});
    if (!layout) return fail(std::move(layout.diagnostics));
    auto inspection = capture_inspection({tree.get(), *styles.value, &*layout.value, &authored.sources});
    if (!inspection) return fail(std::move(inspection.diagnostics));
    auto paint = build_paint_list({tree.get(), *styles.value, &*layout.value, &text});
    if (!paint) return fail(std::move(paint.diagnostics));
    return {Snapshot{std::move(authored), std::move(commands), std::move(tree), std::move(*styles.value),
        std::move(*layout.value), std::move(*inspection.value), std::move(*paint.value)}, {}};
}
Snapshot prepare(ui::Session& view, const ui::Element& form, CommandSnapshot commands, TextShaper& text) {
    return take(prepare_candidate(view, form, std::move(commands), text));
}

// A feedback-only update retains tree identity so a press survives until release.
// Full document replacement instead uses ordinary dispatcher refresh (clearing pointers).
struct FeedbackView {
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
};
Result<FeedbackView> prepare_feedback(const Snapshot& snapshot, const SettingsAppearance& appearance,
    const InteractionState& state, TextShaper& text, Size viewport) {
    auto fail = [&](std::vector<Diagnostic> errors) -> Result<FeedbackView> {
        for (auto& error : errors) {
            if (error.path.starts_with("/nodes/")) {
                const auto end = error.path.find('/', 7);
                const auto count = (end == std::string::npos ? error.path.size() : end) - 7;
                std::size_t index = 0;
                const auto parsed = std::from_chars(error.path.data() + 7, error.path.data() + 7 + count, index);
                if (parsed.ec == std::errc{} && parsed.ptr == error.path.data() + 7 + count && index < snapshot.authored.sources.nodes.size())
                    error.path = snapshot.authored.sources.nodes[index].pointer + (end == std::string::npos ? "" : error.path.substr(end));
            }
        }
        const auto mapping = locate_diagnostics(snapshot.authored.document, snapshot.authored.sources, errors);
        if (!mapping.empty()) return {std::nullopt, mapping};
        return {std::nullopt, std::move(errors)};
    };
    auto styles = resolve_styles({snapshot.tree.get(), &appearance.sheet, state, snapshot.authored.styles, &appearance.theme});
    if (!styles) return fail(std::move(styles.diagnostics));
    std::vector<Point> offsets(snapshot.tree->size());
    for (const auto& box : snapshot.layout.boxes) if (box.scroll) offsets[box.node.index] = box.scroll->offset;
    auto layout = compute_layout({snapshot.tree.get(), *styles.value, viewport, &text, offsets});
    if (!layout) return fail(std::move(layout.diagnostics));
    auto inspection = capture_inspection({snapshot.tree.get(), *styles.value, &*layout.value, &snapshot.authored.sources});
    if (!inspection) return fail(std::move(inspection.diagnostics));
    auto paint = build_paint_list({snapshot.tree.get(), *styles.value, &*layout.value, &text});
    if (!paint) return fail(std::move(paint.diagnostics));
    return {FeedbackView{std::move(*styles.value), std::move(*layout.value), std::move(*inspection.value), std::move(*paint.value)}, {}};
}
void accept_feedback(Snapshot& snapshot, FeedbackView candidate) {
    snapshot.styles = std::move(candidate.styles); snapshot.layout = std::move(candidate.layout);
    snapshot.inspection = std::move(candidate.inspection); snapshot.paint = std::move(candidate.paint);
}
InteractionState interaction(const PointerDispatchResult& pointers, const FocusDispatchResult& focus) {
    InteractionState state;
    for (const auto& pointer : pointers.pointers) {
        if (pointer.hovered) state.hovered.push_back(*pointer.hovered);
        if (pointer.active) state.active.push_back(*pointer.active);
    }
    state.focused = focus.focused;
    return state;
}
std::string summary(const Snapshot& snapshot) {
    const auto node = *snapshot.tree->find("summary");
    return std::get<std::string>(snapshot.tree->get(node)->properties.at("text"));
}
void verify(const Snapshot& snapshot, bool enabled) {
    const auto expected = enabled ? "Feature enabled" : "Feature disabled";
    require(summary(snapshot) == expected, "State and derived presentation disagree");
    bool found = false;
    for (const auto& semantic : snapshot.inspection.semantics.nodes)
        if (semantic.id == "summary") { found = true; require(semantic.name == expected, "Semantic summary differs"); }
    require(found, "Derived summary missing from inspection");
    const auto control = *snapshot.tree->find("toggle");
    const auto& children = snapshot.tree->get(control)->children;
    const auto marker = enabled ? "[x]" : "[ ]";
    require(std::get<std::string>(children[0].properties.at("text")) == marker &&
        std::get<std::string>(children[1].properties.at("text")) == "Enable feature",
        "Controlled checked value or label differs from host state");
    found = false;
    for (const auto& semantic : snapshot.inspection.semantics.nodes) if (semantic.id == "toggle") {
        found = true;
        require(semantic.role == SemanticRole::button && semantic.name == std::string(marker) + " Enable feature" &&
            semantic.enabled && semantic.focusable && semantic.actions.size() == 1,
            "Checkbox composition must expose checked text and ordinary activation eligibility");
    }
    require(found, "Checkbox missing from inspection");
    const auto& marker_source = snapshot.inspection.elements[control.index + 1].source;
    require(marker_source && marker_source->property_arguments.at("text") == form_source,
        "Checked marker lost its source origin");
    const auto index = snapshot.tree->find("summary")->index;
    require(snapshot.inspection.elements[index].source &&
        snapshot.inspection.elements[index].source->property_arguments.at("text") == form_source,
        "Source expression missing from inspection");
}
std::vector<ReplayBox> geometry(const Snapshot& snapshot) {
    std::vector<ReplayBox> result;
    for (const auto& box : snapshot.layout.boxes)
        result.push_back({box.node.index, box.parent, box.border_box, box.border, box.padding,
            box.visible, box.clip, box.scroll});
    return result;
}

// Fixed placeholder metrics and synthetic glyph coverage; no installed fonts.
struct FixtureGlyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};

// Each synchronous host edit owns its request envelope; it cannot borrow the
// next input's mutable payload. The executor checks the ordinary live UI owner.
struct UsernameEdit final : CommandHost {
    const TextEditState& accepted;
    TextEditRequest request;
    std::optional<TextEditState> candidate;
    UsernameEdit(const TextEditState& state, TextEditRequest edit) : accepted(state), request(std::move(edit)) {}
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        require(invocation.command == "settings.edit", "Unexpected username command");
        auto edited = edit_text(accepted, request);
        if (!edited) return {CommandOutcomeStatus::error, {}, std::move(edited.diagnostics)};
        candidate = std::move(*edited.value);
        return {};
    }
};

struct RejectingShaper final : TextShaper {
    PlaceholderTextShaper fallback;
    bool layout;
    std::string rejected;
    explicit RejectingShaper(bool measure, std::string value = "rejected") : layout(measure), rejected(std::move(value)) {}
    Result<TextMetrics> measure(std::string_view value, const TextStyle& style, const TextConstraints& constraints) override {
        if (layout && value == rejected) return {std::nullopt,
            {{"fixture_measure", Severity::error, "", "Injected measurement rejection."}}};
        return fallback.measure(value, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view value, const TextStyle& style, const TextConstraints& constraints) override {
        if (!layout && value == rejected) return {std::nullopt,
            {{"fixture_shape", Severity::error, "", "Injected paint rejection."}}};
        return fallback.shape(value, style, constraints);
    }
};

void run_username() {
    ui::Session view;
    auto username = take(make_text_edit_state(1, "Ada"));
    const auto observed = view.state("username", username);
    const auto disabled = view.state("disabled", false);
    const auto caption = view.computed("summary", [observed] { return "User " + observed.read().text + "; feature disabled"; });
    const auto field = ui::TextInput("Username", observed, "settings.edit",
        {.node = {.id = "username", .source = form_source}, .disabled = disabled});
    const auto form = Settings(false, caption, false, field);
    const auto registry = take(CommandRegistry::create(
        {{"settings.toggle", "Toggle feature"}, {"settings.edit", "Edit username"}},
        {{"settings.toggle", "settings.toggle"}, {"settings.edit", "settings.edit"}}));
    std::uint64_t generation = 0, sequence = 0;
    auto commands = [&](bool allowed = true) { return take(CommandSnapshot::publish(registry, ++generation,
        CommandMode::production, {{"settings.toggle", {true}}, {"settings.edit", {allowed}}})); };
    PlaceholderTextShaper text;
    auto accepted = prepare(view, form, commands(), text);
    view.publish();
    FocusDispatcher focus;
    ReplayRecording recording;
    recording.context = actions; recording.viewport = viewport;
    recording.document = accepted.tree->document(); recording.styles = accepted.styles;
    recording.environment.scale = 1.25f; recording.environment.locale = "en";
    std::vector<UiDrawList> paints{accepted.paint};
    std::vector<std::vector<ReplayBox>> boxes{geometry(accepted)};
    std::vector<std::string> names;
    std::vector<std::string> summaries;
    auto field_name = [](const Snapshot& snapshot) {
        for (const auto& node : snapshot.inspection.semantics.nodes)
            if (node.id == "username") return node.name;
        throw std::runtime_error("Username missing from semantics");
    };
    auto verify_edit = [&](const Snapshot& snapshot, const TextEditState& expected) {
        const auto owner = *snapshot.tree->find("username");
        const auto& node = *snapshot.tree->get(owner);
        const auto selection = "Selection " + std::to_string(expected.anchor) + ":" + std::to_string(expected.caret);
        require(std::get<std::string>(node.children[1].properties.at("text")) == expected.text &&
            std::get<std::string>(node.children[2].properties.at("text")) == selection &&
            summary(snapshot) == "User " + expected.text + "; feature disabled",
            "Host value, selection and summary disagree");
        require(field_name(snapshot) == "Username " + expected.text + " " + selection,
            "Field label/value/selection inspection differs");
        for (std::size_t child = 1; child <= 3; ++child) {
            const auto& source = snapshot.inspection.elements[owner.index + child].source;
            require(source && source->property_arguments.at("text") == form_source,
                "Field projection lost its expression origin");
        }
    };
    verify_edit(accepted, username); names.push_back(field_name(accepted)); summaries.push_back(summary(accepted));
    for (auto time : {1us, 2us}) {
        const InputEvent event{time, FocusNext{}};
        (void)take(focus.dispatch(accepted.hit(), event)); recording.steps.push_back(event);
    }
    require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("username"), "Username focus order differs");
    CommandExecutor executor;
    std::optional<CommandInvocation> retained;
    std::optional<ActionRequest> old_owner;
    unsigned edits = 0;
    auto invoke = [&](TextEditRequest request, bool expect_success, TextShaper* rejected_stage = nullptr) {
        const auto current_ui = accepted.semantic();
        const auto action = take(request_semantic_action(current_ui, *accepted.tree->find("username"), "activate"));
        const auto invocation = take(accepted.commands.resolve_action(current_ui, action,
            accepted.commands.generation(), {}, CommandSource::keyboard, ++sequence));
        UsernameEdit host(username, std::move(request));
        const auto execution = take(executor.execute(accepted.commands, invocation, host, &current_ui));
        require((execution.outcome.status == CommandOutcomeStatus::success) == expect_success,
            "Edit request eligibility differs");
        if (!expect_success) { require(!host.candidate, "Rejected edit produced a candidate"); return; }
        retained = take(accepted.commands.resolve_action(current_ui, action, accepted.commands.generation(), {},
            CommandSource::keyboard, ++sequence));
        old_owner = action;
        const auto previous = username;
        view.update([&] { observed.write(*host.candidate); });
        // Prepare every observable stage before accepting application state.
        auto staged = [&] {
            try { return prepare_candidate(view, form, commands(), rejected_stage ? *rejected_stage : text); }
            catch (...) { view.update([&] { observed.write(previous); }); throw; }
        }();
        if (!staged) {
            view.update([&] { observed.write(previous); });
            require(rejected_stage && staged.diagnostics[0].source &&
                staged.diagnostics[0].source->location == form_source, "Stage rejection lost source diagnostics");
            return;
        }
        require(!rejected_stage, "Injected stage rejection was not reached");
        auto candidate = std::move(*staged.value);
        verify_edit(candidate, *host.candidate);
        view.publish(); username = *host.candidate; accepted = std::move(candidate); ++edits;
        require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("username"),
            "Compatible replacement lost editing focus");
        recording.steps.push_back(ReplayReload{accepted.tree->document(), accepted.styles});
        paints.push_back(accepted.paint); boxes.push_back(geometry(accepted)); names.push_back(field_name(accepted));
        summaries.push_back(summary(accepted));
    };
    // The adapter consumes editing keys before ordinary focus/gamepad routing.
    auto input = [&](InputEvent event, const Snapshot& routing) {
        require(validate(event).empty(), "Invalid normalized input");
        if (take(focus.refresh(routing.hit())).focused != routing.tree->find("username")) return false;
        require(&routing == &accepted, "Only an accepted presentation can dispatch edits");
        std::optional<decltype(TextEditRequest::operation)> operation;
        if (const auto* insert = std::get_if<tessera::TextInput>(&event.data)) operation = TextEditInsert{insert->text};
        if (const auto* key = std::get_if<KeyDown>(&event.data)) {
            if (key->modifiers.control || key->modifiers.alt || key->modifiers.super) return false;
            switch (key->key) {
            case Key::left: operation = TextEditMove{TextMove::left, key->modifiers.shift}; break;
            case Key::right: operation = TextEditMove{TextMove::right, key->modifiers.shift}; break;
            case Key::home: operation = TextEditMove{TextMove::home, key->modifiers.shift}; break;
            case Key::end: operation = TextEditMove{TextMove::end, key->modifiers.shift}; break;
            case Key::backspace: operation = TextEditErase{TextErase::backward}; break;
            case Key::delete_key: operation = TextEditErase{TextErase::forward}; break;
            default: return false;
            }
        }
        if (!operation) return false;
        invoke({username.owner, username.revision, *operation}, true);
        return true;
    };
    auto expect = [&](InputEvent event, const char* value, std::size_t anchor, std::size_t caret) {
        const auto revision = username.revision;
        require(input(event, accepted) && username.text == value && username.anchor == anchor && username.caret == caret &&
            username.revision == revision + 1, "Normalized edit value/selection/revision differs");
    };
    expect({3us, tessera::TextInput{"日😀"}}, "Ada日😀", 10, 10);
    expect({4us, KeyDown{Key::left}}, "Ada日😀", 6, 6);
    expect({5us, KeyDown{Key::left, {.shift = true}}}, "Ada日😀", 6, 3);
    expect({6us, tessera::TextInput{"本"}}, "Ada本😀", 6, 6);
    expect({7us, KeyDown{Key::delete_key}}, "Ada本", 6, 6);
    expect({8us, KeyDown{Key::home}}, "Ada本", 0, 0);
    expect({9us, KeyDown{Key::right}}, "Ada本", 1, 1);
    expect({10us, KeyDown{Key::backspace}}, "da本", 0, 0);
    expect({11us, KeyDown{Key::end}}, "da本", 5, 5);
    require(edits == 9, "Editing keys were dispatched twice");
    const auto unchanged = username;
    const auto stable_document = accepted.tree->document();
    const auto stable_generation = accepted.inspection.generation;
    for (bool measure : {true, false}) {
        RejectingShaper rejecting(measure);
        // Selection is an accepted host observation, so use a fresh edit request
        // that first selects all through the same command route.
        invoke({username.owner, username.revision, TextEditSelection{0, username.text.size()}}, true);
        const auto before = username;
        const auto document = accepted.tree->document();
        const auto generation_before = accepted.inspection.generation;
        invoke({username.owner, username.revision, TextEditInsert{"rejected"}}, true, &rejecting);
        require(username == before && accepted.tree->document() == document &&
            accepted.inspection.generation == generation_before, "Stage rejection changed accepted state");
        verify_edit(prepare(view, form, commands(), text), username);
        invoke({username.owner, username.revision, TextEditInsert{unchanged.text}}, true);
    }
    const auto ui = accepted.semantic();
    UsernameEdit stale_host(username, {username.owner, username.revision, TextEditInsert{"late"}});
    require(retained.has_value(), "Missing retained editing invocation");
    const auto stale_execution = executor.execute(accepted.commands, *retained, stale_host, &ui);
    require(!stale_execution && stale_execution.diagnostics[0].code != "consumed_sequence" && !stale_host.candidate,
        "Retained invocation entered host after replacement");
    require(old_owner && !accepted.commands.resolve_action(ui, *old_owner, accepted.commands.generation(), {},
        CommandSource::agent, ++sequence), "Old tree edit owner accepted");
    invoke({username.owner, username.revision - 1, TextEditInsert{"stale"}}, false);
    invoke({username.owner + 1, username.revision, TextEditInsert{"foreign"}}, false);
    invoke({username.owner, username.revision, TextEditInsert{"\n"}}, false);
    for (bool authored : {false, true}) {
        view.update([&] { disabled.write(authored); });
        const auto denied = prepare(view, form, commands(authored), text);
        const auto owner = *denied.tree->find("username");
        require(!request_semantic_action(denied.semantic(), owner, "activate") &&
            !denied.commands.resolve_action(denied.semantic(), {"activate", "settings.edit", owner},
                denied.commands.generation(), {}, CommandSource::keyboard, ++sequence), "Disabled edit owner accepted");
        FocusDispatcher denied_focus;
        require(!denied_focus.focus(denied.hit(), owner), "Disabled field acquired focus");
        require(!input({13us, tessera::TextInput{"disabled"}}, denied), "Disabled normalized input reached host");
        for (const auto& node : denied.inspection.semantics.nodes) if (node.id == "username")
            require(!node.enabled && !node.focusable && node.actions.empty(), "Disabled field semantics differ");
    }
    view.update([&] { disabled.write(false); });
    const auto accepted_document = accepted.tree->document();
    auto malformed = username; malformed.caret = 3; // Inside 本 at bytes 2..5.
    view.update([&] { observed.write(malformed); });
    const auto invalid = view.lower(form, actions);
    require(!invalid && invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == form_source &&
        accepted.tree->document() == accepted_document, "Malformed candidate replaced accepted presentation");
    view.update([&] { observed.write(username); });
    require(username.text == unchanged.text && username.anchor == unchanged.anchor && username.caret == unchanged.caret &&
        accepted.tree->document() == stable_document && accepted.inspection.generation != stable_generation,
        "Rejected requests changed host value/selection");
    verify_edit(accepted, username);
    // Keys/text are host-translated: replay stores accepted views, not raw edit events.
    recording.steps.push_back(ReplayTick{12us});
    FixtureGlyphs raster; GlyphCache cache(raster); CpuFrameCapture frames(&cache);
    const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    require(first == repeated && first.frames.size() == paints.size() + 1, "Username CPU replay differs");
    for (std::size_t i = 0; i < paints.size(); ++i) {
        require(first.output.generations[i].paint == paints[i] && first.output.generations[i].boxes == boxes[i],
            "Username replay paint/geometry differs");
    }
    for (const auto& observation : first.output.semantics) {
        const auto index = std::min(observation.generation, names.size() - 1);
        bool found = false, found_summary = false;
        for (const auto& node : observation.nodes) {
            if (node.id == "username") { found = true; require(node.name == names[index], "Replay field observation differs"); }
            if (node.id == "summary") { found_summary = true;
                require(node.name == summaries[index], "Replay username summary differs"); }
        }
        require(found && found_summary, "Replay omitted field or summary observation");
    }
    require(first.frames[1].image != first.frames[2].image && first.frames[2].image != first.frames[3].image,
        "Caret and selection changes must be visible in CPU observations");
    std::cout << "Settings TextInput: " << edits << " staged host edits, UTF-8 value/selection/summary, disabled/stale rejection; "
        << first.frames.size() << " deterministic CPU/replay frames\n";
}
struct SettingsData {
    std::string username;
    bool feature = false;
    bool operator==(const SettingsData&) const = default;
};
std::string saved_caption(unsigned count, const SettingsData& settings) {
    return "Saved " + std::to_string(count) + ": " + settings.username +
        (settings.feature ? "; feature enabled" : "; feature disabled");
}
// Injected synchronous application operation. It owns copies, never UI handles
// or references into the editable draft. No filesystem service is involved.
struct SaveObservation {
    std::vector<SettingsData> calls;
    void save(SettingsData settings) { calls.push_back(std::move(settings)); }
};
struct SettingsOperation final : CommandHost {
    std::function<CommandOutcome(const CommandInvocationRecord&)> run;
    explicit SettingsOperation(std::function<CommandOutcome(const CommandInvocationRecord&)> operation)
        : run(std::move(operation)) {}
    CommandOutcome execute(const CommandInvocationRecord& invocation) override { return run(invocation); }
};

// Application section IDs are data, separate from runtime handles and text
// selection. Only the selected branch is authored; all draft state is host/session owned.
struct SettingsSections {
    std::vector<std::string> order{"account", "feature", "about"};
    std::string selected = "account";
    bool operator==(const SettingsSections&) const = default;
};
std::vector<Diagnostic> validate_sections(const SettingsSections& sections) {
    const std::set<std::string> known{"account", "feature", "about"};
    const std::set<std::string> unique(sections.order.begin(), sections.order.end());
    if (unique.empty() || unique.size() != sections.order.size() ||
        !std::includes(known.begin(), known.end(), unique.begin(), unique.end()) || !unique.contains(sections.selected))
        return {{"settings_sections", Severity::error, "/sections",
            "Use a nonempty unique list of account, feature, about and select one of its members.", {},
            DiagnosticSource{form_source, DiagnosticOrigin::argument}}};
    return {};
}
ui::Element SectionSettings(const SettingsSections& sections, ui::Value<TextEditState> username,
    ui::Value<bool> enabled, ui::Value<std::string> caption, ui::Value<std::string> saved, bool disabled = false,
    bool scrolling = false, bool styled = false) {
    auto control = [&](std::string id, bool section = false) {
        return ui::Props{.id = std::move(id),
            .classes = styled ? (section ? std::vector<std::string>{"control", "section"} : std::vector<std::string>{"control"}) :
                std::vector<std::string>{}, .source = form_source};
    };
    std::vector<ui::Element> choices;
    for (const auto& id : sections.order)
        choices.push_back(ui::Button((id == sections.selected ? "* " : "") + id, "settings.section." + id,
            {.node = control("section-" + id, true), .disabled = disabled}));
    std::vector<ui::Element> rows{
        ui::Text("Settings", {.id = "heading", .classes = styled ? std::vector<std::string>{"heading"} : std::vector<std::string>{}}),
        ui::Row(std::move(choices), {.id = "sections", .style = {.gap = 12.f}, .source = form_source}),
        ui::Text("Section " + sections.selected, {.id = "selected-section", .source = form_source})};
    if (sections.selected == "account")
        rows.push_back(SettingsRow({"Account", "username-row"}, {
            ui::TextInput("Username", username, "settings.edit", {.node = control("username")})}));
    else if (sections.selected == "feature")
        rows.push_back(SettingsRow({"Feature", "feature-row"}, {
            ui::Checkbox("Enable feature", enabled, "settings.toggle", {.node = control("toggle")})}));
    else rows.push_back(ui::Text("Choose Account to edit; Apply saves the whole draft.", {.id = "section-help", .source = form_source}));
    const auto status = [&](std::string id) { return ui::Props{.id = std::move(id),
        .classes = styled ? std::vector<std::string>{"status"} : std::vector<std::string>{}, .source = form_source}; };
    rows.push_back(ui::Text(caption, status("summary")));
    auto apply = control("apply");
    if (styled) apply.style.background = apply_ink;
    rows.push_back(ui::Button("Apply", "settings.apply", {.node = std::move(apply)}));
    rows.push_back(ui::Text(saved, status("save-result")));
    if (styled) rows.push_back(ui::Button("Appearance", "settings.appearance", {.node = control("appearance")}));
    return ui::Column(std::move(rows), {.id = "settings", .style = {
        .overflow = scrolling ? Overflow::scroll : Overflow::visible,
        .padding = Edges{12, 12, 12, 12}, .gap = styled ? std::optional<float>{} : 12.f,
        .background = Color{0.94f, 0.94f, 0.96f, 1}}, .source = form_source});
}

void run_sections() {
    constexpr Size form_viewport{720, 440};
    ui::Session view;
    SettingsSections sections;
    auto username = take(make_text_edit_state(1, "Ada"));
    SettingsData draft{username.text, false};
    SaveObservation saves;
    std::string result = "Not saved";
    const auto observed = view.state("username", username);
    const auto enabled = view.state("enabled", false);
    const auto saved = view.state("save-result", result);
    const auto caption = view.computed("summary", [observed, enabled] {
        return "User " + observed.read().text + (enabled.read() ? "; feature enabled" : "; feature disabled");
    });
    const auto registry = take(CommandRegistry::create({
        {"settings.toggle", "Toggle feature"}, {"settings.edit", "Edit username"}, {"settings.apply", "Apply settings"},
        {"settings.section.account", "Account"}, {"settings.section.feature", "Feature"}, {"settings.section.about", "About"}},
        {{"settings.toggle", "settings.toggle"}, {"settings.edit", "settings.edit"}, {"settings.apply", "settings.apply"},
        {"settings.section.account", "settings.section.account"}, {"settings.section.feature", "settings.section.feature"},
        {"settings.section.about", "settings.section.about"}}));
    std::uint64_t generation = 0, sequence = 0;
    PlaceholderTextShaper text;
    auto stage = [&](const SettingsSections& candidate, TextShaper& shaper, bool authored_disabled = false, bool allowed = true)
        -> Result<Snapshot> {
        auto errors = validate_sections(candidate);
        if (!errors.empty()) return {std::nullopt, std::move(errors)};
        CommandStates states{{"settings.toggle", {true}}, {"settings.edit", {true}}, {"settings.apply", {true}}};
        for (const auto& id : {"account", "feature", "about"})
            states.emplace(std::string("settings.section.") + id, CommandState{allowed});
        auto commands = take(CommandSnapshot::publish(registry, ++generation, CommandMode::production, std::move(states)));
        return prepare_candidate(view, SectionSettings(candidate, observed, enabled, caption, saved, authored_disabled),
            std::move(commands), shaper, form_viewport);
    };
    auto accepted = take(stage(sections, text));
    view.publish();
    FocusDispatcher focus;
    CommandExecutor executor;
    ReplayRecording recording;
    recording.context = actions; recording.viewport = form_viewport;
    recording.document = accepted.tree->document(); recording.styles = accepted.styles;
    recording.environment.scale = 1.25f; recording.environment.locale = "en";
    FixtureGlyphs raster; GlyphCache cache(raster); CpuFrameCapture frames(&cache);
    const auto extent = take(capture_extent(form_viewport, recording.environment.scale));
    using Names = std::map<std::string, std::string>;
    auto observations = [](const Snapshot& snapshot) {
        Names values;
        for (const auto& node : snapshot.inspection.semantics.nodes) if (node.id) values.emplace(*node.id, node.name);
        return values;
    };
    std::vector<UiDrawList> paints;
    std::vector<std::vector<ReplayBox>> boxes;
    std::vector<CapturedImage> images;
    std::vector<Names> names;
    auto remember = [&] {
        const auto values = observations(accepted);
        require(values.at("selected-section") == "Section " + sections.selected &&
            values.at("summary") == "User " + draft.username + (draft.feature ? "; feature enabled" : "; feature disabled") &&
            values.at("save-result") == result && username.text == draft.username,
            "Section, draft, derived summary or save result differs");
        require(values.contains("username") == (sections.selected == "account") &&
            values.contains("toggle") == (sections.selected == "feature") &&
            values.contains("section-help") == (sections.selected == "about"), "Conditional branch differs from selection");
        if (sections.selected == "account") require(values.at("username") == "Username " + username.text + " Selection " +
            std::to_string(username.anchor) + ":" + std::to_string(username.caret), "Restored field observation differs");
        if (sections.selected == "feature") require(values.at("toggle") == (draft.feature ? "[x] Enable feature" : "[ ] Enable feature"),
            "Restored Checkbox observation differs");
        std::vector<std::string> order;
        for (const auto& element : accepted.inspection.elements) if (element.id && element.id->starts_with("section-")) {
            const auto id = element.id->substr(8);
            if (id == "help") continue;
            order.push_back(id);
            require(values.at(*element.id) == (id == sections.selected ? "* " : "") + id && element.source &&
                element.source->event_arguments.at("activate") == form_source, "Section identity, marker or origin differs");
        }
        require(order == sections.order, "Repeated section order differs");
        if (!saves.calls.empty()) require(result == saved_caption(static_cast<unsigned>(saves.calls.size()), saves.calls.back()),
            "Saved observation differs from owned settings");
        paints.push_back(accepted.paint); boxes.push_back(geometry(accepted)); names.push_back(values);
        images.push_back(take(frames.capture({images.size(), form_viewport, recording.environment.scale, extent, &accepted.paint})));
    };
    remember();
    auto stable = [&] { return std::tuple{sections, username, draft, saves.calls, result, accepted.tree->document(),
        accepted.inspection, accepted.commands.generation(), accepted.paint}; };
    unsigned entered = 0;
    std::vector<CommandInvocationRecord> selections;
    std::optional<CommandInvocation> pending;
    std::optional<ActionRequest> old_owner;
    auto operate = [&](const ActionRequest& action, CommandSource source, std::optional<TextEditRequest> request = {},
        TextShaper* rejecting = nullptr) {
        const auto before = stable();
        const auto current_ui = accepted.semantic();
        const auto invocation = take(accepted.commands.resolve_action(current_ui, action, accepted.commands.generation(), {}, source, ++sequence));
        SettingsOperation host([&, request = std::move(request)](const CommandInvocationRecord& call) -> CommandOutcome {
            ++entered;
            auto next_sections = sections;
            auto next_username = username;
            auto next_draft = draft;
            auto next_result = result;
            const bool select = call.command.starts_with("settings.section.");
            if (select) next_sections.selected = call.command.substr(17);
            else if (call.command == "settings.edit") {
                require(request.has_value(), "Section field edit needs a request");
                auto edited = edit_text(username, *request);
                if (!edited) return {CommandOutcomeStatus::error, {}, std::move(edited.diagnostics)};
                next_username = std::move(*edited.value); next_draft.username = next_username.text;
            } else if (call.command == "settings.toggle") next_draft.feature = !next_draft.feature;
            else {
                require(call.command == "settings.apply", "Unexpected section command");
                next_result = saved_caption(static_cast<unsigned>(saves.calls.size()) + 1, next_draft);
            }
            auto restore = [&] { view.update([&] { observed.write(username); enabled.write(draft.feature); saved.write(result); }); };
            view.update([&] { observed.write(next_username); enabled.write(next_draft.feature); saved.write(next_result); });
            auto staged = [&] {
                try { return stage(next_sections, rejecting ? *rejecting : text); }
                catch (...) { restore(); throw; }
            }();
            if (!staged) { restore(); return {CommandOutcomeStatus::error, {}, std::move(staged.diagnostics)}; }
            pending = take(accepted.commands.resolve_action(current_ui, action, accepted.commands.generation(), {}, source, ++sequence));
            old_owner = action;
            if (select) selections.push_back(call);
            if (call.command == "settings.apply") saves.save(next_draft);
            sections = std::move(next_sections); username = std::move(next_username);
            draft = std::move(next_draft); result = std::move(next_result);
            view.publish(); accepted = std::move(*staged.value);
            return {};
        });
        const auto execution = take(executor.execute(accepted.commands, invocation, host, &current_ui));
        if (rejecting) require(execution.outcome.status == CommandOutcomeStatus::error && !execution.outcome.diagnostics.empty() &&
            execution.outcome.diagnostics[0].source && execution.outcome.diagnostics[0].source->location == form_source && before == stable(),
            "Rejected selection changed accepted state or lost located diagnostics");
        else require(execution.outcome.status == CommandOutcomeStatus::success, "Section operation failed");
        const auto after = stable();
        const auto refreshed = accepted.semantic();
        const auto duplicate = executor.execute(accepted.commands, invocation, host, &refreshed);
        require(!duplicate && duplicate.diagnostics[0].code == "consumed_sequence" && stable() == after, "Section request executed twice");
        if (!rejecting) {
            recording.steps.push_back(ReplayReload{accepted.tree->document(), accepted.styles}); remember();
        }
    };
    auto logical = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
        const auto dispatched = take(focus.dispatch(accepted.hit(), event));
        recording.steps.push_back(event);
        for (const auto& action : dispatched.actions) operate(action, source);
    };
    auto semantic = [&](const std::string& id) {
        recording.steps.push_back(ReplaySemanticAction{AuthorIdTarget{id}});
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find(id), "activate")), CommandSource::agent);
    };
    auto focused = [&](const std::string& id) {
        const auto current = take(focus.refresh(accepted.hit())).focused;
        if (current != accepted.tree->find(id)) throw std::runtime_error("Expected focus " + id + ", got " +
            (current ? accepted.tree->get(*current)->id.value_or("<anonymous>") : "<none>"));
    };
    logical({1us, FocusNext{}}); focused("section-account");
    for (int i = 0; i < 3; ++i) logical({2us, FocusNext{}});
    focused("username");
    const InputEvent committed{3us, tessera::TextInput{"本"}};
    require(validate(committed).empty(), "Malformed section text input");
    operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate")), CommandSource::keyboard,
        TextEditRequest{username.owner, username.revision, TextEditInsert{std::get<tessera::TextInput>(committed.data).text}});
    const auto retained_edit = username;
    const auto removed_edit_owner = take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate"));
    logical({4us, FocusPrevious{}}); logical({4us, FocusPrevious{}}); focused("section-feature");
    logical({5us, Activate{}}); focused("section-feature");
    logical({6us, FocusNext{}}); logical({6us, FocusNext{}}); focused("toggle");
    logical({7us, Activate{}}); logical({8us, FocusNext{}}); logical({9us, Activate{}}); focused("apply");
    require(draft == SettingsData{"Ada本", true} && saves.calls == std::vector<SettingsData>{draft}, "Section draft or save differs");
    logical({10us, FocusPrevious{}}); logical({10us, FocusPrevious{}}); focused("section-about");
    logical({11us, Activate{}}, CommandSource::gamepad); focused("section-about");
    const auto saved_state = std::tuple{username, draft, saves.calls, result};
    const auto box = accepted.layout.boxes[accepted.tree->find("section-account")->index].border_box;
    const Point point{box.origin.x + 1, box.origin.y + 1};
    PointerDispatcher pointer;
    const InputEvent down{12us, PointerDown{{1}, point}}, up{13us, PointerUp{{1}, point}};
    const auto before_press = stable();
    require(take(pointer.dispatch(accepted.hit(), down)).actions.empty() && stable() == before_press, "Section pointer press selected");
    recording.steps.push_back(down);
    const auto click = take(pointer.dispatch(accepted.hit(), up)); recording.steps.push_back(up);
    require(click.actions.size() == 1, "Section pointer release did not request once");
    operate(click.actions[0], CommandSource::pointer); focused("section-about");
    semantic("section-feature"); focused("section-about");
    require(std::tuple{username, draft, saves.calls, result} == saved_state, "Selection changed draft or saved settings");
    require(selections.size() == 4, "Missing four section input routes");
    const std::vector<CommandSource> sources{CommandSource::keyboard, CommandSource::gamepad, CommandSource::pointer, CommandSource::agent};
    for (std::size_t i = 0; i < sources.size(); ++i)
        require(selections[i].source == sources[i] && selections[i].owner && selections[i].owner->action == selections[i].command,
            "Section routes did not share ordinary commands and owners");
    for (bool measure : {true, false}) {
        RejectingShaper rejecting(measure, "Section about");
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("section-about"), "activate")),
            CommandSource::agent, {}, &rejecting);
    }
    semantic("section-about"); semantic("section-account");
    logical({14us, FocusNext{}}); focused("username");
    auto replace_list = [&](SettingsSections candidate, bool valid) {
        const auto before = stable();
        auto staged = stage(candidate, text);
        if (!valid) {
            require(!staged && !staged.diagnostics.empty() && staged.diagnostics[0].source &&
                staged.diagnostics[0].source->location == form_source && before == stable(), "Invalid section list changed accepted state");
            return;
        }
        accepted = take(std::move(staged)); sections = std::move(candidate); view.publish();
        recording.steps.push_back(ReplayReload{accepted.tree->document(), accepted.styles}); remember();
        require(std::tuple{username, draft, saves.calls, result} == saved_state, "List replacement changed host observations");
    };
    replace_list({{"about", "account", "feature"}, "account"}, true); focused("username");
    replace_list({{"account", "feature"}, "account"}, true); focused("username");
    replace_list({{"feature"}, "feature"}, true); focused("toggle");
    replace_list({{"account", "feature", "about"}, "feature"}, true); focused("toggle");
    semantic("section-account"); focused("username");
    require(username == retained_edit, "Removed/reinserted Account lost its host edit lifetime");
    for (const auto& invalid : std::vector<SettingsSections>{{{}, "account"}, {{"account", "account"}, "account"},
        {{"unknown"}, "unknown"}, {{"account"}, "about"}}) replace_list(invalid, false);
    const auto before_rejections = stable();
    const auto entered_before = entered;
    SettingsOperation forbidden([&](const CommandInvocationRecord&) { ++entered; return CommandOutcome{}; });
    const auto current_ui = accepted.semantic();
    require(pending && old_owner && !executor.execute(accepted.commands, *pending, forbidden, &current_ui) &&
        !accepted.commands.resolve_action(current_ui, *old_owner, accepted.commands.generation(), {}, CommandSource::agent, ++sequence) &&
        !accepted.commands.resolve_action(current_ui, removed_edit_owner, accepted.commands.generation(), {}, CommandSource::keyboard, ++sequence),
        "Stale section invocation/owner or removed field owner accepted");
    const auto live_action = take(request_semantic_action(current_ui, *accepted.tree->find("section-feature"), "activate"));
    const auto live = take(accepted.commands.resolve_action(current_ui, live_action, accepted.commands.generation(), {}, CommandSource::agent, ++sequence));
    const auto foreign = take(prepare_candidate(view, SectionSettings(sections, observed, enabled, caption, saved), accepted.commands, text, form_viewport));
    const auto foreign_ui = foreign.semantic();
    require(!executor.execute(foreign.commands, live, forbidden, &foreign_ui), "Foreign section tree accepted equal command counters");
    for (bool authored : {false, true}) {
        const auto denied = take(stage(sections, text, authored, authored));
        const auto denied_ui = denied.semantic();
        for (const auto& id : sections.order) {
            const auto owner = *denied.tree->find("section-" + id);
            require(!request_semantic_action(denied_ui, owner, "activate") &&
                !denied.commands.resolve_action(denied_ui, {"activate", "settings.section." + id, owner}, denied.commands.generation(), {},
                    CommandSource::agent, ++sequence), "Disabled section resolved");
            FocusDispatcher denied_focus;
            require(!denied_focus.focus(denied.hit(), owner) &&
                take(denied_focus.dispatch(denied.hit(), {15us, Activate{}})).actions.empty(), "Disabled section focused or activated");
            const auto bounds = denied.layout.boxes[owner.index].border_box;
            const Point at{bounds.origin.x + 1, bounds.origin.y + 1};
            PointerDispatcher denied_pointer;
            require(take(denied_pointer.dispatch(denied.hit(), {16us, PointerDown{{2}, at}})).actions.empty() &&
                take(denied_pointer.dispatch(denied.hit(), {17us, PointerUp{{2}, at}})).actions.empty(), "Disabled section clicked");
        }
        require(!executor.execute(denied.commands, live, forbidden, &denied_ui), "Disabled selection entered host");
    }
    require(stable() == before_rejections && entered == entered_before &&
        observations(take(stage(sections, text))) == names.back(), "Rejected section request changed host or reactive state");
    recording.steps.push_back(ReplayTick{18us});
    const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    require(first == repeated && first.frames.size() == paints.size() + 1, "Section CPU/replay differs");
    for (std::size_t i = 0; i < paints.size(); ++i)
        require(first.output.generations[i].paint == paints[i] && first.output.generations[i].boxes == boxes[i] &&
            first.frames[i].image == images[i], "Accepted section presentation differs from replay/capture");
    for (const auto& observation : first.output.semantics) {
        Names values;
        for (const auto& node : observation.nodes) if (node.id) values.emplace(*node.id, node.name);
        require(values == names[std::min(observation.generation, names.size() - 1)], "Section replay inspection differs");
    }
    const std::vector<std::string> expected_actions{"settings.section.feature", "settings.toggle", "settings.apply", "settings.section.about",
        "settings.section.account", "settings.section.feature", "settings.section.about", "settings.section.account", "settings.section.account"};
    require(first.output.actions.size() == expected_actions.size(), "Section replay action count differs");
    for (std::size_t i = 0; i < expected_actions.size(); ++i)
        require(first.output.actions[i].action == expected_actions[i], "Section replay action order differs");
    require(images[1] != images[2] && images[2] != images[4], "Conditional content absent from CPU capture");
    std::cout << "Settings sections: repeated/conditional composition, four selection routes, draft/save/focus retention and rejection; "
        << first.frames.size() << " deterministic CPU/replay frames\n";
}

// Scrolling is host state keyed by author identity. The application composition
// above only declares overflow; this fixture stages full presentations before
// replacing either that state or the accepted command/inspection generation.
void run_scrolling(bool styled = false, bool feedback = false) {
    constexpr Size form_viewport{720, 160};
    ui::Session view;
    SettingsSections sections;
    auto username = take(make_text_edit_state(1, "Ada"));
    SettingsData draft{username.text, false};
    SaveObservation saves;
    std::string result = "Not saved";
    Point offset;
    bool compact = false;
    auto accepted_appearance = appearance(compact, feedback);
    FocusDispatcher focus;
    PointerDispatcher feedback_pointer;
    const auto observed = view.state("username", username);
    const auto enabled = view.state("enabled", false);
    const auto saved = view.state("save-result", result);
    const auto caption = view.computed("summary", [observed, enabled] {
        return "User " + observed.read().text + (enabled.read() ? "; feature enabled" : "; feature disabled");
    });
    const auto registry = take(CommandRegistry::create({
        {"settings.toggle", "Toggle feature"}, {"settings.edit", "Edit username"}, {"settings.apply", "Apply settings"},
        {"settings.section.account", "Account"}, {"settings.section.feature", "Feature"}, {"settings.section.about", "About"},
        {"settings.appearance", "Change appearance"}},
        {{"settings.toggle", "settings.toggle"}, {"settings.edit", "settings.edit"}, {"settings.apply", "settings.apply"},
        {"settings.section.account", "settings.section.account"}, {"settings.section.feature", "settings.section.feature"},
        {"settings.section.about", "settings.section.about"}, {"settings.appearance", "settings.appearance"}}));
    std::uint64_t generation = 0, sequence = 0;
    PlaceholderTextShaper text;
    auto stage = [&](const SettingsSections& candidate, Point requested, TextShaper& shaper,
        FocusDispatcher candidate_focus, bool reveal, bool allow_apply = true,
        const SettingsAppearance* candidate_appearance = nullptr) -> Result<Snapshot> {
        const auto commands = take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
            {{"settings.toggle", {true}}, {"settings.edit", {true}}, {"settings.apply", {allow_apply}},
             {"settings.section.account", {true}}, {"settings.section.feature", {true}}, {"settings.section.about", {true}},
             {"settings.appearance", {styled && allow_apply}}}));
        const auto form = SectionSettings(candidate, observed, enabled, caption, saved, false, true, styled);
        const auto* styles = styled ? (candidate_appearance ? candidate_appearance : &accepted_appearance) : nullptr;
        auto staged = prepare_candidate(view, form, commands, shaper, form_viewport, {{"settings", requested}}, styles);
        if (!staged) return staged;
        const auto target = take(candidate_focus.refresh(staged.value->hit())).focused;
        if (target && reveal) {
            const auto updates = take(scroll_into_view(staged.value->hit(), *target));
            if (!updates.empty())
                staged = prepare_candidate(view, form, commands, shaper, form_viewport, {{"settings", updates[0].offset}}, styles);
        }
        if (staged && feedback) {
            auto visuals = prepare_feedback(*staged.value, *styles,
                interaction({}, take(candidate_focus.refresh(staged.value->hit()))), shaper, form_viewport);
            if (!visuals) return {std::nullopt, std::move(visuals.diagnostics)};
            accept_feedback(*staged.value, std::move(*visuals.value));
        }
        return staged;
    };
    auto accepted = take(stage(sections, offset, text, focus, false));
    view.publish();
    auto root = [](const Snapshot& snapshot) -> const LayoutBox& {
        return snapshot.layout.boxes[snapshot.tree->find("settings")->index];
    };
    auto bounds = [](const Snapshot& snapshot, const std::string& id) -> const LayoutBox& {
        return snapshot.layout.boxes[snapshot.tree->find(id)->index];
    };
    auto fully_visible = [&](const std::string& id) {
        const auto& box = bounds(accepted, id);
        return box.clip && intersect(box.border_box, *box.clip) == box.border_box;
    };
    auto commit = [&](Snapshot candidate) {
        offset = root(candidate).scroll->offset;
        accepted = std::move(candidate);
        take(focus.refresh(accepted.hit()));
        take(feedback_pointer.refresh(accepted.hit()));
    };
    auto stable = [&] { return std::tuple{sections, username, draft, saves.calls, result, offset, compact, accepted_appearance,
        accepted.tree->document(), accepted.inspection, accepted.commands.generation(), accepted.paint,
        take(focus.refresh(accepted.hit())).focused, take(feedback_pointer.refresh(accepted.hit())).pointers}; };
    FixtureGlyphs raster; GlyphCache cache(raster); CpuFrameCapture frames(&cache);
    unsigned observations = 0;
    auto verify_view = [&] {
        require(root(accepted).border_box == Rect{{0, 0}, form_viewport} &&
            offset == root(accepted).scroll->offset && offset.y >= 0 && offset.y <= root(accepted).scroll_limit().y,
            "Scroll viewport, accepted offset or clamping differs");
        for (const auto& box : accepted.layout.boxes) {
            if (box.node != *accepted.tree->find("settings"))
                require(box.clip == std::optional<Rect>{root(accepted).padding_box()}, "Descendant lost scroll clip");
            const auto& inspected = accepted.inspection.elements[box.node.index].geometry;
            require(inspected && inspected->border_box == box.border_box && inspected->clip == box.clip,
                "Inspection bounds disagree with scroll layout");
        }
        require(summary(accepted) == "User " + draft.username + (draft.feature ? "; feature enabled" : "; feature disabled"),
            "Scrolling changed derived draft");
        std::map<std::string, std::string> names;
        for (const auto& node : accepted.inspection.semantics.nodes) if (node.id) names.emplace(*node.id, node.name);
        require(names.at("selected-section") == "Section " + sections.selected && names.at("save-result") == result &&
            username.text == draft.username && (saves.calls.empty() ? result == "Not saved" :
                result == saved_caption(static_cast<unsigned>(saves.calls.size()), saves.calls.back())),
            "Scrolled selection, edit or owned save result differs from inspection");
        if (styled) {
            const auto style = [&](const std::string& id) -> const ResolvedStyle& {
                return accepted.inspection.elements[accepted.tree->find(id)->index].style;
            };
            const auto body = compact ? 14.f : 16.f;
            const auto ink = compact ? Color{0.1f, 0.15f, 0.2f, 1} : Color{0.2f, 0.2f, 0.25f, 1};
            const auto surface = compact ? Color{0.85f, 0.9f, 0.95f, 1} : Color{0.95f, 0.95f, 0.98f, 1};
            require(style("settings").text.size == body && style("settings").gap == (compact ? 8.f : 12.f) &&
                style("heading").text.size == 20 && style("heading").color == ink &&
                style("summary").color == Color{0.25f, 0.3f, 0.4f, 1} &&
                style("save-result").color == style("summary").color,
                "Shared typography, inheritance or theme spacing differs from inspection");
            for (const auto& id : {"section-account", "section-feature", "section-about", "username", "toggle", "apply", "appearance"}) {
                if (const auto owner = accepted.tree->find(id)) {
                    const auto& resolved = style(id);
                    auto expected_border = Color{0.35f, 0.4f, 0.5f, 1};
                    if (feedback) {
                        const auto state = interaction(take(feedback_pointer.refresh(accepted.hit())), take(focus.refresh(accepted.hit())));
                        auto contains = [&](const std::vector<NodeHandle>& nodes) {
                            for (const auto& node : nodes) {
                                auto index = node.index;
                                while (index != no_inspection_parent) {
                                    if (index == owner->index) return true;
                                    index = accepted.inspection.elements[index].parent;
                                }
                            }
                            return false;
                        };
                        if (contains(state.hovered)) expected_border = hover_border;
                        if (state.focused == owner) expected_border = focus_border;
                        if (contains(state.active)) expected_border = active_border;
                    }
                    require(resolved.text.size == body && resolved.color == ink &&
                        resolved.padding == (compact ? Edges{2, 2, 2, 2} : Edges{4, 4, 4, 4}) &&
                        resolved.border == Edges{1, 1, 1, 1} && resolved.border_color == expected_border && resolved.corner_radius == 4 &&
                        resolved.background == (std::string_view(id) == "apply" ? apply_ink : surface),
                        "Theme, shared control rule and instance precedence differ");
                    if (std::string_view(id).starts_with("section-"))
                        require(resolved.text.weight == 600, "Section style was not composed with control style");
                    for (const auto& child : accepted.inspection.elements[owner->index].children)
                        require(accepted.styles[child].text == resolved.text && accepted.styles[child].color == ink,
                            "Generated control labels lost inherited typography");
                }
            }
        }
        // Replay v1 reload resets offsets. Reconstruct this accepted host view
        // explicitly from zero using normalized scrolling; no host command is
        // executed by replay and no hidden offset state is assumed.
        ReplayRecording recording;
        recording.context = actions; recording.viewport = form_viewport;
        recording.document = accepted.tree->document(); recording.styles = accepted.styles;
        recording.environment.scale = 1.25f; recording.environment.locale = "en";
        if (offset != Point{}) recording.steps.push_back(InputEvent{1us, Scroll{{16, 16}, offset}});
        recording.steps.push_back(ReplayTick{2us});
        const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        require(first == repeated && first.output.generations.back().boxes == geometry(accepted) &&
            first.output.generations.back().paint == accepted.paint &&
            first.output.semantics.back().nodes == own_semantic_records(accepted.inspection.semantics),
            "Accepted scroll geometry, semantics or paint differs from repeated replay");
        const auto extent = take(capture_extent(form_viewport, recording.environment.scale));
        require(first.frames.back().image == take(frames.capture({0, form_viewport, recording.environment.scale, extent, &accepted.paint})),
            "Accepted scroll CPU bytes differ from replay");
        ++observations;
    };
    require(!fully_visible("username") && !fully_visible("apply") && offset == Point{} &&
        bounds(accepted, "apply").border_box.origin.y > form_viewport.height,
        "Constrained fixture must require scrolling to reach lower controls");
    const auto clipped_apply = bounds(accepted, "apply").border_box;
    const Point clipped_point{clipped_apply.origin.x + 1, clipped_apply.origin.y + 1};
    require(!take(hit_test(accepted.hit(), clipped_point)).target, "Clipped Apply remained hittable");
    verify_view();
    auto scroll = [&](Point delta, TextShaper* rejecting = nullptr) {
        const auto before = stable();
        const auto routed = take(route_scroll(accepted.hit(), Scroll{{16, 16}, delta}));
        require(routed.updates.size() == 1 && routed.updates[0].container == accepted.tree->find("settings"),
            "Wheel must update the declared scroll owner once");
        auto candidate = stage(sections, routed.updates[0].offset, rejecting ? *rejecting : text, focus, false);
        if (rejecting) {
            require(!candidate && candidate.diagnostics[0].source && candidate.diagnostics[0].source->location == form_source &&
                stable() == before, "Rejected scroll presentation changed accepted state or lost source");
        } else { commit(take(std::move(candidate))); verify_view(); }
    };
    scroll({0, 48});
    require(offset == Point{0, 48}, "Wheel did not apply exact normalized delta");
    std::chrono::microseconds input_time{};
    auto logical = [&](InputEvent input) {
        input.timestamp = ++input_time;
        auto candidate_focus = focus;
        const auto dispatched = take(candidate_focus.dispatch(accepted.hit(), input));
        require(dispatched.actions.empty(), "Navigation unexpectedly activated a command");
        const auto target = dispatched.focused;
        if (target && !take(scroll_into_view(accepted.hit(), *target)).empty())
            commit(take(stage(sections, offset, text, candidate_focus, true)));
        else if (feedback)
            accept_feedback(accepted, take(prepare_feedback(accepted, accepted_appearance,
                interaction(take(feedback_pointer.refresh(accepted.hit())), dispatched), text, form_viewport)));
        focus = std::move(candidate_focus);
        take(focus.refresh(accepted.hit()));
        if (const auto current = take(focus.refresh(accepted.hit())).focused) {
            require(fully_visible(*accepted.tree->get(*current)->id) && take(scroll_into_view(accepted.hit(), *current)).empty(),
                "Logical navigation did not reveal the focused control");
        }
        verify_view();
    };
    for (int i = 0; i < 4; ++i) logical({3us, FocusNext{}});
    require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("username"), "Expected username focus");
    const auto old_username = *accepted.tree->find("username");
    CommandExecutor executor;
    std::optional<CommandInvocation> pending;
    unsigned entered = 0;
    auto operate = [&](const ActionRequest& action, CommandSource source, std::optional<TextEditRequest> edit = {},
        TextShaper* rejecting = nullptr, const SettingsAppearance* invalid_appearance = nullptr) {
        const auto before = stable();
        const auto current_ui = accepted.semantic();
        const auto invocation = take(accepted.commands.resolve_action(current_ui, action, accepted.commands.generation(), {}, source, ++sequence));
        SettingsOperation host([&](const CommandInvocationRecord& call) -> CommandOutcome {
            ++entered;
            auto next_sections = sections;
            auto next_username = username;
            auto next_draft = draft;
            auto next_result = result;
            const auto next_compact = call.command == "settings.appearance" ? !compact : compact;
            const auto next_appearance = call.command == "settings.appearance" ?
                (invalid_appearance ? *invalid_appearance : appearance(next_compact, feedback)) : accepted_appearance;
            if (call.command.starts_with("settings.section.")) next_sections.selected = call.command.substr(17);
            else if (call.command == "settings.edit") {
                require(edit.has_value(), "Scroll form edit needs its owned request");
                auto edited = edit_text(username, *edit);
                if (!edited) return {CommandOutcomeStatus::error, {}, std::move(edited.diagnostics)};
                next_username = std::move(*edited.value); next_draft.username = next_username.text;
            } else if (call.command == "settings.toggle") next_draft.feature = !next_draft.feature;
            else if (call.command != "settings.appearance") {
                require(call.command == "settings.apply", "Unexpected scroll form command");
                next_result = saved_caption(static_cast<unsigned>(saves.calls.size()) + 1, next_draft);
            }
            auto restore = [&] { view.update([&] { observed.write(username); enabled.write(draft.feature); saved.write(result); }); };
            view.update([&] { observed.write(next_username); enabled.write(next_draft.feature); saved.write(next_result); });
            auto candidate = [&] {
                try { return stage(next_sections, offset, rejecting ? *rejecting : text, focus, true, true, &next_appearance); }
                catch (...) { restore(); throw; }
            }();
            if (!candidate) { restore(); return {CommandOutcomeStatus::error, {}, std::move(candidate.diagnostics)}; }
            pending = take(accepted.commands.resolve_action(current_ui, action, accepted.commands.generation(), {}, source, ++sequence));
            if (call.command == "settings.apply") saves.save(next_draft);
            sections = std::move(next_sections); username = std::move(next_username);
            draft = std::move(next_draft); result = std::move(next_result);
            compact = next_compact; accepted_appearance = next_appearance;
            view.publish(); commit(std::move(*candidate.value));
            return {};
        });
        const auto execution = take(executor.execute(accepted.commands, invocation, host, &current_ui));
        if (rejecting || invalid_appearance) require(execution.outcome.status == CommandOutcomeStatus::error &&
            !execution.outcome.diagnostics.empty() && execution.outcome.diagnostics[0].source && before == stable(),
            "Rejected scroll form command changed accepted state");
        else require(execution.outcome.status == CommandOutcomeStatus::success, "Scroll form command failed");
        const auto refreshed = accepted.semantic();
        require(!executor.execute(accepted.commands, invocation, host, &refreshed), "Scroll form sequence executed twice");
        verify_view();
        return execution.outcome;
    };
    auto semantic = [&](const std::string& id, TextShaper* rejecting = nullptr) {
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find(id), "activate")), CommandSource::agent, {}, rejecting);
    };
    const InputEvent committed{4us, tessera::TextInput{"本"}};
    require(validate(committed).empty(), "Malformed committed input");
    operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate")), CommandSource::keyboard,
        TextEditRequest{username.owner, username.revision, TextEditInsert{std::get<tessera::TextInput>(committed.data).text}});
    const auto retained_username = username;
    require(!scroll_into_view(accepted.hit(), old_username), "Replaced username handle revealed");
    if (styled) {
        const auto before = std::tuple{sections, username, draft, saves.calls, result};
        const auto extent = root(accepted).scroll->extent;
        semantic("appearance");
        require(compact && root(accepted).scroll->extent.height < extent.height && fully_visible("username") &&
            take(focus.refresh(accepted.hit())).focused == accepted.tree->find("username") &&
            std::tuple{sections, username, draft, saves.calls, result} == before,
            "Compact theme lost edit/focus/data or did not change content geometry");
        semantic("appearance");
        require(!compact && root(accepted).scroll->extent == extent && fully_visible("username"),
            "Comfortable theme did not restore geometry and reveal focus");
    }
    logical({5us, FocusNext{}});
    require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("apply") && fully_visible("apply"), "Apply unreachable by navigation");
    auto activate = [&](CommandSource source) {
        const auto dispatched = take(focus.dispatch(accepted.hit(), {++input_time, Activate{}}));
        require(dispatched.actions.size() == 1, "Focused activation missing");
        operate(dispatched.actions[0], source);
    };
    activate(CommandSource::gamepad);
    require(saves.calls == std::vector<SettingsData>{{"Ada本", false}}, "Scrolled Apply did not save draft");
    const auto apply_box = bounds(accepted, "apply").border_box;
    const Point apply_point{apply_box.origin.x + 1, apply_box.origin.y + 1};
    PointerDispatcher pointer;
    require(take(pointer.dispatch(accepted.hit(), {1us, PointerDown{{1}, apply_point}})).actions.empty(),
        "Visible Apply activated on pointer press");
    const auto released = take(pointer.dispatch(accepted.hit(), {2us, PointerUp{{1}, apply_point}}));
    require(released.actions.size() == 1 && released.actions[0].action == "settings.apply" &&
        released.actions[0].target == accepted.tree->find("apply"), "Revealed Apply hit geometry disagrees with command owner");
    scroll({0, 4096});
    require(offset == root(accepted).scroll_limit(), "Wheel did not clamp at bottom");
    const auto saved_state = std::tuple{username, draft, saves.calls, result};
    for (bool measure : {true, false}) {
        RejectingShaper rejecting(measure, "User Ada本; feature disabled");
        scroll({0, -48}, &rejecting);
        semantic("apply", &rejecting);
    }
    const auto account_bottom = offset;
    semantic("section-about");
    require(offset.y < account_bottom.y && offset == root(accepted).scroll_limit() && fully_visible("apply"),
        "Shorter branch did not clamp retained bottom offset");
    semantic("section-account");
    require(std::tuple{username, draft, saves.calls, result} == saved_state, "Branch replacement changed retained settings");
    scroll({0, -4096});
    require(offset == Point{} && std::tuple{username, draft, saves.calls, result} == saved_state, "Wheel changed draft or save");
    logical({7us, FocusPrevious{}});
    semantic("section-feature");
    require(sections.selected == "feature" && username == retained_username && fully_visible("toggle"), "Feature replacement lost edit state or focus reveal");
    activate(CommandSource::keyboard);
    logical({8us, FocusNext{}}); activate(CommandSource::keyboard);
    require(saves.calls.size() == 2 && saves.calls.back() == SettingsData{"Ada本", true}, "Feature Apply saved wrong draft");
    scroll({0, 4096});
    const auto prior_offset = offset;
    semantic("section-about");
    require(offset.y <= prior_offset.y && offset.y <= root(accepted).scroll_limit().y && fully_visible("apply"),
        "Shorter About content did not clamp/reveal retained offset");
    semantic("section-account");
    require(username == retained_username && draft == SettingsData{"Ada本", true} && saves.calls.size() == 2 && fully_visible("apply"),
        "Account restoration lost retained edit/draft/save observations");
    if (styled) {
        const auto retained = std::tuple{sections, username, draft, saves.calls, result};
        const auto previous_offset = offset;
        const auto extent = root(accepted).scroll->extent;
        semantic("appearance");
        require(compact && offset.y <= previous_offset.y && root(accepted).scroll->extent.height < extent.height &&
            fully_visible("apply") && take(focus.refresh(accepted.hit())).focused == accepted.tree->find("apply") &&
            std::tuple{sections, username, draft, saves.calls, result} == retained,
            "Saved draft or Apply focus did not survive compact theme and offset clamping");
        for (int invalid = 0; invalid < 3; ++invalid) {
            auto malformed = appearance(false);
            if (invalid == 0) malformed.theme.variables[0].value = ThemeValue{Color{1, 0, 0, 1}};
            if (invalid == 1) malformed.theme.variables[0].value = ThemeValue{0.f};
            if (invalid == 2) malformed.sheet.rules[0].declarations.corner_radius = -1.f;
            const auto outcome = operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("appearance"), "activate")),
                CommandSource::agent, {}, nullptr, &malformed);
            const auto& diagnostic = outcome.diagnostics.front();
            require(diagnostic.code == (invalid == 0 ? "theme_type" : "out_of_range") &&
                diagnostic.path.starts_with(invalid == 2 ? "/result/host/sheet/rules/0/declarations/" : "/result/host/theme/defaults/rules/0/") &&
                diagnostic.source->location == form_source,
                "Invalid appearance lost its actionable field path or host expression source");
        }
        for (bool measure : {true, false}) {
            RejectingShaper rejecting(measure, "User Ada本; feature enabled");
            semantic("appearance", &rejecting);
        }
        semantic("appearance");
        require(!compact && std::tuple{sections, username, draft, saves.calls, result} == retained && fully_visible("apply"),
            "Fresh appearance retry changed saved draft or lost focus reveal");
        // A normalized pointer activates only on release. Its rebuilt tree
        // clears pointer capture; later events use the newly accepted bounds.
        logical({9us, FocusNext{}});
        require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("appearance"), "Appearance unreachable by navigation");
        activate(CommandSource::keyboard);
        activate(CommandSource::gamepad);
        const auto box = bounds(accepted, "appearance").border_box;
        const Point point{box.origin.x + 2, box.origin.y + 2};
        PointerDispatcher appearance_pointer;
        require(take(appearance_pointer.dispatch(accepted.hit(), {1us, PointerDown{{1}, point}})).actions.empty(),
            "Appearance activated on pointer press");
        const auto release = take(appearance_pointer.dispatch(accepted.hit(), {2us, PointerUp{{1}, point}}));
        require(release.actions.size() == 1 && release.actions[0].action == "settings.appearance", "Appearance pointer owner differs");
        operate(release.actions[0], CommandSource::pointer);
        semantic("appearance");
        require(!compact && std::tuple{sections, username, draft, saves.calls, result} == retained && fully_visible("appearance"),
            "Appearance input routes changed data or failed to reveal focused control");
    }
    if (feedback) {
        const auto retained = std::tuple{sections, username, draft, saves.calls, result, compact, offset};
        const auto geometry_before = geometry(accepted);
        auto feed = [&](InputEvent event, TextShaper* rejecting = nullptr) {
            const auto before = stable();
            auto candidate_pointer = feedback_pointer;
            auto dispatched = take(candidate_pointer.dispatch(accepted.hit(), event));
            auto visuals = prepare_feedback(accepted, accepted_appearance,
                interaction(dispatched, take(focus.refresh(accepted.hit()))), rejecting ? *rejecting : text, form_viewport);
            if (rejecting) {
                require(!visuals && !visuals.diagnostics.empty() && visuals.diagnostics[0].source &&
                    visuals.diagnostics[0].source->location == form_source && before == stable(),
                    "Rejected feedback changed accepted pointer/style state or lost source");
                return std::vector<ActionRequest>{};
            }
            accept_feedback(accepted, take(std::move(visuals)));
            feedback_pointer = std::move(candidate_pointer);
            const auto refreshed = take(feedback_pointer.refresh(accepted.hit()));
            require(refreshed.pointers == dispatched.pointers && geometry(accepted) == geometry_before &&
                std::tuple{sections, username, draft, saves.calls, result, compact, offset} == retained,
                "Paint-only feedback changed geometry, input targeting or application data");
            verify_view();
            return dispatched.actions;
        };
        const auto owner = *accepted.tree->find("appearance");
        const auto label = accepted.tree->children(owner).front();
        const auto box = accepted.layout.boxes[label.index].border_box;
        const Point point{box.origin.x + 1, box.origin.y + 1};
        const Point outside{-1, -1};
        require(take(hit_test(accepted.hit(), point)).target == label, "Feedback probe must hit generated Text");
        // Move/down/out/back/cancel/up retain tree identity, including capture.
        feed({1us, PointerMove{{7}, point}});
        require(accepted.styles[owner.index].border_color == focus_border &&
            accepted.styles[label.index].border_color != focus_border, "Focus must beat hover and match only its owner");
        feed({2us, PointerDown{{7}, point}});
        require(accepted.styles[owner.index].border_color == active_border, "Press must beat focus/hover");
        feed({3us, PointerMove{{7}, outside}});
        require(accepted.styles[owner.index].border_color == focus_border, "Outside press retained active style");
        feed({4us, PointerMove{{7}, point}});
        require(accepted.styles[owner.index].border_color == active_border, "Returning press did not restore active style");
        feed({5us, PointerCancel{{7}}});
        require(feed({6us, PointerUp{{7}, point}}).empty(), "Cancelled press activated");
        for (bool measure : {true, false}) {
            RejectingShaper rejecting(measure, "User Ada本; feature enabled");
            feed({7us, PointerDown{{7}, point}}, &rejecting);
        }
        feed({8us, PointerDown{{7}, point}});
        const auto release = feed({9us, PointerUp{{7}, point}});
        require(release.size() == 1 && release[0].target == owner && release[0].action == "settings.appearance",
            "Feedback release did not use the ordinary binding owner once");
        operate(release[0], CommandSource::pointer);
        require(take(feedback_pointer.refresh(accepted.hit())).pointers.empty() &&
            !accepted.tree->get(owner) && accepted.styles[accepted.tree->find("appearance")->index].border_color == focus_border,
            "Replacement retained stale pointer capture or lost recovered focus styling");
        // With a different logical focus owner, a generated section label's hover
        // reaches its ancestor without giving that ancestor focus styling.
        logical({10us, FocusNext{}});
        const auto hovered_owner = *accepted.tree->find("section-feature");
        const auto hovered_label = accepted.tree->children(hovered_owner).front();
        const auto hovered_box = accepted.layout.boxes[hovered_label.index].border_box;
        const Point hovered_point{hovered_box.origin.x + 1, hovered_box.origin.y + 1};
        auto candidate_pointer = feedback_pointer;
        const auto hovered = take(candidate_pointer.dispatch(accepted.hit(), {10us, PointerMove{{7}, hovered_point}}));
        require(hovered.target == hovered_label, "Hover probe missed generated section label");
        accept_feedback(accepted, take(prepare_feedback(accepted, accepted_appearance,
            interaction(hovered, take(focus.refresh(accepted.hit()))), text, form_viewport)));
        feedback_pointer = std::move(candidate_pointer);
        require(accepted.styles[hovered_owner.index].border_color == hover_border &&
            accepted.styles[accepted.tree->find("section-account")->index].border_color == focus_border,
            "Ancestor hover and owner-only focus did not remain independent");
        verify_view();
        // A command rebuild clears the stationary pointer; no author-ID press migration.
        semantic("section-feature");
        require(take(feedback_pointer.refresh(accepted.hit())).pointers.empty(), "Branch replacement retained pointer handles");
        semantic("section-account");
    }
    const auto before_rejections = stable();
    const auto calls_before = entered;
    const auto invalid = route_scroll(accepted.hit(), Scroll{{16, 16}, {0, std::numeric_limits<float>::infinity()}});
    require(!invalid && invalid.diagnostics[0].code == "invalid_number", "Nonfinite wheel accepted");
    SettingsOperation forbidden([&](const CommandInvocationRecord&) { ++entered; return CommandOutcome{}; });
    const auto current_ui = accepted.semantic();
    require(pending && !executor.execute(accepted.commands, *pending, forbidden, &current_ui), "Pending scroll command survived replacement");
    const auto denied = take(stage(sections, offset, text, focus, false, false));
    const auto owner = *denied.tree->find("apply");
    require(!request_semantic_action(denied.semantic(), owner, "activate") &&
        !denied.commands.resolve_action(denied.semantic(), {"activate", "settings.apply", owner}, denied.commands.generation(), {},
            CommandSource::agent, ++sequence), "Disabled scrolled Apply resolved");
    FocusDispatcher denied_focus;
    require(!denied_focus.focus(denied.hit(), owner), "Disabled scrolled Apply focused");
    const auto disabled_box = bounds(denied, "apply").border_box;
    const Point disabled_point{disabled_box.origin.x + 1, disabled_box.origin.y + 1};
    PointerDispatcher denied_pointer;
    require(take(denied_pointer.dispatch(denied.hit(), {1us, PointerDown{{1}, disabled_point}})).actions.empty() &&
        take(denied_pointer.dispatch(denied.hit(), {2us, PointerUp{{1}, disabled_point}})).actions.empty(),
        "Disabled scrolled Apply clicked");
    if (styled) {
        const auto disabled_appearance = *denied.tree->find("appearance");
        require(!request_semantic_action(denied.semantic(), disabled_appearance, "activate") &&
            !denied.commands.resolve_action(denied.semantic(), {"activate", "settings.appearance", disabled_appearance},
                denied.commands.generation(), {}, CommandSource::agent, ++sequence) &&
            !denied_focus.focus(denied.hit(), disabled_appearance), "Disabled appearance resolved or focused");
        const auto box = bounds(denied, "appearance").border_box;
        const Point point{box.origin.x + 2, box.origin.y + 2};
        PointerDispatcher disabled_pointer;
        require(take(disabled_pointer.dispatch(denied.hit(), {1us, PointerDown{{1}, point}})).actions.empty() &&
            take(disabled_pointer.dispatch(denied.hit(), {2us, PointerUp{{1}, point}})).actions.empty(), "Disabled appearance clicked");
    }
    if (feedback) {
        auto check_disabled = [&](const Snapshot& snapshot, const std::string& id) {
            const auto owner = *snapshot.tree->find(id);
            const auto child = snapshot.tree->children(owner).front();
            const auto visuals = take(prepare_feedback(snapshot, accepted_appearance,
                {{child}, {owner}, owner}, text, form_viewport));
            require(visuals.styles[owner.index].border_color == disabled_border &&
                visuals.styles[owner.index].color == disabled_ink && visuals.styles[child.index].color == disabled_ink &&
                !request_semantic_action(snapshot.semantic(), owner, "activate"),
                "Disabled feedback failed suppression, inherited ink or semantic eligibility");
        };
        check_disabled(denied, "apply"); check_disabled(denied, "appearance");
        auto document = accepted.tree->document();
        document.root.properties["disabled"] = true;
        Snapshot ancestor{accepted.authored, accepted.commands, take(UiTree::create(document, actions)), {}, {}, {}, {}};
        ancestor.authored.document = document;
        ancestor.authored.sources.nodes[0].unmapped_properties.insert("disabled");
        accept_feedback(ancestor, take(prepare_feedback(ancestor, accepted_appearance, {}, text, form_viewport)));
        for (const auto& id : {"section-account", "section-feature", "section-about", "username", "apply", "appearance"}) {
            check_disabled(ancestor, id);
            const auto target = *ancestor.tree->find(id);
            FocusDispatcher disabled_focus;
            require(!disabled_focus.focus(ancestor.hit(), target) &&
                !ancestor.commands.resolve_action(ancestor.semantic(), {"activate", ancestor.tree->get(target)->events.at("activate"), target},
                    ancestor.commands.generation(), {}, CommandSource::agent, ++sequence),
                "Ancestor-disabled control acquired focus or resolved a command");
        }
    }
    require(stable() == before_rejections && entered == calls_before, "Invalid scroll/stale/disabled request changed accepted observations");
    const auto restored = take(stage(sections, offset, text, focus, false));
    require(restored.tree->document() == accepted.tree->document() && geometry(restored) == geometry(accepted) &&
        restored.paint == accepted.paint, "Rejected candidates left changed reactive observations");
    verify_view();
    std::cout << (feedback ? "Settings interaction feedback: " : styled ? "Settings reusable styles: " : "Settings scrolling: ")
        << "lower controls reachable, clamped host offsets, retained draft/save, staged rejection; "
        << observations << " accepted views match repeated CPU/replay\n";
}

void run_apply() {
    constexpr Size form_viewport{640, 360};
    ui::Session view;
    auto username = take(make_text_edit_state(1, "Ada"));
    SettingsData draft{username.text, false};
    SaveObservation saves;
    std::string result = "Not saved";
    const auto observed = view.state("username", username);
    const auto enabled = view.state("enabled", draft.feature);
    const auto saved = view.state("save-result", result);
    const auto disabled = view.state("apply-disabled", false);
    const auto caption = view.computed("summary", [observed, enabled] {
        return "User " + observed.read().text + (enabled.read() ? "; feature enabled" : "; feature disabled");
    });
    const auto form = Settings(enabled, caption, false,
        ui::TextInput("Username", observed, "settings.edit", {.node = {.id = "username", .source = form_source}}), {
            ui::Button("Apply", "settings.apply", {.node = {.id = "apply", .source = form_source}, .disabled = disabled}),
            ui::Text(saved, {.id = "save-result", .source = form_source})});
    const auto registry = take(CommandRegistry::create(
        {{"settings.toggle", "Toggle feature"}, {"settings.edit", "Edit username"}, {"settings.apply", "Apply settings"}},
        {{"settings.toggle", "settings.toggle"}, {"settings.edit", "settings.edit"}, {"settings.apply", "settings.apply"}}));
    std::uint64_t generation = 0, sequence = 0;
    auto commands = [&](bool allowed = true) {
        return take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
            {{"settings.toggle", {true}}, {"settings.edit", {true}}, {"settings.apply", {allowed}}}));
    };
    PlaceholderTextShaper text;
    auto accepted = take(prepare_candidate(view, form, commands(), text, form_viewport));
    view.publish();
    ReplayRecording recording;
    recording.context = actions; recording.viewport = form_viewport;
    recording.document = accepted.tree->document(); recording.styles = accepted.styles;
    recording.environment.scale = 1.25f; recording.environment.locale = "en";
    FixtureGlyphs raster; GlyphCache cache(raster); CpuFrameCapture frames(&cache);
    const auto extent = take(capture_extent(form_viewport, recording.environment.scale));
    std::vector<UiDrawList> paints;
    std::vector<std::vector<ReplayBox>> boxes;
    std::vector<CapturedImage> images;
    using Names = std::map<std::string, std::string>;
    std::vector<Names> names;
    auto observations = [](const Snapshot& snapshot) {
        Names values;
        for (const auto& node : snapshot.inspection.semantics.nodes)
            if (node.id) values.emplace(*node.id, node.name);
        return values;
    };
    auto remember = [&] {
        const auto values = observations(accepted);
        require(draft.username == username.text && values.at("summary") ==
            "User " + draft.username + (draft.feature ? "; feature enabled" : "; feature disabled") &&
            values.at("save-result") == result && values.at("apply") == "Apply" &&
            values.at("toggle") == (draft.feature ? "[x] Enable feature" : "[ ] Enable feature"),
            "Settings draft, saved result and inspection disagree");
        require(values.at("username") == "Username " + username.text + " Selection " +
            std::to_string(username.anchor) + ":" + std::to_string(username.caret), "Combined field projection differs");
        if (!saves.calls.empty()) require(result == saved_caption(static_cast<unsigned>(saves.calls.size()), saves.calls.back()),
            "Saved result must describe the owned host copy");
        const auto owner = *accepted.tree->find("save-result");
        require(accepted.inspection.elements[owner.index].source &&
            accepted.inspection.elements[owner.index].source->property_arguments.at("text") == form_source,
            "Save observation lost its source expression");
        paints.push_back(accepted.paint); boxes.push_back(geometry(accepted)); names.push_back(values);
        images.push_back(take(frames.capture({images.size(), form_viewport, recording.environment.scale,
            extent, &accepted.paint})));
    };
    remember();
    FocusDispatcher focus;
    CommandExecutor executor;
    std::optional<CommandInvocation> pending;
    std::optional<ActionRequest> old_owner;
    std::vector<CommandInvocationRecord> applied;
    unsigned entered = 0;
    auto stable = [&] { return std::tuple{draft, username, saves.calls, result, accepted.tree->document(),
        accepted.inspection.generation, accepted.commands.generation(), accepted.paint}; };
    auto operate = [&](const ActionRequest& action, CommandSource source,
        std::optional<TextEditRequest> request = {}, TextShaper* rejecting = nullptr) {
        const auto before = stable();
        const auto current_ui = accepted.semantic();
        const auto invocation = take(accepted.commands.resolve_action(current_ui, action,
            accepted.commands.generation(), {}, source, ++sequence));
        // The synchronous host request owns the edit envelope and copies its
        // accepted settings. Stage every observable before calling save once.
        SettingsOperation host([&, request = std::move(request)](const CommandInvocationRecord& call) -> CommandOutcome {
            ++entered;
            auto next_username = username;
            auto next_draft = draft;
            auto next_result = result;
            const bool apply = call.command == "settings.apply";
            if (call.command == "settings.edit") {
                require(request.has_value(), "Editing requires an owned request");
                auto edited = edit_text(username, *request);
                if (!edited) return {CommandOutcomeStatus::error, {}, std::move(edited.diagnostics)};
                next_username = std::move(*edited.value); next_draft.username = next_username.text;
            } else if (call.command == "settings.toggle") next_draft.feature = !next_draft.feature;
            else {
                require(apply, "Unexpected Settings command");
                next_result = saved_caption(static_cast<unsigned>(saves.calls.size()) + 1, next_draft);
            }
            auto restore = [&] { view.update([&] { observed.write(username); enabled.write(draft.feature); saved.write(result); }); };
            view.update([&] { observed.write(next_username); enabled.write(next_draft.feature); saved.write(next_result); });
            auto staged = [&] {
                try { return prepare_candidate(view, form, commands(), rejecting ? *rejecting : text, form_viewport); }
                catch (...) { restore(); throw; }
            }();
            if (!staged) { restore(); return {CommandOutcomeStatus::error, {}, std::move(staged.diagnostics)}; }
            pending = take(accepted.commands.resolve_action(current_ui, action,
                accepted.commands.generation(), {}, source, ++sequence));
            old_owner = action;
            if (apply) { saves.save(next_draft); applied.push_back(call); }
            username = std::move(next_username); draft = std::move(next_draft); result = std::move(next_result);
            view.publish(); accepted = std::move(*staged.value);
            return {};
        });
        const auto execution = take(executor.execute(accepted.commands, invocation, host, &current_ui));
        if (rejecting) {
            require(execution.outcome.status == CommandOutcomeStatus::error && !execution.outcome.diagnostics.empty() &&
                execution.outcome.diagnostics[0].source && execution.outcome.diagnostics[0].source->location == form_source &&
                before == stable(), "Rejected Apply changed settings/save/presentation or lost its source");
            // The failed request is consumed even though its presentation was retained.
            const auto duplicate = executor.execute(accepted.commands, invocation, host, &current_ui);
            require(!duplicate && duplicate.diagnostics[0].code == "consumed_sequence", "Failed Apply entered twice");
            return;
        }
        require(execution.outcome.status == CommandOutcomeStatus::success, "Combined settings command failed");
        const auto after = stable();
        const auto refreshed_ui = accepted.semantic();
        const auto duplicate = executor.execute(accepted.commands, invocation, host, &refreshed_ui);
        require(!duplicate && duplicate.diagnostics[0].code == "consumed_sequence" && stable() == after,
            "Accepted Settings command executed twice");
        recording.steps.push_back(ReplayReload{accepted.tree->document(), accepted.styles});
        remember();
    };
    auto logical = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
        const auto dispatched = take(focus.dispatch(accepted.hit(), event));
        recording.steps.push_back(event);
        for (const auto& action : dispatched.actions) operate(action, source);
    };
    auto insert = [&](InputEvent event) {
        require(validate(event).empty() && take(focus.refresh(accepted.hit())).focused == accepted.tree->find("username"),
            "Text insertion must target the accepted focused username");
        const auto& input = std::get<tessera::TextInput>(event.data);
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate")),
            CommandSource::keyboard, TextEditRequest{username.owner, username.revision, TextEditInsert{input.text}});
    };
    auto semantic_apply = [&] {
        recording.steps.push_back(ReplaySemanticAction{AuthorIdTarget{"apply"}});
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("apply"), "activate")), CommandSource::agent);
    };
    logical({1us, FocusNext{}}); logical({2us, FocusNext{}});
    insert({3us, tessera::TextInput{"本"}});
    require(draft == SettingsData{"Ada本", false} && saves.calls.empty(), "Editing saved prematurely");
    logical({4us, FocusPrevious{}}); logical({5us, Activate{}});
    logical({6us, FocusNext{}}); logical({7us, FocusNext{}});
    require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("apply"), "Apply focus order differs");
    logical({8us, Activate{}});
    require(saves.calls == std::vector<SettingsData>{{"Ada本", true}}, "Keyboard Apply payload differs");
    require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("apply"), "Apply replacement lost focus");
    logical({9us, Activate{}}, CommandSource::gamepad);
    const auto box = accepted.layout.boxes[accepted.tree->find("apply")->index].border_box;
    const Point point{box.origin.x + 1, box.origin.y + 1};
    PointerDispatcher pointer;
    const InputEvent down{10us, PointerDown{{1}, point}}, up{11us, PointerUp{{1}, point}};
    const auto before_press = stable();
    require(take(pointer.dispatch(accepted.hit(), down)).actions.empty() && stable() == before_press,
        "Pointer press saved before release");
    recording.steps.push_back(down);
    const auto click = take(pointer.dispatch(accepted.hit(), up)); recording.steps.push_back(up);
    require(click.actions.size() == 1, "Apply release must request one command");
    operate(click.actions[0], CommandSource::pointer);
    semantic_apply();
    require(saves.calls.size() == 4, "Four Apply routes did not save once each");
    for (const auto& payload : saves.calls) require(payload == SettingsData{"Ada本", true}, "Apply routes copied different settings");
    logical({12us, FocusPrevious{}}); insert({13us, tessera::TextInput{"!"}});
    logical({14us, FocusPrevious{}}); logical({15us, Activate{}});
    require(draft == SettingsData{"Ada本!", false} && saves.calls.back() == SettingsData{"Ada本", true} &&
        result == "Saved 4: Ada本; feature enabled", "Draft mutation changed the owned saved payload/result");
    logical({16us, FocusNext{}}); logical({17us, FocusNext{}});
    for (bool measure : {true, false}) {
        RejectingShaper rejecting(measure, saved_caption(5, draft));
        operate(take(request_semantic_action(accepted.semantic(), *accepted.tree->find("apply"), "activate")),
            CommandSource::agent, {}, &rejecting);
    }
    semantic_apply();
    require(saves.calls.size() == 5 && saves.calls.back() == draft && result == "Saved 5: Ada本!; feature disabled",
        "Retry did not save the edited draft once");
    const auto before_rejections = stable();
    SettingsOperation forbidden([&](const CommandInvocationRecord&) { ++entered; saves.save(draft); return CommandOutcome{}; });
    const auto entered_before = entered;
    const auto current_ui = accepted.semantic();
    require(pending && old_owner, "Missing pending Apply guards");
    const auto stale = executor.execute(accepted.commands, *pending, forbidden, &current_ui);
    require(!stale && stale.diagnostics[0].code != "consumed_sequence" &&
        !accepted.commands.resolve_action(current_ui, *old_owner, accepted.commands.generation(), {}, CommandSource::agent, ++sequence),
        "Stale Apply invocation or owner accepted");
    // Even equal command counters cannot make an invocation's tree owner live.
    const auto live_action = take(request_semantic_action(current_ui, *accepted.tree->find("apply"), "activate"));
    const auto live_invocation = take(accepted.commands.resolve_action(current_ui, live_action,
        accepted.commands.generation(), {}, CommandSource::agent, ++sequence));
    const auto replacement = take(prepare_candidate(view, form, accepted.commands, text, form_viewport));
    const auto replacement_ui = replacement.semantic();
    require(!executor.execute(replacement.commands, live_invocation, forbidden, &replacement_ui), "Foreign tree accepted equal counters");
    for (bool authored : {false, true}) {
        view.update([&] { disabled.write(authored); });
        const auto denied = take(prepare_candidate(view, form, commands(authored), text, form_viewport));
        const auto owner = *denied.tree->find("apply");
        const auto denied_ui = denied.semantic();
        require(!request_semantic_action(denied_ui, owner, "activate") &&
            !denied.commands.resolve_action(denied_ui, {"activate", "settings.apply", owner}, denied.commands.generation(), {},
                CommandSource::agent, ++sequence), "Disabled Apply resolved");
        require(!executor.execute(denied.commands, live_invocation, forbidden, &denied_ui), "Disabled execution entered host");
        for (const auto& node : denied.inspection.semantics.nodes) if (node.id == "apply")
            require(!node.enabled && !node.focusable && node.actions.empty(), "Disabled Apply semantics disagree");
        FocusDispatcher denied_focus;
        require(!denied_focus.focus(denied.hit(), owner), "Disabled Apply acquired focus");
        // Keyboard and gamepad host routes both normalize to Activate.
        require(take(denied_focus.dispatch(denied.hit(), {18us, Activate{}})).actions.empty(), "Disabled logical Apply activated");
        PointerDispatcher denied_pointer;
        require(take(denied_pointer.dispatch(denied.hit(), {19us, PointerDown{{2}, point}})).actions.empty() &&
            take(denied_pointer.dispatch(denied.hit(), {20us, PointerUp{{2}, point}})).actions.empty(), "Disabled pointer Apply activated");
    }
    view.update([&] { disabled.write(false); });
    auto malformed = username; malformed.caret = 4; // Inside 本 at bytes 3..6.
    view.update([&] { observed.write(malformed); });
    const auto invalid = prepare_candidate(view, form, commands(), text, form_viewport);
    require(!invalid && invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == form_source,
        "Invalid combined form lost located diagnostics");
    view.update([&] { observed.write(username); });
    require(stable() == before_rejections && entered == entered_before, "Rejected request changed host state or entered save");
    require(observations(take(prepare_candidate(view, form, commands(), text, form_viewport))) == names.back(),
        "Rejected candidate left stale reactive observations");
    recording.steps.push_back(ReplayTick{21us});
    const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
    require(first == repeated && first.frames.size() == paints.size() + 1 && first.output.actions.size() == 7,
        "Combined Apply CPU/replay or action count differs");
    for (std::size_t i = 0; i < paints.size(); ++i)
        require(first.output.generations[i].paint == paints[i] && first.output.generations[i].boxes == boxes[i] &&
            first.frames[i].image == images[i], "Accepted presentation/capture differs from Apply replay");
    for (const auto& observation : first.output.semantics) {
        Names values;
        for (const auto& node : observation.nodes) if (node.id) values.emplace(*node.id, node.name);
        require(values == names[std::min(observation.generation, names.size() - 1)], "Apply replay semantics differ");
    }
    const std::vector<std::string> expected_actions{"settings.toggle", "settings.apply", "settings.apply", "settings.apply",
        "settings.apply", "settings.toggle", "settings.apply"};
    for (std::size_t i = 0; i < expected_actions.size(); ++i)
        require(first.output.actions[i].action == expected_actions[i] && first.output.actions[i].id ==
            (expected_actions[i] == "settings.apply" ? "apply" : "toggle"), "Apply replay action target/order differs");
    const std::vector<CommandSource> sources{CommandSource::keyboard, CommandSource::gamepad, CommandSource::pointer,
        CommandSource::agent, CommandSource::agent};
    require(applied.size() == sources.size(), "Apply execution count differs from host saves");
    for (std::size_t i = 0; i < applied.size(); ++i)
        require(applied[i].command == "settings.apply" && applied[i].source == sources[i] && applied[i].owner &&
            applied[i].owner->action == "settings.apply", "Apply did not share a descriptor/executor/owner");
    require(images[2] != images[3] && images[3] != images[4], "Saved result changes are absent from CPU capture");
    std::cout << "Settings Apply: 5 owned host saves, shared keyboard/gamepad/pointer/semantic command, staged/disabled/stale rejection; "
        << first.frames.size() << " deterministic CPU/replay frames\n";
}
} // namespace

int main() {
    try {
        run_scrolling();
        run_scrolling(true);
        run_scrolling(true, true);
        run_sections();
        run_apply();
        run_username();
        ui::Session view;
        const auto enabled = view.state("enabled", false);
        const auto disabled = view.state("disabled", false);
        const auto caption = view.computed("summary", [enabled] {
            return enabled.read() ? std::string("Feature enabled") : std::string("Feature disabled");
        });
        const auto form = Settings(enabled, caption, disabled);
        SettingsHost host(view, enabled);
        const auto registry = take(CommandRegistry::create({{"settings.toggle", "Toggle feature"}},
            {{"settings.toggle", "settings.toggle"}}));
        std::uint64_t generation = 0, sequence = 0;
        auto commands = [&](bool allowed = true) {
            return take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
                {{"settings.toggle", {allowed}}}));
        };
        PlaceholderTextShaper text;
        auto accepted = prepare(view, form, commands(), text);
        verify(accepted, false);
        view.publish();
        ReplayRecording recording;
        recording.context = actions;
        recording.viewport = viewport;
        recording.document = accepted.tree->document();
        recording.styles = accepted.styles;
        recording.environment.scale = 1.25f;
        recording.environment.locale = "en";
        std::vector<UiDrawList> expected_paint{accepted.paint};
        std::vector<std::vector<ReplayBox>> expected_geometry{geometry(accepted)};

        CommandExecutor executor;
        FocusDispatcher focus;
        std::optional<CommandInvocation> retained;
        auto operate = [&](const ActionRequest& action, CommandSource source) {
            const auto current_ui = accepted.semantic();
            auto invocation = take(accepted.commands.resolve_action(current_ui, action,
                accepted.commands.generation(), {}, source, ++sequence));
            require(take(executor.execute(accepted.commands, invocation, host, &current_ui)).outcome.status ==
                CommandOutcomeStatus::success, "Host command failed");
            // Keep a fresh unexecuted request to check stale ownership after replacement.
            retained = take(accepted.commands.resolve_action(current_ui, action,
                accepted.commands.generation(), {}, source, ++sequence));
            auto candidate = prepare(view, form, commands(), text);
            verify(candidate, host.feature);
            view.publish();
            accepted = std::move(candidate);
            recording.steps.push_back(ReplayReload{accepted.tree->document(), accepted.styles});
            expected_paint.push_back(accepted.paint);
            expected_geometry.push_back(geometry(accepted));
        };
        auto logical = [&](InputEvent event, CommandSource source) {
            auto dispatch = take(focus.dispatch(accepted.hit(), event));
            recording.steps.push_back(event);
            for (const auto& action : dispatch.actions) operate(action, source);
        };
        logical({1us, FocusNext{}}, CommandSource::keyboard);
        logical({2us, Activate{}}, CommandSource::keyboard);
        require(host.feature, "Keyboard command did not change host state");
        require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("toggle"),
            "Checkbox author identity must restore keyboard/gamepad focus");
        logical({3us, Activate{}}, CommandSource::gamepad);
        require(!host.feature, "Gamepad logical activation did not change host state");

        const auto target = *accepted.tree->find("toggle");
        recording.steps.push_back(ReplaySemanticAction{AuthorIdTarget{"toggle"}});
        operate(take(request_semantic_action(accepted.semantic(), target, "activate")), CommandSource::agent);
        require(host.feature, "Semantic command did not change host state");

        const auto button = *accepted.tree->find("toggle");
        const auto box = accepted.inspection.elements[button.index].geometry->border_box;
        const Point point{box.origin.x + 2, box.origin.y + 2};
        PointerDispatcher pointer;
        const InputEvent down{4us, PointerDown{{1}, point}};
        const InputEvent up{5us, PointerUp{{1}, point}};
        const auto press = take(pointer.dispatch(accepted.hit(), down)); recording.steps.push_back(down);
        require(press.actions.empty() && host.feature && host.executions == 3,
            "Pointer press must not activate a controlled Checkbox");
        const auto click = take(pointer.dispatch(accepted.hit(), up)); recording.steps.push_back(up);
        require(click.actions.size() == 1, "Pointer must request exactly one activation");
        operate(click.actions[0], CommandSource::pointer);
        require(!host.feature && host.executions == 4, "Command order or host state differs");

        // Rejection leaves the accepted document, generation and image inputs owned.
        const auto accepted_document = accepted.tree->document();
        const auto accepted_generation = accepted.inspection.generation;
        const auto invalid = view.lower(ui::Text("bad", {.style = {.text = {.size = 0.f}}, .source = form_source}));
        require(!invalid && invalid.diagnostics[0].source &&
            invalid.diagnostics[0].source->location == form_source && accepted.tree->document() == accepted_document &&
            accepted.inspection.generation == accepted_generation, "Invalid candidate replaced accepted presentation");
        auto reject_disabled = [&](const Snapshot& snapshot) {
            const auto owner = *snapshot.tree->find("toggle");
            const auto ui = snapshot.semantic();
            const auto semantics = take(build_semantic_tree(ui));
            for (const auto& node : semantics.nodes) if (node.id == "toggle")
                require(!node.enabled && !node.focusable && node.actions.empty(), "Disabled Checkbox eligibility differs");
            require(!request_semantic_action(ui, owner, "activate"), "Disabled semantic activation accepted");
            require(!snapshot.commands.resolve_action(ui, {"activate", "settings.toggle", owner},
                snapshot.commands.generation(), {}, CommandSource::agent, ++sequence),
                "Disabled direct command request accepted");
            FocusDispatcher denied_focus;
            require(!take(denied_focus.dispatch(snapshot.hit(), {6us, FocusNext{}})).focused &&
                take(denied_focus.dispatch(snapshot.hit(), {7us, Activate{}})).actions.empty(),
                "Disabled Checkbox received logical focus/activation");
            PointerDispatcher denied_pointer;
            require(take(denied_pointer.dispatch(snapshot.hit(), {8us, PointerDown{{2}, point}})).actions.empty() &&
                take(denied_pointer.dispatch(snapshot.hit(), {9us, PointerUp{{2}, point}})).actions.empty(),
                "Disabled Checkbox received pointer activation");
            require(!host.feature && host.executions == 4, "Disabled request changed host state");
        };
        const auto command_disabled = prepare(view, form, commands(false), text);
        reject_disabled(command_disabled);
        view.update([&] { disabled.write(true); });
        const auto authored_disabled = prepare(view, form, commands(), text);
        reject_disabled(authored_disabled);
        view.update([&] { disabled.write(false); });
        require(!accepted.commands.resolve_action(accepted.semantic(), {"activate", "settings.toggle", target},
            accepted.commands.generation(), {}, CommandSource::agent, ++sequence), "Stale owner accepted");
        const auto current_ui = accepted.semantic();
        require(retained.has_value(), "Missing pending invocation fixture");
        const auto stale = executor.execute(accepted.commands, *retained, host, &current_ui);
        require(!stale && stale.diagnostics.front().code != "consumed_sequence" &&
            !host.feature && host.executions == 4, "Stale retained invocation entered the host");

        recording.steps.push_back(ReplayTick{10us});
        FixtureGlyphs raster;
        GlyphCache cache(raster);
        CpuFrameCapture frames(&cache);
        const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        require(first == repeated && first.output.actions.size() == host.executions,
            "Repeated replay/capture or command action order differs");
        require(first.output.generations.size() == expected_paint.size() + 1,
            "Replay must contain initial, command reloads and final tick generations");
        for (std::size_t i = 0; i < expected_paint.size(); ++i) {
            require(first.output.generations[i].paint == expected_paint[i], "Replay paint differs from accepted full-tree paint");
            require(first.output.generations[i].boxes == expected_geometry[i], "Replay geometry differs from accepted full-tree layout");
        }
        for (const auto& observation : first.output.semantics) {
            bool found = false;
            bool found_check = false;
            const bool enabled_generation = observation.generation == 1 || observation.generation == 3;
            for (const auto& node : observation.nodes) if (node.id == "toggle") {
                found_check = true;
                require(node.name == (enabled_generation ? "[x] Enable feature" : "[ ] Enable feature"),
                    "Replay checked observation differs from accepted host state");
            }
            for (const auto& node : observation.nodes) if (node.id == "summary") {
                found = true;
                const auto expected = enabled_generation ? "Feature enabled" : "Feature disabled";
                require(node.name == expected, "Replay semantic summary differs from accepted host state");
            }
            require(found && found_check, "Replay omitted summary or checked semantics");
        }
        const std::vector<std::size_t> action_steps{1, 3, 5, 8};
        for (std::size_t i = 0; i < first.output.actions.size(); ++i)
            require(first.output.actions[i].action == "settings.toggle" && first.output.actions[i].id == "toggle" &&
                first.output.actions[i].step == action_steps[i], "Replay action target/order differs");
        require(first.frames.front().image != first.frames[1].image &&
            first.frames.front().image == first.frames[2].image &&
            first.frames.front().image == first.frames[4].image, "CPU pixels do not follow host setting updates");
        std::cout << "Settings Checkbox: 4 host commands, coherent checked value/summary, focus, disabled/stale rejection; "
            << first.frames.size() << " deterministic CPU/replay frames (placeholder text, synthetic glyphs)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
