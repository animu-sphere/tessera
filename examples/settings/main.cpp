#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <charconv>
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
const ValidationContext actions{{"settings.toggle", "settings.edit"}};
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
    std::optional<ui::Element> username = {}) {
    std::vector<ui::Element> rows{
        ui::Text("Settings", {.id = "heading"}),
        SettingsRow({"Feature", "feature-row"}, {
            ui::Checkbox("Enable feature", checked, "settings.toggle", {.node = {
                .id = "toggle", .style = {.padding = Edges{6, 8, 6, 8},
                    .background = Color{0.2f, 0.35f, 0.6f, 1}}, .source = form_source}, .disabled = disabled})}),
        SettingsRow({"Status", "status-row"}, {
            ui::Text(summary, {.id = "summary", .source = form_source})})};
    if (username) rows.insert(rows.begin() + 2, SettingsRow({"Account", "username-row"}, {*username}));
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
Result<Snapshot> prepare_candidate(ui::Session& view, const ui::Element& form, CommandSnapshot commands, TextShaper& text) {
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
    auto layout = compute_layout({tree.get(), *styles.value, viewport, &text});
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
    explicit RejectingShaper(bool measure) : layout(measure) {}
    Result<TextMetrics> measure(std::string_view value, const TextStyle& style, const TextConstraints& constraints) override {
        if (layout && value == "rejected") return {std::nullopt,
            {{"fixture_measure", Severity::error, "", "Injected measurement rejection."}}};
        return fallback.measure(value, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view value, const TextStyle& style, const TextConstraints& constraints) override {
        if (!layout && value == "rejected") return {std::nullopt,
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
} // namespace

int main() {
    try {
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
