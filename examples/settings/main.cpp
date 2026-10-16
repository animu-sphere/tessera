#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
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
const ValidationContext actions{{"settings.toggle"}};
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
ui::Element Settings(ui::Value<std::string> summary) {
    return ui::Column({
        ui::Text("Settings", {.id = "heading"}),
        SettingsRow({"Feature", "feature-row"}, {
            ui::Text(summary, {.id = "summary", .source = form_source})}),
        SettingsRow({"Action", "action-row"}, {
            ui::Button("Toggle feature", "settings.toggle", {.node = {
                .id = "toggle", .style = {.padding = Edges{6, 8, 6, 8},
                    .background = Color{0.2f, 0.35f, 0.6f, 1}}, .source = form_source}})})},
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
Snapshot prepare(ui::Session& view, const ui::Element& form, CommandSnapshot commands, TextShaper& text) {
    auto authored = take(view.lower(form, actions));
    auto eligible = take(commands.apply_eligibility(authored.document, actions));
    auto tree = take(UiTree::create(eligible, actions));
    const StyleSheet sheet;
    auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
    auto layout = take(compute_layout({tree.get(), styles, viewport, &text}));
    auto inspection = take(capture_inspection({tree.get(), styles, &layout, &authored.sources}));
    auto paint = take(build_paint_list({tree.get(), styles, &layout, &text}));
    return {std::move(authored), std::move(commands), std::move(tree), std::move(styles),
        std::move(layout), std::move(inspection), std::move(paint)};
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
} // namespace

int main() {
    try {
        ui::Session view;
        const auto enabled = view.state("enabled", false);
        const auto caption = view.computed("summary", [enabled] {
            return enabled.read() ? std::string("Feature enabled") : std::string("Feature disabled");
        });
        const auto form = Settings(caption);
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
        auto operate = [&](const ActionRequest& action, CommandSource source) {
            const auto current_ui = accepted.semantic();
            auto invocation = take(accepted.commands.resolve_action(current_ui, action,
                accepted.commands.generation(), {}, source, ++sequence));
            require(take(executor.execute(accepted.commands, invocation, host, &current_ui)).outcome.status ==
                CommandOutcomeStatus::success, "Host command failed");
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
        (void)take(focus.refresh(accepted.hit()));
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
        (void)take(pointer.dispatch(accepted.hit(), down)); recording.steps.push_back(down);
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
        const auto disabled = prepare(view, form, commands(false), text);
        const auto denied = request_semantic_action(disabled.semantic(), *disabled.tree->find("toggle"), "activate");
        require(!denied && host.executions == 4, "Command eligibility must disable ordinary activation");
        require(!accepted.commands.resolve_action(accepted.semantic(), {"activate", "settings.toggle", target},
            accepted.commands.generation(), {}, CommandSource::agent, ++sequence), "Stale owner accepted");

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
            for (const auto& node : observation.nodes) if (node.id == "summary") {
                found = true;
                const bool enabled_generation = observation.generation == 1 || observation.generation == 3;
                const auto expected = enabled_generation ? "Feature enabled" : "Feature disabled";
                require(node.name == expected, "Replay semantic summary differs from accepted host state");
            }
            require(found, "Replay omitted summary semantics");
        }
        const std::vector<std::size_t> action_steps{1, 3, 5, 8};
        for (std::size_t i = 0; i < first.output.actions.size(); ++i)
            require(first.output.actions[i].action == "settings.toggle" && first.output.actions[i].id == "toggle" &&
                first.output.actions[i].step == action_steps[i], "Replay action target/order differs");
        require(first.frames.front().image != first.frames[1].image &&
            first.frames.front().image == first.frames[2].image &&
            first.frames.front().image == first.frames[4].image, "CPU pixels do not follow host setting updates");
        std::cout << "Settings: 4 host commands, coherent derived summary, source inspection, candidate rejection; "
            << first.frames.size() << " deterministic CPU/replay frames (placeholder text, synthetic glyphs)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
