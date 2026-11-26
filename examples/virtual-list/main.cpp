#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>
#include <algorithm>
#include <iostream>
#include <map>
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
const ValidationContext actions{{"log.select", "scene.select", "scene.toggle"}};
constexpr float row_extent = 20;

// One virtual collection's host-owned presentation state: scroll offset, accepted viewport and focus.
struct Pane {
    std::string id;
    float height = 0;
    float offset = 0;
    float viewport = 0;
    std::optional<std::string> focus;
    std::optional<std::string> selected;
    ui::VirtualWindow window() const {
        ui::VirtualWindow result{.item_extent = row_extent, .viewport = viewport, .offset = offset, .overscan = 2};
        if (focus) result.pinned.push_back(*focus);
        return result;
    }
    bool operator==(const Pane&) const = default;
};

// An application with a streaming event log and a large scene outline, far beyond the document limit.
struct Application {
    std::vector<ui::ListItem> log;
    std::vector<ui::TreeItem> scene;
    std::size_t next_event = 0;
    Pane log_pane{"log", 160};
    Pane scene_pane{"scene", 200};
    Application() {
        for (int i = 0; i < 50000; ++i) append_event();
        for (int group = 0; group < 400; ++group) {
            ui::TreeItem node{"g" + std::to_string(group), "Group " + std::to_string(group), "scene.select",
                              "scene.toggle", group % 3 != 2};
            for (int object = 0; object < 50; ++object)
                node.children.push_back(
                    {node.key + "-" + std::to_string(object), "Object " + std::to_string(object), "scene.select"});
            scene.push_back(std::move(node));
        }
    }
    void append_event() {
        const auto id = next_event++;
        log.push_back({"e" + std::to_string(id), "Event " + std::to_string(id), "log.select", id % 97 == 13});
    }
    ui::TreeItem* group(std::string_view key) {
        const auto found =
            std::find_if(scene.begin(), scene.end(), [&](const ui::TreeItem& node) { return node.key == key; });
        return found == scene.end() ? nullptr : &*found;
    }
};

ui::Element Browser(const Application& app) {
    const auto& scene = app.scene_pane;
    const auto& log = app.log_pane;
    return ui::Column(
        {ui::VirtualTreeView("Scene", scene.selected, app.scene,
                             {.node = {.id = scene.id, .style = {.height = Dimension::points(scene.height)}},
                              .window = scene.window()}),
         ui::VirtualListView(
             "Event log", log.selected, app.log,
             {.node = {.id = log.id, .style = {.height = Dimension::points(log.height)}}, .window = log.window()}),
         ui::Text("Selected: " + log.selected.value_or("-") + " / " + scene.selected.value_or("-"), {.id = "details"})},
        {.id = "root", .style = {.overflow = Overflow::clip}});
}

struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    const LayoutBox* find(std::string_view id) const {
        const auto node = tree->find(id);
        if (!node) return nullptr;
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == *node; });
        return found == layout.boxes.end() ? nullptr : &*found;
    }
    const LayoutBox& box(std::string_view id) const {
        const auto* found = find(id);
        require(found, "Missing realized browser row");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
    // The key of the realized row at the pane's leading edge.
    std::string leading_key(const Pane& pane) const {
        const auto top = box(pane.id).border_box.origin.y;
        for (const auto& box : layout.boxes) {
            const auto& id = tree->get(box.node)->id;
            if (id && id->starts_with(pane.id + "-item-") && box.border_box.origin.y <= top &&
                box.border_box.origin.y + box.border_box.size.height > top)
                return id->substr(pane.id.size() + 6);
        }
        throw std::runtime_error("No realized row at the leading edge");
    }
};

struct Host final : CommandHost {
    Application app;
    const View* view = nullptr;
    int selections = 0, toggles = 0;
    // The row key the invocation targets: the item row or its disclosure button.
    std::string target_key(const CommandInvocationRecord& invocation, const Pane& pane) const {
        require(invocation.owner.has_value(), "Browser commands come from a row");
        const auto& id = *view->tree->get(invocation.owner->target)->id;
        for (const auto& part : {pane.id + "-item-", pane.id + "-toggle-", pane.id + "-label-"})
            if (id.starts_with(part)) return id.substr(part.size());
        throw std::runtime_error("Unexpected command owner");
    }
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        if (invocation.command == "log.select") {
            app.log_pane.selected = target_key(invocation, app.log_pane);
            ++selections;
        } else if (invocation.command == "scene.select") {
            app.scene_pane.selected = target_key(invocation, app.scene_pane);
            ++selections;
        } else if (invocation.command == "scene.toggle") {
            auto* group = app.group(target_key(invocation, app.scene_pane));
            require(group != nullptr, "Only groups toggle");
            group->expanded = !group->expanded;
            ++toggles;
        } else
            throw std::runtime_error("Unexpected browser command");
        return {};
    }
};
} // namespace

int main() {
    try {
        Host host;
        auto& app = host.app;
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
        Size viewport{320, 420};
        View view;
        std::size_t largest = 0, frames = 0;
        auto pane_of = [&](std::string_view id) -> Pane* {
            for (auto* pane : {&app.log_pane, &app.scene_pane})
                if (id.starts_with(pane->id + "-item-")) return pane;
            return nullptr;
        };
        // Recompose until the windows match the accepted layout, focus and reveal (at most a few passes).
        auto refresh = [&](bool reveal = false) {
            for (int pass = 0;; ++pass) {
                require(pass < 4, "Virtual windows must settle");
                const auto before = std::pair{app.log_pane, app.scene_pane};
                const auto previous = focused && view.tree ? view.tree->get(*focused)->id : std::nullopt;
                auto authored = take(session.lower(Browser(app), actions));
                commands = take(CommandSnapshot::publish(registry, ++generation, CommandMode::production, states));
                auto tree =
                    take(UiTree::create(take(commands->apply_eligibility(authored.document, actions)), actions));
                const StyleSheet sheet;
                auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
                std::vector<Point> offsets(tree->size());
                for (const auto* pane : {&app.log_pane, &app.scene_pane})
                    offsets[tree->find(pane->id)->index] = {0, pane->offset};
                auto layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
                view = {std::move(tree), std::move(styles), std::move(layout)};
                host.view = &view;
                largest = std::max(largest, view.tree->size());
                (void)take(pointer.refresh(view.hit()));
                focused = take(focus.refresh(view.hit())).focused;
                const auto current = focused ? view.tree->get(*focused)->id : std::nullopt;
                for (auto* pane : {&app.log_pane, &app.scene_pane}) {
                    const auto& box = view.box(pane->id);
                    pane->offset = box.scroll->offset.y;
                    pane->viewport = box.border_box.size.height - box.border.top - box.border.bottom;
                    pane->focus.reset();
                }
                if (current)
                    if (auto* pane = pane_of(*current)) pane->focus = current->substr(pane->id.size() + 6);
                if (focused && (reveal || previous != current))
                    for (const auto& update : take(scroll_into_view(view.hit(), *focused))) {
                        const auto& id = view.tree->get(update.container)->id;
                        for (auto* pane : {&app.log_pane, &app.scene_pane})
                            if (id == pane->id) pane->offset = update.offset.y;
                    }
                reveal = false;
                if (std::pair{app.log_pane, app.scene_pane} == before) break;
            }
            (void)take(build_semantic_tree(view.semantic(focused)));
            (void)take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
            session.publish();
            ++frames;
        };
        auto execute = [&](const std::vector<ActionRequest>& requests, CommandSource source) {
            for (const auto& request : requests) {
                const auto semantic = view.semantic();
                const auto invocation =
                    take(commands->resolve_action(semantic, request, commands->generation(), {}, source, ++sequence));
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
                    const std::string prefix = "scene-toggle-";
                    if (node_id.starts_with(prefix))
                        owner = *view.tree->find("scene-item-" + node_id.substr(prefix.size()));
                    focused = take(focus.focus(view.hit(), owner)).focused;
                }
            const auto up = take(pointer.dispatch(view.hit(), {time += 1us, PointerUp{{1}, position}}));
            execute(up.actions, CommandSource::pointer);
            refresh(true);
        };
        auto semantics = [&] { return take(build_semantic_tree(view.semantic(focused))); };
        auto entry = [&](const SemanticTree& tree, std::string_view id) -> const SemanticNode& {
            const auto found = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                                            [&](const SemanticNode& node) { return node.id == id; });
            require(found != tree.nodes.end(), "Missing semantic entry");
            return *found;
        };

        refresh();
        auto observed = semantics();
        require(entry(observed, "log").item_count == 50000 && entry(observed, "scene").item_count == 400 + 267 * 50,
                "Collections must report every visible logical row");
        require(view.tree->size() < 300, "Only a bounded set of rows is realized");

        click("log-label-e3");
        require(app.log_pane.selected == "e3" && host.selections == 1, "Pointer selection uses the row command");
        navigate({time += 1us, KeyDown{Key::end}});
        require(app.log_pane.focus == "e49999" && view.find("log-item-e49999"), "End reaches the last logical row");
        require(app.log_pane.offset == 50000 * row_extent - app.log_pane.viewport, "End reveals the last row");
        navigate({time += 1us, Activate{}});
        require(app.log_pane.selected == "e49999", "Keyboard selection reaches an initially unrealized row");
        navigate({time += 1us, KeyDown{Key::home}});
        require(app.log_pane.focus == "e0" && app.log_pane.offset == 0, "Home returns to the first row");

        const auto scroll = take(route_scroll(view.hit(), {view.center("log"), {0, 400000}})).updates;
        require(scroll.size() == 1, "Wheel input scrolls the log");
        app.log_pane.offset = scroll.front().offset.y;
        refresh();
        require(app.log_pane.offset == 400000 && view.leading_key(app.log_pane) == "e20000" &&
                    app.log_pane.focus == "e0",
                "Wheel scrolling crosses unrealized rows and keeps focus realized offscreen");
        navigate({time += 1us, Navigate{Direction::down}});
        require(app.log_pane.focus == "e1" && app.log_pane.offset == row_extent,
                "Arrow movement from offscreen focus reveals the next row");

        // Streaming: prepend history while scrolled into the middle; the anchored row stays in place.
        app.log_pane.offset = 123450;
        refresh();
        const auto anchor = *ui::virtual_anchor(ui::virtual_keys(app.log), row_extent, app.log_pane.offset);
        const auto leading = view.leading_key(app.log_pane);
        std::vector<ui::ListItem> history;
        for (int i = 0; i < 250; ++i)
            history.push_back({"h" + std::to_string(i), "Archived " + std::to_string(i), "log.select"});
        app.log.insert(app.log.begin(), history.begin(), history.end());
        app.log_pane.offset = *ui::virtual_offset(ui::virtual_keys(app.log), row_extent, anchor);
        refresh();
        require(view.leading_key(app.log_pane) == leading && anchor.key == leading &&
                    app.log_pane.offset == 123450 + 250 * row_extent,
                "Anchored scrolling keeps visible content in place across a prepend");
        require(entry(semantics(), "log").item_count == 50250, "The logical count follows replaced data");

        // Scene: walk down across the window edge, then collapse a group from inside it.
        click("scene-label-g0-0");
        require(app.scene_pane.selected == "g0-0", "Scene selection");
        const auto scene_keys = ui::virtual_keys(app.scene);
        for (int step = 1; step <= 30; ++step) {
            navigate({time += 1us, Navigate{Direction::down}});
            require(app.scene_pane.focus == scene_keys[static_cast<std::size_t>(step) + 1],
                    "Down follows logical order through realization");
        }
        require(app.scene_pane.offset > 0, "Focus reveal scrolled the outline");
        navigate({time += 1us, Navigate{Direction::left}});
        require(app.scene_pane.focus == "g0", "Left reaches the group above the viewport");
        navigate({time += 1us, Navigate{Direction::left}});
        require(host.toggles == 1 && !app.group("g0")->expanded && app.scene_pane.focus == "g0",
                "Left collapses the group through its toggle command");
        navigate({time += 1us, Navigate{Direction::right}});
        require(app.group("g0")->expanded, "Right expands it again");
        navigate({time += 1us, Navigate{Direction::right}});
        require(app.scene_pane.focus == "g0-0", "Right enters the first child");

        // Agent operations through semantics on realized rows.
        observed = semantics();
        execute(
            {take(request_semantic_action(view.semantic(focused), entry(observed, "scene-item-g0").node, "collapse"))},
            CommandSource::agent);
        refresh();
        require(!app.group("g0")->expanded && app.scene_pane.focus == "g0",
                "Collapsing the focused row's group recovers focus to the group");
        observed = semantics();
        const auto& target = entry(observed, "scene-item-g1");
        require(target.item_position == 2, "Positions follow the collapsed outline");
        execute({take(request_semantic_action(view.semantic(focused), target.node, "activate"))}, CommandSource::agent);
        refresh();
        require(app.scene_pane.selected == "g1", "Semantic activation selects the realized row");
        click("scene-toggle-g0");
        require(app.group("g0")->expanded && host.toggles == 4 && app.scene_pane.focus == "g0",
                "The disclosure button expands the group and focuses its row");

        viewport = {320, 300};
        app.log_pane.height = 60;
        refresh();
        require(app.log_pane.viewport == 60 && view.tree->size() < 300, "Resize recomposes a smaller window");

        std::cout << "Virtual list: " << app.log.size() << " log rows and " << scene_keys.size()
                  << " outline rows; at most " << largest << " realized nodes over " << frames
                  << " frames; pointer, keyboard, wheel, anchor, collapse, semantic and resize passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
