#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/render/paint.hpp>
#include "../fixtures.hpp"
#include <algorithm>
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
const ValidationContext actions{{"folder.all", "folder.models", "folder.materials", "workspace.toggle",
    "asset.cube", "asset.sphere", "asset.paint"}};
struct Host final : CommandHost {
    bool expanded = false;
    std::string folder = "models";
    std::string asset = "cube";
    int selections = 0;
    int toggles = 0;
    std::vector<ui::ListItem> assets() const {
        std::vector<ui::ListItem> result;
        if (folder != "materials") {
            result.push_back({"cube", "Cube mesh", "asset.cube"});
            result.push_back({"sphere", "Sphere mesh", "asset.sphere"});
        }
        if (folder != "models") result.push_back({"paint", "Paint material", "asset.paint"});
        return result;
    }
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "workspace.toggle") { expanded = !expanded; ++toggles; }
        else if (invocation.command.starts_with("folder.")) {
            folder = invocation.command.substr(7);
            const auto items = assets();
            if (std::none_of(items.begin(), items.end(), [&](const ui::ListItem& item) { return item.key == asset; }))
                asset = items.front().key;
        } else if (invocation.command.starts_with("asset.")) {
            asset = invocation.command.substr(6);
            ++selections;
        } else throw std::runtime_error("Unexpected browser command");
        return {};
    }
};
ui::Element Browser(const Host& host) {
    return ui::Column({
        ui::TreeView("Folders", host.folder == "all" ? "workspace" : host.folder,
            {{"workspace", "Workspace", "folder.all", "workspace.toggle", host.expanded, false, {
                {"models", "Models", "folder.models"}, {"materials", "Materials", "folder.materials"}}}},
            {.node = {.id = "folders", .style = {.height = Dimension::points(90)}}}),
        ui::ListView("Assets", host.asset, host.assets(),
            {.node = {.id = "assets", .style = {.height = Dimension::points(60)}}}),
        ui::Text("Details: " + host.asset, {.id = "details"})},
        {.id = "root", .style = {.overflow = Overflow::clip}});
}
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    const LayoutBox& box(std::string_view id) const {
        const auto node = tree->find(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
            [&](const LayoutBox& box) { return box.node == node; });
        require(found != layout.boxes.end(), "Missing browser control");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
} // namespace

int tessera::examples::run_tree_list(const FrameObserver& observer) {
    try {
        Host host;
        ui::Session session;
        PlaceholderTextShaper text;
        FocusDispatcher focus;
        PointerDispatcher pointer;
        CommandExecutor executor;
        std::vector<CommandDescriptor> descriptors;
        CommandBindings bindings;
        CommandStates states;
        for (const auto& action : actions.actions) {
            descriptors.push_back({action, action});
            bindings.emplace(action, action);
            states.emplace(action, CommandState{true});
        }
        const auto registry = take(CommandRegistry::create(std::move(descriptors), std::move(bindings)));
        std::optional<CommandSnapshot> commands;
        std::uint64_t generation = 0, sequence = 0;
        std::optional<NodeHandle> focused;
        Size viewport{280, 190};
        std::map<std::string, Point, std::less<>> scroll_offsets;
        View view;
        auto store_scroll = [&](const std::vector<ScrollUpdate>& updates) {
            for (const auto& update : updates) {
                const auto& id = view.tree->get(update.container)->id;
                require(id.has_value(), "Browser scroll owners need stable IDs");
                scroll_offsets[*id] = update.offset;
            }
        };
        auto refresh = [&](bool reveal = false) {
            auto authored = take(session.lower(Browser(host), actions));
            commands = take(CommandSnapshot::publish(registry, ++generation, CommandMode::production, states));
            auto tree = take(UiTree::create(take(commands->apply_eligibility(authored.document, actions)), actions));
            const StyleSheet sheet;
            auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
            std::vector<Point> offsets(tree->size());
            for (const auto& [id, offset] : scroll_offsets)
                if (const auto node = tree->find(id)) offsets[node->index] = offset;
            auto layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
            const auto previous = focused && view.tree ? view.tree->get(*focused)->id : std::nullopt;
            view = {std::move(tree), std::move(styles), std::move(layout)};
            (void)take(pointer.refresh(view.hit()));
            focused = take(focus.refresh(view.hit())).focused;
            const auto current = focused ? view.tree->get(*focused)->id : std::nullopt;
            if (focused && (reveal || previous != current)) {
                store_scroll(take(scroll_into_view(view.hit(), *focused)));
                for (const auto& [id, offset] : scroll_offsets)
                    if (const auto node = view.tree->find(id)) offsets[node->index] = offset;
                view.layout = take(compute_layout({view.tree.get(), view.styles, viewport, &text, offsets}));
            }
            for (const auto& box : view.layout.boxes)
                if (box.scroll) scroll_offsets[*view.tree->get(box.node)->id] = box.scroll->offset;
            (void)take(build_semantic_tree({view.tree.get(), view.styles, &view.layout, focused}));
            (void)take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
            session.publish();
        };
        auto capture = [&](std::string_view name) {
            if (observer) observer(name, viewport,
                take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text})));
        };
        auto execute = [&](const std::vector<ActionRequest>& requests, CommandSource source) {
            for (const auto& request : requests) {
                const auto semantic = view.semantic();
                const auto invocation = take(commands->resolve_action(semantic, request,
                    commands->generation(), {}, source, ++sequence));
                const auto result = take(executor.execute(*commands, invocation, host, &semantic));
                require(result.outcome.status == CommandOutcomeStatus::success, "Browser command failed");
            }
        };
        std::chrono::microseconds time{};
        auto navigate = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
            const auto result = take(focus.dispatch(view.hit(), event));
            const bool moved = focused != result.focused;
            focused = result.focused;
            execute(result.actions, source);
            refresh(moved);
        };
        auto click = [&](std::string_view id) {
            const auto position = view.center(id);
            const auto down = take(pointer.dispatch(view.hit(), {time += 1us, PointerDown{{1}, position}}));
            for (const auto& state : down.pointers)
                if (state.pressed) {
                    // Disclosure buttons do not add a Tab stop; their row owns focus.
                    auto owner = *state.pressed;
                    const auto& node_id = *view.tree->get(owner)->id;
                    const std::string prefix = "folders-toggle-";
                    if (node_id.starts_with(prefix))
                        owner = *view.tree->find("folders-item-" + node_id.substr(prefix.size()));
                    focused = take(focus.focus(view.hit(), owner)).focused;
                }
            const auto up = take(pointer.dispatch(view.hit(), {time += 1us, PointerUp{{1}, position}}));
            execute(up.actions, CommandSource::pointer);
            refresh(true);
        };
        refresh();
        capture("tree-initial");
        click("folders-toggle-workspace");
        require(host.expanded && host.toggles == 1 && host.asset == "cube", "Expansion must preserve selection/details");
        capture("tree-expanded");
        click("assets-label-sphere");
        require(host.asset == "sphere" && host.selections == 1, "Pointer selection must update details once");
        capture("tree-selected");
        navigate({time += 1us, KeyDown{Key::home}});
        navigate({time += 1us, Activate{}});
        require(host.asset == "cube" && host.selections == 2, "Keyboard must select the same list command");
        click("folders-label-models");
        navigate({time += 1us, Navigate{Direction::down}}, CommandSource::gamepad);
        require(host.folder == "models", "Navigation must not change controlled selection");
        navigate({time += 1us, Activate{}}, CommandSource::gamepad);
        require(host.folder == "materials" && host.asset == "paint", "Gamepad selection must update the dependent list/details");
        navigate({time += 1us, Navigate{Direction::left}}, CommandSource::gamepad);
        navigate({time += 1us, Navigate{Direction::left}}, CommandSource::gamepad);
        require(!host.expanded && host.folder == "materials" && host.asset == "paint",
            "Collapsing a selected descendant must retain selection/details");
        capture("tree-collapsed");
        navigate({time += 1us, Navigate{Direction::right}}, CommandSource::gamepad);
        click("folders-label-workspace");
        viewport = {140, 190};
        refresh();
        (void)take(focus.focus(view.hit(), *view.tree->find("assets-item-cube")));
        focused = *view.tree->find("assets-item-cube");
        navigate({time += 1us, KeyDown{Key::end}});
        navigate({time += 1us, Activate{}});
        require(host.asset == "paint" && host.selections == 3, "Narrow resize/focus reveal must preserve a reachable last item");
        store_scroll(take(route_scroll(view.hit(), {view.center("assets"), {0, -20}})).updates);
        refresh();
        require(host.asset == "paint" && host.folder == "all", "Wheel scrolling must not mutate browser state");
        capture("tree-narrow-scrolled");
        const auto semantics = take(build_semantic_tree(view.semantic()));
        require(std::any_of(semantics.nodes.begin(), semantics.nodes.end(), [](const SemanticNode& node) {
            return node.id == "assets-item-paint" && node.selected == true;
        }), "Inspection must observe the selected asset");
        std::cout << "TreeView/ListView browser: pointer, keyboard/gamepad, details, collapse, resize and scroll passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_tree_list({}); }
#endif
