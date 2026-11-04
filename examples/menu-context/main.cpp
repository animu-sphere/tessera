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
enum class OpenMenu { none, actions, asset };
const ValidationContext actions{{"menu.open", "asset.select", "asset.inspect", "asset.rename", "asset.delete"}};
struct Host final : CommandHost {
    OpenMenu open = OpenMenu::none;
    Point position;
    bool asset = true;
    std::string status = "Asset ready";
    int invocations = 0;
    int selections = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "menu.open") open = OpenMenu::actions;
        else if (invocation.command == "asset.select") ++selections;
        else if (invocation.command == "asset.inspect" || invocation.command == "asset.rename") {
            status = invocation.command == "asset.inspect" ? "Inspected Asset" : "Renamed Asset";
            ++invocations;
            open = OpenMenu::none;
        } else throw std::runtime_error("Unexpected or disabled menu command");
        return {};
    }
};
ui::Element Form(const Host& host) {
    const std::vector<ui::MenuItem> items{{"inspect", "Inspect", "asset.inspect"},
        {"delete", "Delete (unavailable)", "asset.delete", true}, {"rename", "Rename", "asset.rename"}};
    std::vector<ui::Element> content;
    if (host.asset) content.push_back(ui::Button("Asset: right-click or Shift+F10", "asset.select",
        {.node = {.id = "asset", .style = {.height = Dimension::points(56)}}}));
    content.push_back(ui::Column({}, {.style = {.height = Dimension::points(160)}}));
    auto menu_props = [](const char* id, bool open) {
        return ui::PopupProps{.node = {.id = id, .style = {.width = Dimension::points(200),
            .padding = Edges{4, 4, 4, 4}, .background = Color{0.85f, 0.9f, 1, 1}}}, .open = open};
    };
    return ui::Column({
        ui::Button("Actions", "menu.open", {.node = {.id = "actions", .style = {.height = Dimension::points(28)}}}),
        ui::ScrollView(std::move(content), {.id = "browser", .style = {.height = Dimension::points(88)}}),
        ui::Button("Background", "asset.select", {.node = {.id = "background",
            .style = {.height = Dimension::points(64)}}}),
        ui::Text(host.status, {.id = "status"}),
        ui::Menu("Asset actions", "actions", items, menu_props("actions-menu", host.open == OpenMenu::actions)),
        ui::ContextMenu("Asset actions", "asset", host.position, items,
            menu_props("asset-menu", host.open == OpenMenu::asset))},
        {.id = "root", .style = {.overflow = Overflow::clip}});
}
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    Rect bounds(const char* id) const {
        const auto node = tree->find(id);
        for (const auto& box : layout.boxes) if (node == box.node) return box.border_box;
        throw std::runtime_error("Expected a visible menu example node");
    }
    Point center(const char* id) const {
        const auto box = bounds(id);
        return {box.origin.x + box.size.width / 2, box.origin.y + box.size.height / 2};
    }
};
} // namespace

int tessera::examples::run_menu_context(const FrameObserver& observer) {
    try {
        Host host;
        ui::Session session;
        PlaceholderTextShaper text;
        MenuDispatcher menu("actions-menu"), context("asset-menu");
        PointerDispatcher pointer;
        FocusDispatcher focus;
        CommandExecutor executor;
        std::optional<NodeHandle> focused;
        Size viewport{360, 240};
        float scroll_y = 0;
        View view;
        const auto registry = take(CommandRegistry::create({
            {"menu.open", "Open actions"}, {"asset.select", "Select asset"},
            {"asset.inspect", "Inspect asset"}, {"asset.rename", "Rename asset"}, {"asset.delete", "Delete asset"}},
            {{"menu.open", "menu.open"}, {"asset.select", "asset.select"}, {"asset.inspect", "asset.inspect"},
             {"asset.rename", "asset.rename"}, {"asset.delete", "asset.delete"}}));
        const auto commands = take(CommandSnapshot::publish(registry, 1, CommandMode::production,
            {{"menu.open", {true}}, {"asset.select", {true}}, {"asset.inspect", {true}},
             {"asset.rename", {true}}, {"asset.delete", {false}}}));
        std::uint64_t sequence = 0;
        auto refresh = [&] {
            for (int attempt = 0; attempt < 3; ++attempt) {
                auto authored = take(session.lower(Form(host), actions));
                auto tree = take(UiTree::create(authored.document, actions));
                const StyleSheet sheet;
                auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
                std::vector<Point> offsets(tree->size());
                offsets[tree->find("browser")->index] = {0, scroll_y};
                auto layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
                view = {std::move(tree), std::move(styles), std::move(layout)};
                (void)take(pointer.refresh(view.hit()));
                focused = take(focus.refresh(view.hit())).focused;
                const auto ordinary = take(menu.refresh(view.hit()));
                const auto contextual = take(context.refresh(view.hit()));
                for (const auto& transition : {ordinary, contextual})
                    if (transition.focus) focused = take(focus.focus(view.hit(), *transition.focus)).focused;
                if ((ordinary.dismiss && host.open == OpenMenu::actions) ||
                    (contextual.dismiss && host.open == OpenMenu::asset)) {
                    host.open = OpenMenu::none;
                    continue;
                }
                (void)take(build_semantic_tree({view.tree.get(), view.styles, &view.layout, focused}));
                (void)take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
                session.publish();
                return;
            }
            throw std::runtime_error("Menu presentation did not settle");
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
                require(result.outcome.status == CommandOutcomeStatus::success, "Menu command failed");
            }
        };
        auto dispatch = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
            auto transition = take(context.dispatch(view.hit(), event, focused));
            if (!transition.consumed && !transition.dismiss)
                transition = take(menu.dispatch(view.hit(), event, focused));
            if (transition.focus) focused = take(focus.focus(view.hit(), *transition.focus)).focused;
            if (transition.open_at) {
                host.position = *transition.open_at;
                host.open = OpenMenu::asset;
            } else if (transition.dismiss) host.open = OpenMenu::none;
            if (transition.open_at || transition.dismiss) {
                pointer.reset();
                refresh();
            }
            if (transition.consumed) return;
            if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->key == Key::tab)
                event.data = key->modifiers.shift ? decltype(event.data){FocusPrevious{}} : decltype(event.data){FocusNext{}};
            if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
                std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
                const auto result = take(pointer.dispatch(view.hit(), event));
                if (std::holds_alternative<PointerDown>(event.data))
                    for (const auto& state : result.pointers)
                        if (state.pressed) focused = take(focus.focus(view.hit(), *state.pressed)).focused;
                execute(result.actions, CommandSource::pointer);
                if (!result.actions.empty()) refresh();
            } else if (std::holds_alternative<FocusNext>(event.data) || std::holds_alternative<FocusPrevious>(event.data) ||
                std::holds_alternative<Navigate>(event.data) || std::holds_alternative<Activate>(event.data)) {
                const auto result = take(focus.dispatch(view.hit(), event));
                focused = result.focused;
                execute(result.actions, source);
                if (!result.actions.empty()) refresh();
            }
        };
        std::chrono::microseconds time{};
        auto click = [&](const char* id) {
            const auto position = view.center(id);
            dispatch({++time, PointerDown{{1}, position}});
            dispatch({++time, PointerUp{{1}, position}});
        };
        auto right_click = [&] {
            const auto position = view.center("asset");
            dispatch({++time, PointerDown{{1}, position, PointerButton::secondary}});
            dispatch({++time, PointerUp{{1}, position, PointerButton::secondary}});
        };
        auto focused_id = [&] { return focused ? view.tree->get(*focused)->id.value_or("") : ""; };
        refresh();
        capture("menu-initial");
        click("actions");
        require(host.open == OpenMenu::actions && focused_id() == "actions-menu-item-inspect",
            "Pointer opening must focus the first command");
        capture("menu-open");
        click("actions-menu-item-inspect");
        require(host.invocations == 1 && host.status == "Inspected Asset" && focused_id() == "actions",
            "Pointer choice must execute once, close and restore focus");
        dispatch({++time, Activate{}});
        dispatch({++time, Navigate{Direction::down}}, CommandSource::gamepad);
        require(focused_id() == "actions-menu-item-rename", "Gamepad navigation must skip the disabled command");
        dispatch({++time, Activate{}}, CommandSource::gamepad);
        require(host.invocations == 2 && host.status == "Renamed Asset", "Gamepad must execute the ordinary item command");
        right_click();
        require(host.open == OpenMenu::asset && host.selections == 0 && focused_id() == "asset-menu-item-inspect",
            "Right-click must open without activating the asset");
        capture("menu-context");
        dispatch({++time, Cancel{}}, CommandSource::gamepad);
        require(host.open == OpenMenu::none && focused_id() == "asset", "Cancel must restore the context target");
        dispatch({++time, KeyDown{Key::f10, {true}}});
        require(host.open == OpenMenu::asset, "Shift+F10 must open from the focused asset");
        click("asset-menu-item-inspect");
        require(host.invocations == 3 && host.status == "Inspected Asset", "Context item must share the same command path");
        right_click();
        dispatch({++time, KeyDown{Key::escape}});
        require(host.open == OpenMenu::none && host.invocations == 3, "Escape must close without executing");
        right_click();
        click("background");
        require(host.open == OpenMenu::none && host.selections == 0, "Outside press/release must not click through");
        click("actions");
        dispatch({++time, FocusNext{}});
        require(host.open == OpenMenu::none && focused_id() == "asset", "Tab must close and continue outside the menu");
        right_click();
        const auto position = host.position;
        viewport = {300, 220};
        scroll_y = 8;
        refresh();
        require(host.open == OpenMenu::asset && host.position == position && focused_id() == "asset-menu-item-inspect",
            "Resize and scroll must preserve the open context operation");
        capture("menu-resized");
        host.asset = false;
        refresh();
        require(host.open == OpenMenu::none, "Removing the target must dismiss without retaining stale handles");
        std::cout << "Menu/ContextMenu: pointer, keyboard/gamepad, commands, focus, dismissal and resize passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_menu_context({}); }
#endif
