#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <charconv>
#include <functional>
#include <tuple>
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
const ValidationContext actions{{"settings.toggle", "settings.edit", "settings.apply"}};
constexpr Size viewport{480, 240};
// Opaque fixture expression identity; the runtime opens no source file.
const SourceLocation form_source{"fixture://settings-form", {0, 20}};

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
    Size logical_viewport = viewport) {
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
    auto styles = resolve_styles({tree.get(), &sheet, {}, authored.styles});
    if (!styles) return fail(std::move(styles.diagnostics));
    auto layout = compute_layout({tree.get(), *styles.value, logical_viewport, &text});
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
