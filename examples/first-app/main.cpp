#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <stdexcept>

using namespace tessera;
namespace ui = tessera::authoring;
using namespace std::chrono_literals;

namespace {
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message;
        for (const auto& error : result.diagnostics)
            message += error.code + " at " + error.path + ": " + error.message + "\n";
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Application composition: ordinary typed props, defaults and ordered children.
struct FieldProps { std::string label; float gap = 12; };
ui::Element Field(FieldProps props, std::vector<ui::Element> children) {
    return ui::Row({ui::Text(std::move(props.label)), ui::Column(std::move(children))},
        {.style = {.gap = props.gap}});
}
ui::Element FirstApp(ui::Value<bool> enabled, ui::Value<std::string> summary) {
    return ui::Column({
        ui::Text("My first app"),
        Field({"Feature"}, {
            ui::Checkbox("Enable feature", enabled, "app.toggle", {.node = {
                .id = "toggle", .style = {.padding = Edges{4, 8, 4, 8},
                    .background = Color{0.8f, 0.9f, 1, 1}}}})}),
        ui::Text(summary, {.id = "summary"})},
        {.id = "app", .style = {.padding = Edges{12, 12, 12, 12}, .gap = 12.f,
            .background = Color{0.95f, 0.95f, 0.95f, 1}}});
}

// Host presentation: all borrowed inputs are owned here until replacement.
constexpr Size viewport{480, 180};
const ValidationContext actions{{"app.toggle"}};
struct Presentation {
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
};
Presentation prepare(ui::Session& session, const ui::Element& form,
    CommandSnapshot commands, TextShaper& text) {
    auto authored = take(session.lower(form, actions));
    auto document = take(commands.apply_eligibility(authored.document, actions));
    auto tree = take(UiTree::create(document, actions));
    const StyleSheet sheet;
    auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
    auto layout = take(compute_layout({tree.get(), styles, viewport, &text}));
    auto inspection = take(capture_inspection({tree.get(), styles, &layout, &authored.sources}));
    auto paint = take(build_paint_list({tree.get(), styles, &layout, &text}));
    return {std::move(commands), std::move(tree), std::move(styles), std::move(layout),
        std::move(inspection), std::move(paint)};
}

// Application state is host-owned. Execution proposes an in-memory change;
// the caller accepts it only after preparing the replacement presentation.
struct Host final : CommandHost {
    bool enabled = false;
    bool candidate = false;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        require(invocation.command == "app.toggle", "Unexpected command");
        candidate = !enabled;
        return {};
    }
};

// Deliberately synthetic marks for placeholder glyph IDs, not a real font.
// The rasterizer outlives the cache, which outlives the capture adapter.
struct SyntheticGlyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};
std::string summary_name(const Presentation& presentation) {
    const auto target = take(resolve_target(presentation.inspection, AuthorIdTarget{"summary"}));
    const auto semantic = presentation.inspection.elements[target.index].semantic;
    require(semantic.has_value(), "Summary has no semantic observation");
    return presentation.inspection.semantics.nodes[*semantic].name;
}
} // namespace

int main() {
    try {
        // One session lifetime; these weak state/derived handles expire with it.
        ui::Session session;
        Host host;
        const auto enabled = session.state("enabled", host.enabled);
        const auto summary = session.computed("summary", [enabled] {
            return enabled.read() ? std::string("Feature enabled") : std::string("Feature disabled");
        });
        const auto form = FirstApp(enabled, summary);
        const auto registry = take(CommandRegistry::create({{"app.toggle", "Toggle feature"}},
            {{"app.toggle", "app.toggle"}}));
        std::uint64_t generation = 0, sequence = 0;
        auto commands = [&] {
            return take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
                {{"app.toggle", {true}}}));
        };
        PlaceholderTextShaper text;
        auto accepted = prepare(session, form, commands(), text);
        session.publish();

        SyntheticGlyphs raster;
        GlyphCache cache(raster);
        CpuFrameCapture frames(&cache);
        const auto extent = take(capture_extent(viewport, 1));
        auto capture = [&] {
            return take(frames.capture({0, viewport, 1, extent, &accepted.paint, 0us}));
        };
        const auto initial = capture();
        require(summary_name(accepted) == "Feature disabled", "Initial summary differs");
        std::cout << summary_name(accepted) << '\n';

        CommandExecutor executor;
        FocusDispatcher focus;
        // A native host maps Tab/D-pad and Enter/A to logical input here.
        // This window-free example supplies those normalized events itself.
        auto input = [&](InputEvent event, CommandSource source) {
            const auto dispatched = take(focus.dispatch(accepted.hit(), event));
            for (const auto& action : dispatched.actions) {
                const auto current = accepted.semantic();
                const auto invocation = take(accepted.commands.resolve_action(current, action,
                    accepted.commands.generation(), {}, source, ++sequence));
                const auto result = take(executor.execute(accepted.commands, invocation, host, &current));
                require(result.outcome.status == CommandOutcomeStatus::success, "Command failed");
                session.update([&] { enabled.write(host.candidate); });
                // Keep the accepted state/snapshot if a presentation stage fails.
                // This bounded restoration is not a general transaction system.
                auto candidate = [&] {
                    try { return prepare(session, form, commands(), text); }
                    catch (...) {
                        session.update([&] { enabled.write(host.enabled); });
                        throw;
                    }
                }();
                session.publish();
                host.enabled = host.candidate;
                accepted = std::move(candidate);
                // Replacement handles belong to a new tree; recover by author ID.
                (void)take(focus.refresh(accepted.hit()));
            }
        };
        input({1us, FocusNext{}}, CommandSource::keyboard);
        input({2us, Activate{}}, CommandSource::keyboard);
        require(host.enabled && summary_name(accepted) == "Feature enabled", "Keyboard toggle differs");
        const auto changed = capture();
        require(changed != initial && capture() == changed, "CPU capture differs");
        std::cout << summary_name(accepted) << '\n';
        input({3us, Activate{}}, CommandSource::gamepad);
        require(!host.enabled && sequence == 2 && summary_name(accepted) == "Feature disabled",
            "Gamepad toggle or focus recovery differs");
        require(capture() == initial, "Restored CPU image differs");
        std::cout << summary_name(accepted) << '\n'
            << "2 host commands; " << extent.width << 'x' << extent.height
            << " RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
