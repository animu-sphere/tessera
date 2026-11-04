#include <tessera/ui/authoring.hpp>
#include <tessera/input/modal.hpp>
#include <tessera/input/scroll.hpp>
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
const ValidationContext actions{
    {"settings.open_reset", "settings.cancel_reset", "settings.confirm_reset", "settings.toggle"}};
struct Host final : CommandHost {
    bool open = false;
    bool enabled = true;
    int resets = 0;
    int toggles = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "settings.open_reset")
            open = true;
        else if (invocation.command == "settings.cancel_reset")
            open = false;
        else if (invocation.command == "settings.confirm_reset") {
            enabled = false;
            open = false;
            ++resets;
        } else if (invocation.command == "settings.toggle") {
            enabled = !enabled;
            ++toggles;
        } else
            throw std::runtime_error("Unexpected settings command");
        return {};
    }
};
ui::Element Form(const Host& host) {
    return ui::Column({ui::Checkbox("Enable effects", host.enabled, "settings.toggle",
                                    {.node = {.id = "effects", .style = {.height = Dimension::points(32)}}}),
                       ui::Button("Reset settings...", "settings.open_reset",
                                  {.node = {.id = "reset", .style = {.height = Dimension::points(32)}}}),
                       ui::Dialog("Reset settings?",
                                  {ui::Text("Restore defaults? Cancel keeps the current settings."),
                                   ui::Button("Cancel", "settings.cancel_reset",
                                              {.node = {.id = "cancel", .style = {.height = Dimension::points(28)}}}),
                                   ui::Button("Reset", "settings.confirm_reset",
                                              {.node = {.id = "confirm", .style = {.height = Dimension::points(28)}}})},
                                  {.node = {.id = "reset-dialog"},
                                   .panel = {.style = {.max_width = 240.f, .padding = Edges{8, 8, 8, 8}, .gap = 4.f}},
                                   .open = host.open})},
                      {.id = "root", .style = {.overflow = Overflow::clip}});
}
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    Point center(const char* id) const {
        const auto node = tree->find(id);
        for (const auto& box : layout.boxes)
            if (node == box.node)
                return {box.border_box.origin.x + box.border_box.size.width / 2,
                        box.border_box.origin.y + box.border_box.size.height / 2};
        throw std::runtime_error("Missing visible control");
    }
};
} // namespace

int tessera::examples::run_modal_dialog(const FrameObserver& observer) {
    try {
        Host host;
        ui::Session session;
        PlaceholderTextShaper text;
        ModalDispatcher modal("reset-dialog");
        PointerDispatcher pointer;
        FocusDispatcher focus;
        CommandExecutor executor;
        std::optional<NodeHandle> focused;
        Size viewport{320, 220};
        Point panel_offset;
        bool reveal_focus = false;
        View view;
        const auto registry = take(CommandRegistry::create({{"settings.open_reset", "Open reset confirmation"},
                                                            {"settings.cancel_reset", "Cancel reset"},
                                                            {"settings.confirm_reset", "Reset settings"},
                                                            {"settings.toggle", "Toggle effects"}},
                                                           {{"settings.open_reset", "settings.open_reset"},
                                                            {"settings.cancel_reset", "settings.cancel_reset"},
                                                            {"settings.confirm_reset", "settings.confirm_reset"},
                                                            {"settings.toggle", "settings.toggle"}}));
        const auto commands = take(CommandSnapshot::publish(registry, 1, CommandMode::production,
                                                            {{"settings.open_reset", {true}},
                                                             {"settings.cancel_reset", {true}},
                                                             {"settings.confirm_reset", {true}},
                                                             {"settings.toggle", {true}}}));
        std::uint64_t sequence = 0;
        auto apply = [&](const ModalDispatchResult& transition) {
            if (transition.reset_pointer) pointer.reset();
            if (transition.focus) {
                focused = take(focus.focus(view.hit(), *transition.focus)).focused;
                reveal_focus = true;
            }
            if (transition.dismiss) host.open = false;
        };
        auto store_scroll = [&](const std::vector<ScrollUpdate>& updates) {
            for (const auto& update : updates) {
                require(update.container == view.tree->find("reset-dialog-panel"), "Unexpected scroll owner");
                panel_offset = update.offset;
            }
        };
        auto refresh = [&] {
            const auto previous = focused ? view.tree->get(*focused)->id : std::nullopt;
            auto authored = take(session.lower(Form(host), actions));
            auto tree = take(UiTree::create(authored.document, actions));
            const StyleSheet sheet;
            auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
            std::vector<Point> offsets(tree->size());
            offsets[tree->find("reset-dialog-panel")->index] = panel_offset;
            auto layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
            view = {std::move(tree), std::move(styles), std::move(layout)};
            (void)take(pointer.refresh(view.hit()));
            focused = take(focus.refresh(view.hit())).focused;
            if (previous != (focused ? view.tree->get(*focused)->id : std::nullopt)) reveal_focus = true;
            apply(take(modal.refresh(view.hit(), focused)));
            if (focused && reveal_focus) {
                store_scroll(take(scroll_into_view(view.hit(), *focused)));
                offsets[view.tree->find("reset-dialog-panel")->index] = panel_offset;
                view.layout = take(compute_layout({view.tree.get(), view.styles, viewport, &text, offsets}));
            }
            reveal_focus = false;
            for (const auto& box : view.layout.boxes)
                if (box.node == view.tree->find("reset-dialog-panel")) panel_offset = box.scroll->offset;
            (void)take(build_semantic_tree({view.tree.get(), view.styles, &view.layout, focused}));
            (void)take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
            session.publish();
        };
        auto capture = [&](std::string_view name) {
            if (observer)
                observer(name, viewport, take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text})));
        };
        auto execute = [&](const std::vector<ActionRequest>& requests, CommandSource source) {
            for (const auto& request : requests) {
                const auto semantic = view.semantic();
                const auto invocation =
                    take(commands.resolve_action(semantic, request, commands.generation(), {}, source, ++sequence));
                const auto result = take(executor.execute(commands, invocation, host, &semantic));
                require(result.outcome.status == CommandOutcomeStatus::success, "Settings command failed");
            }
        };
        auto dispatch = [&](InputEvent event, CommandSource source = CommandSource::keyboard, bool publish = true) {
            const auto transition = take(modal.dispatch(view.hit(), event, focused));
            apply(transition);
            if (!transition.consumed) {
                if (std::holds_alternative<PointerMove>(event.data) ||
                    std::holds_alternative<PointerDown>(event.data) || std::holds_alternative<PointerUp>(event.data) ||
                    std::holds_alternative<PointerCancel>(event.data)) {
                    const auto result = take(pointer.dispatch(view.hit(), event));
                    if (std::holds_alternative<PointerDown>(event.data))
                        for (const auto& state : result.pointers)
                            if (state.pressed) {
                                reveal_focus = reveal_focus || focused != state.pressed;
                                focused = take(focus.focus(view.hit(), *state.pressed)).focused;
                            }
                    execute(result.actions, CommandSource::pointer);
                } else if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
                    store_scroll(take(route_scroll(view.hit(), *scroll)).updates);
                } else {
                    const auto result = take(focus.dispatch(view.hit(), event));
                    reveal_focus = reveal_focus || focused != result.focused;
                    focused = result.focused;
                    execute(result.actions, source);
                }
            }
            if (publish) refresh();
        };
        std::chrono::microseconds time{};
        auto click = [&](const char* id) {
            const auto position = view.center(id);
            // Preserve the accepted press snapshot until release.
            dispatch({time += 1us, PointerDown{{1}, position}}, CommandSource::pointer, false);
            dispatch({time += 1us, PointerUp{{1}, position}}, CommandSource::pointer);
        };
        refresh();
        capture("modal-initial");
        click("reset");
        require(host.open && focused == view.tree->find("cancel"), "Pointer opening must focus safe Cancel");
        capture("modal-open");
        const auto background = view.center("effects");
        dispatch({time += 1us, PointerDown{{2}, background}});
        dispatch({time += 1us, PointerUp{{2}, background}});
        require(host.enabled && host.toggles == 0 && host.open, "Modal must block background activation");
        click("cancel");
        require(!host.open && host.enabled && host.resets == 0 && focused == view.tree->find("reset"),
                "Cancel must preserve settings and recover the opener");
        dispatch({time += 1us, Activate{}});
        require(host.open, "Keyboard must open the same dialog command");
        dispatch({time += 1us, FocusPrevious{}});
        require(focused == view.tree->find("confirm"), "Shift-Tab must wrap only inside the dialog");
        dispatch({time += 1us, FocusNext{}});
        dispatch({time += 1us, Navigate{Direction::down}}, CommandSource::gamepad);
        require(focused == view.tree->find("confirm"), "Gamepad navigation must reach confirmation");
        dispatch({time += 1us, Activate{}}, CommandSource::gamepad);
        require(!host.open && !host.enabled && host.resets == 1 && focused == view.tree->find("reset"),
                "Confirmation must reset once and recover focus");
        capture("modal-confirmed");
        click("reset");
        dispatch({time += 1us, Cancel{}}, CommandSource::gamepad);
        require(!host.open && host.resets == 1, "Gamepad Cancel must close without another reset");
        click("reset");
        dispatch({time += 1us, KeyDown{Key::escape}});
        require(!host.open && focused == view.tree->find("reset"), "Escape must close and restore focus");
        viewport = {180, 160};
        click("reset");
        require(host.open && focused == view.tree->find("cancel"), "Resize must preserve a reachable safe default");
        dispatch({time += 1us, Scroll{view.center("reset-dialog-panel"), {0, -20}}});
        require(host.open && host.resets == 1, "Panel wheel routing must retain controlled settings");
        capture("modal-narrow-scrolled");
        dispatch({time += 1us, Navigate{Direction::down}}, CommandSource::gamepad);
        click("confirm");
        require(!host.open && host.resets == 2, "Focus reveal must make narrow confirmation pointer-reachable");
        std::cout
            << "Modal/Dialog settings reset: pointer, keyboard/gamepad, background blocking, resize and focus recovery passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_modal_dialog({}); }
#endif
