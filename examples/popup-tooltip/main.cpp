#include <tessera/ui/authoring.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/render/paint.hpp>
#include "../fixtures.hpp"
#include <iostream>
#include <stdexcept>

using namespace tessera;
using namespace std::chrono_literals;
namespace ui = tessera::authoring;

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
const ValidationContext actions{{"popup.toggle", "select.dark", "select.light", "scene.select"}};
ui::Element Form(bool open, bool help, bool anchor, const std::string& selected) {
    std::vector<ui::Element> content;
    if (anchor) content.push_back(ui::Button("Theme: " + selected, "popup.toggle",
        {.node = {.id = "theme", .style = {.height = Dimension::points(28), .margin = Edges{32, 0, 0, 0},
            .background = Color{0.7f, 0.8f, 1, 1}}}}));
    content.push_back(ui::Column({}, {.style = {.height = Dimension::points(180)}}));
    return ui::Column({
        ui::ScrollView(std::move(content), {.id = "settings", .style = {
            .width = Dimension::points(180), .height = Dimension::points(72)}}),
        ui::Button("Select scene", "scene.select", {.node = {.id = "scene",
            .style = {.height = Dimension::points(100), .background = Color{0.2f, 0.3f, 0.4f, 1}}}}),
        ui::Popup("theme", {
            ui::Button("Dark", "select.dark", {.node = {.id = "dark", .style = {.height = Dimension::points(28)}}}),
            ui::Button("Light", "select.light", {.node = {.id = "light", .style = {.height = Dimension::points(28)}}})},
            {.node = {.id = "theme-popup", .style = {.width = Dimension::points(160),
                .padding = Edges{4, 4, 4, 4}, .background = Color{0.8f, 0.85f, 1, 1}}},
             .open = open}),
        ui::Tooltip("theme", "Choose the editor color theme",
            {.node = {.id = "theme-help", .style = {.max_width = 180.f,
                .padding = Edges{4, 4, 4, 4}, .background = Color{1, 1, 0.7f, 1}}}, .open = help})},
        {.id = "root", .style = {.overflow = Overflow::clip}});
}
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    std::optional<Rect> bounds(const char* id) const {
        const auto node = tree->find(id);
        for (const auto& box : layout.boxes) if (node == box.node) return box.border_box;
        return {};
    }
};
struct Host final : CommandHost {
    bool open = false;
    bool help = false;
    bool anchor = true;
    std::string selected = "Dark";
    int scene_selections = 0;
    int selections = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "popup.toggle") open = !open;
        else if (invocation.command == "select.dark") { selected = "Dark"; open = false; ++selections; }
        else if (invocation.command == "select.light") { selected = "Light"; open = false; ++selections; }
        else if (invocation.command == "scene.select") ++scene_selections;
        else throw std::runtime_error("Unexpected popup command");
        return {};
    }
};
Point center(Rect box) { return {box.origin.x + box.size.width / 2, box.origin.y + box.size.height / 2}; }
} // namespace

int tessera::examples::run_popup_tooltip(const FrameObserver& observer) {
    try {
        Host host;
        ui::Session session;
        PlaceholderTextShaper text;
        PopupDispatcher popup("theme-popup");
        TooltipDispatcher tooltip("theme");
        PointerDispatcher pointer;
        FocusDispatcher focus;
        CommandExecutor executor;
        std::optional<NodeHandle> focused;
        Size viewport{240, 180};
        float scroll_y = 0;
        View view;
        const auto registry = take(CommandRegistry::create({
            {"popup.toggle", "Choose theme"}, {"select.dark", "Dark"}, {"select.light", "Light"}, {"scene.select", "Select scene"}},
            {{"popup.toggle", "popup.toggle"}, {"select.dark", "select.dark"},
             {"select.light", "select.light"}, {"scene.select", "scene.select"}}));
        const auto commands = take(CommandSnapshot::publish(registry, 1, CommandMode::production,
            {{"popup.toggle", {true}}, {"select.dark", {true}}, {"select.light", {true}}, {"scene.select", {true}}}));
        std::uint64_t sequence = 0;
        auto refresh = [&] {
            for (int attempt = 0; attempt < 4; ++attempt) {
                auto authored = take(session.lower(Form(host.open, host.help, host.anchor, host.selected), actions));
                auto tree = take(UiTree::create(authored.document, actions));
                const StyleSheet sheet;
                auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
                std::vector<Point> offsets(tree->size());
                offsets[tree->find("settings")->index] = {0, scroll_y};
                auto layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
                view = {std::move(tree), std::move(styles), std::move(layout)};
                (void)take(pointer.refresh(view.hit()));
                focused = take(focus.refresh(view.hit())).focused;
                const auto transition = take(popup.refresh(view.hit()));
                if (transition.focus) focused = take(focus.focus(view.hit(), *transition.focus)).focused;
                const bool open = host.open && !transition.dismiss;
                const bool help = take(tooltip.refresh(view.hit(), focused)).visible && !open;
                if (open == host.open && help == host.help) {
                    (void)take(build_semantic_tree({view.tree.get(), view.styles, &view.layout, focused}));
                    (void)take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
                    session.publish();
                    return;
                }
                host.open = open;
                host.help = help;
            }
            throw std::runtime_error("Popup presentation did not settle");
        };
        auto capture = [&](std::string_view name) {
            if (observer) observer(name, viewport,
                take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text})));
        };
        auto execute = [&](const std::vector<ActionRequest>& requests, CommandSource source) {
            for (const auto& request : requests) {
                const auto semantic = view.semantic();
                const auto invocation = take(commands.resolve_action(semantic, request,
                    commands.generation(), {}, source, ++sequence));
                const auto result = take(executor.execute(commands, invocation, host, &semantic));
                require(result.outcome.status == CommandOutcomeStatus::success, "Popup command failed");
            }
        };
        auto dispatch = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
            const auto transition = take(popup.dispatch(view.hit(), event));
            host.help = take(tooltip.dispatch(view.hit(), event, focused)).visible && !host.open;
            if (transition.focus) focused = take(focus.focus(view.hit(), *transition.focus)).focused;
            if (transition.consumed || transition.dismiss) {
                host.open = false;
                pointer.reset();
            } else if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
                std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
                const auto result = take(pointer.dispatch(view.hit(), event));
                if (std::holds_alternative<PointerDown>(event.data))
                    for (const auto& state : result.pointers)
                        if (state.pressed) focused = take(focus.focus(view.hit(), *state.pressed)).focused;
                execute(result.actions, CommandSource::pointer);
            } else {
                const auto result = take(focus.dispatch(view.hit(), event));
                focused = result.focused;
                execute(result.actions, source);
            }
            // Preserve a pointer press until its release; recompose only on changes.
            if ((!std::holds_alternative<PointerDown>(event.data) || transition.dismiss) &&
                (host.open != view.bounds("theme-popup").has_value() || host.help != view.bounds("theme-help").has_value()))
                refresh();
        };
        auto click = [&](Point point, std::chrono::microseconds time) {
            dispatch({time, PointerDown{{1}, point}});
            dispatch({time + 1us, PointerUp{{1}, point}});
        };
        refresh();
        capture("popup-initial");
        dispatch({1us, PointerMove{{1}, center(*view.bounds("theme"))}});
        require(host.help && !host.open, "Hover must show the noninteractive tooltip");
        capture("popup-tooltip");
        click(center(*view.bounds("theme")), 2us);
        require(host.open && !host.help && focused == view.tree->find("dark"), "Pointer opening must focus the first popup item");
        require(view.bounds("theme-popup")->origin.y + view.bounds("theme-popup")->size.height > 72,
            "Popup operation must escape the ordinary scroll clip");
        capture("popup-open");
        // Explicit refresh verifies the popup does not steal focus on replacement.
        focused = take(focus.focus(view.hit(), *view.tree->find("light"))).focused;
        refresh();
        require(focused == view.tree->find("light"), "Popup replacement must not steal focus from the selected option");
        click(center(*view.bounds("light")), 4us);
        require(host.selected == "Light" && host.selections == 1 && !host.open && focused == view.tree->find("theme"),
            "Popup selection must execute once and restore the trigger");
        dispatch({6us, Activate{}});
        require(host.open, "Keyboard activation must open the same popup");
        dispatch({7us, Navigate{Direction::down}}, CommandSource::gamepad);
        require(focused == view.tree->find("light"), "Gamepad navigation must reach the second option");
        dispatch({8us, Activate{}}, CommandSource::gamepad);
        require(host.selected == "Light" && host.selections == 2 && !host.open, "Gamepad selection must use ordinary commands");
        dispatch({9us, Activate{}});
        click({220, 150}, 10us);
        require(!host.open && host.scene_selections == 0, "Outside dismissal must not click through");
        dispatch({12us, Activate{}});
        dispatch({13us, Cancel{}}, CommandSource::gamepad);
        require(!host.open && focused == view.tree->find("theme"), "Cancel must restore keyboard/gamepad focus");
        dispatch({14us, Activate{}});
        scroll_y = 16;
        refresh();
        require(view.bounds("theme-popup")->origin.y == 48, "Popup must follow the scrolled trigger");
        capture("popup-scrolled");
        viewport.height = 100;
        refresh();
        require(view.bounds("theme-popup")->origin.y == 36, "Narrow viewport must shift the popup into view");
        capture("popup-resized");
        host.anchor = false;
        refresh();
        require(!host.open && !host.help && !view.bounds("theme-popup"), "Removing the trigger must dismiss all transient content");
        std::cout << "Popup/Tooltip: hover, selection, keyboard/gamepad, outside dismissal, scrolling, resize and anchor removal passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_popup_tooltip({}); }
#endif
