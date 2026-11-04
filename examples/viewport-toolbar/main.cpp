#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include "../fixtures.hpp"
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
ui::Element Viewport(ui::Value<std::string> zoom) {
    return ui::Stack({
        ui::Button("Select scene", "scene.select", {.node = {.id = "scene",
            .style = {.background = Color{0.12f, 0.18f, 0.25f, 1}}}}),
        ui::Text(zoom, {.id = "zoom", .style = {.margin = Edges{8, 8, 8, 8},
            .color = Color{1, 1, 1, 1}, .stack_horizontal = Align::center, .stack_vertical = Align::end}}),
        ui::Row({
            ui::Button("Zoom in", "view.zoom", {.node = {.id = "zoom-in",
                .style = {.padding = Edges{4, 8, 4, 8}, .background = Color{0.7f, 0.8f, 1, 1}}}}),
            ui::Button("Reset", "view.reset", {.node = {.id = "reset",
                .style = {.padding = Edges{4, 8, 4, 8}, .background = Color{0.7f, 0.8f, 1, 1}}}})},
            {.id = "toolbar", .style = {.margin = Edges{8, 8, 8, 8}, .gap = 4.f,
                .stack_horizontal = Align::end, .stack_vertical = Align::start}})},
        {.id = "viewport", .style = {.overflow = Overflow::clip}});
}
const ValidationContext actions{{"scene.select", "view.zoom", "view.reset"}};
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    Rect bounds(const char* id) const {
        const auto node = tree->find(id);
        require(node.has_value(), "Missing application control");
        for (const auto& box : layout.boxes) if (box.node == *node) return box.border_box;
        throw std::runtime_error("Application control has no layout");
    }
};
View prepare(ui::Session& session, const ui::Element& form, Size viewport, TextShaper& text) {
    auto authored = take(session.lower(form, actions));
    auto tree = take(UiTree::create(authored.document, actions));
    const StyleSheet sheet;
    auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
    auto layout = take(compute_layout({tree.get(), styles, viewport, &text}));
    return {std::move(tree), std::move(styles), std::move(layout)};
}
struct Host final : CommandHost {
    int zoom = 100;
    int selected = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "view.zoom") zoom += 25;
        else if (invocation.command == "view.reset") zoom = 100;
        else if (invocation.command == "scene.select") ++selected;
        else throw std::runtime_error("Unexpected viewport command");
        return {};
    }
};
Point center(Rect box) {
    return {box.origin.x + box.size.width / 2, box.origin.y + box.size.height / 2};
}
} // namespace

int tessera::examples::run_viewport_toolbar(const FrameObserver& observer) {
    try {
        ui::Session session;
        Host host;
        const auto zoom = session.state("zoom", host.zoom);
        const auto caption = session.computed("zoom-caption", [zoom] {
            return "Zoom " + std::to_string(zoom.read()) + "%";
        });
        const auto form = Viewport(caption);
        const auto registry = take(CommandRegistry::create({
            {"scene.select", "Select scene"}, {"view.zoom", "Zoom in"}, {"view.reset", "Reset view"}},
            {{"scene.select", "scene.select"}, {"view.zoom", "view.zoom"}, {"view.reset", "view.reset"}}));
        const auto commands = take(CommandSnapshot::publish(registry, 1, CommandMode::production,
            {{"scene.select", {true}}, {"view.zoom", {true}}, {"view.reset", {true}}}));
        PlaceholderTextShaper text;
        Size viewport{320, 180};
        auto view = prepare(session, form, viewport, text);
        session.publish();
        PointerDispatcher pointer;
        FocusDispatcher focus;
        CommandExecutor executor;
        std::uint64_t sequence = 0;
        auto execute = [&](const std::vector<ActionRequest>& requests, CommandSource source) {
            for (const auto& request : requests) {
                const auto semantic = view.semantic();
                const auto invocation = take(commands.resolve_action(semantic, request,
                    commands.generation(), {}, source, ++sequence));
                const auto result = take(executor.execute(commands, invocation, host, &semantic));
                require(result.outcome.status == CommandOutcomeStatus::success, "Viewport command failed");
            }
        };
        auto refresh = [&] {
            session.update([&] { zoom.write(host.zoom); });
            auto candidate = prepare(session, form, viewport, text);
            session.publish();
            view = std::move(candidate);
            (void)take(pointer.refresh(view.hit()));
            (void)take(focus.refresh(view.hit()));
        };
        auto capture = [&](std::string_view name) {
            if (observer) observer(name, viewport,
                take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text})));
        };
        auto click = [&](Point point, std::chrono::microseconds time) {
            (void)take(pointer.dispatch(view.hit(), {time, PointerDown{{1}, point}}));
            const auto result = take(pointer.dispatch(view.hit(), {time + 1us, PointerUp{{1}, point}}));
            require(result.actions.size() == 1, "Click must request exactly one action");
            execute(result.actions, CommandSource::pointer);
            refresh();
        };
        capture("stack-initial");
        click(center(view.bounds("zoom-in")), 1us);
        require(host.zoom == 125 && host.selected == 0, "Toolbar click leaked to the scene beneath it");
        capture("stack-zoomed");

        (void)take(focus.dispatch(view.hit(), {3us, FocusNext{}}));
        auto focused = take(focus.dispatch(view.hit(), {4us, FocusNext{}}));
        require(focused.focused == view.tree->find("zoom-in"), "Tab must reach the overlaid toolbar");
        execute(take(focus.dispatch(view.hit(), {5us, Activate{}})).actions, CommandSource::keyboard);
        refresh();
        require(host.zoom == 150 && host.selected == 0, "Keyboard toolbar action differs");
        focused = take(focus.dispatch(view.hit(), {6us, Navigate{Direction::right}}));
        require(focused.focused == view.tree->find("reset"), "Directional navigation must reach Reset");
        execute(take(focus.dispatch(view.hit(), {7us, Activate{}})).actions, CommandSource::gamepad);
        refresh();
        require(host.zoom == 100 && host.selected == 0, "Gamepad Reset differs");

        viewport = {240, 140};
        refresh();
        const auto toolbar = view.bounds("toolbar");
        require(toolbar.origin.x + toolbar.size.width == viewport.width - 8 && toolbar.origin.y == 8,
            "Viewport resize must preserve the toolbar's top/end alignment");
        require(view.tree->get(*view.tree->find("zoom"))->properties.at("text") == Property{std::string("Zoom 100%")},
            "Host zoom must appear through the ordinary reactive caption");
        capture("stack-resized");
        click({12, 100}, 8us);
        require(host.selected == 1 && host.zoom == 100, "Uncovered scene must remain interactive");
        std::cout << "Viewport toolbar: pointer zoom, keyboard zoom, gamepad reset, resize and scene selection passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_viewport_toolbar({}); }
#endif
