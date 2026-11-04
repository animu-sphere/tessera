#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/input/split_pane.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include "../settings-starter/native_win32.hpp"
#endif

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
constexpr ui::SliderRange amount_range{0, 100, 5};
constexpr ui::SplitPaneLimits pane_limits{140, 320, 8};
struct Item {
    std::string id;
    TextEditState name;
    bool enabled = true;
    int amount = 50;
    bool operator==(const Item&) const = default;
};
enum class OpenMenu { none, actions, context };
struct State {
    std::vector<Item> items;
    std::string selected = "cube";
    bool expanded = true;
    float split = 220;
    OpenMenu menu = OpenMenu::none;
    Point position;
    const Item& item() const {
        const auto found =
            std::find_if(items.begin(), items.end(), [&](const Item& item) { return item.id == selected; });
        require(found != items.end(), "Selected Inspector item is missing");
        return *found;
    }
    Item& item() {
        const auto found =
            std::find_if(items.begin(), items.end(), [&](const Item& item) { return item.id == selected; });
        require(found != items.end(), "Selected Inspector item is missing");
        return *found;
    }
};
State initial_state() {
    State state;
    for (const auto& [id, label] : std::vector<std::pair<std::string, std::string>>{
             {"scene", "Scene"}, {"cube", "Cube"}, {"sphere", "Sphere"}, {"light", "Light"}, {"camera", "Camera"}})
        state.items.push_back({id, take(make_text_edit_state(state.items.size() + 1, label))});
    for (int i = 0; i < 12; ++i)
        state.items.push_back({"extra" + std::to_string(i),
                               take(make_text_edit_state(state.items.size() + 1, "Extra " + std::to_string(i)))});
    return state;
}
ValidationContext action_context(const State& state) {
    ValidationContext context{
        {"scene.toggle", "item.name", "item.enabled", "item.amount", "item.rename", "menu.open", "panes.resize"}};
    for (const auto& item : state.items) context.actions.insert("select." + item.id);
    return context;
}
CommandRegistry command_registry(const ValidationContext& context) {
    std::vector<CommandDescriptor> descriptors;
    CommandBindings bindings;
    for (const auto& action : context.actions) {
        descriptors.push_back({action, action});
        bindings.emplace(action, action);
    }
    return take(CommandRegistry::create(std::move(descriptors), std::move(bindings)));
}
CommandStates command_states(const ValidationContext& context) {
    CommandStates states;
    for (const auto& action : context.actions) states.emplace(action, CommandState{true});
    return states;
}
ui::Element Form(const State& state, Size viewport) {
    const float height = std::max(100.f, viewport.height - 64);
    std::vector<ui::TreeItem> children;
    for (std::size_t i = 1; i < state.items.size(); ++i) {
        const auto& item = state.items[i];
        children.push_back({item.id, item.name.text, "select." + item.id});
    }
    const auto& item = state.item();
    const auto control = [](const char* id) { return ui::Props{.id = id, .classes = {"control"}}; };
    const std::vector<ui::MenuItem> commands{{"rename", "Edit display name", "item.rename"},
                                             {"enabled", "Toggle enabled", "item.enabled"}};
    const auto popup = [&](const char* id, OpenMenu kind) {
        return ui::PopupProps{.node = {.id = id, .classes = {"control"}, .style = {.width = Dimension::points(220)}},
                              .open = state.menu == kind};
    };
    return ui::Column(
        {ui::Row(
             {ui::Text("Hierarchy / Inspector"), ui::Button("Item actions", "menu.open", {.node = control("actions")})},
             {.style = {.gap = 16.f}}),
         ui::HorizontalSplitPane(
             ui::TreeView("Hierarchy", state.selected,
                          {{"scene", state.items.front().name.text, "select.scene", "scene.toggle", state.expanded,
                            false, std::move(children)}},
                          {.node = {.id = "hierarchy", .style = {.height = Dimension::points(height)}}}),
             ui::ScrollView(
                 {ui::Button("Selected: " + item.id, "select." + item.id, {.node = control("selected")}),
                  ui::Grid(
                      {ui::Text("Display name"),
                       ui::TextInput("Name", item.name, "item.name", {.node = control("name")}), ui::Text("Enabled"),
                       ui::Checkbox("Enabled", item.enabled, "item.enabled", {.node = control("enabled")}),
                       ui::Text("Amount"),
                       ui::Slider("Amount", item.amount, amount_range, "item.amount", {.node = control("amount")})},
                      {.id = "properties",
                       .style = {.grid_columns =
                                     std::vector<GridTrack>{GridTrack{.maximum = 84}, GridTrack::fraction(1, 80)},
                                 .grid_rows = std::vector<GridTrack>{GridTrack{}, GridTrack{}, GridTrack{}},
                                 .column_gap = 8.f,
                                 .row_gap = 12.f}}),
                  ui::Text("Edits are kept per item.", {.id = "help"}),
                  ui::Text("Name: Ctrl+A, text, arrows, Home/End, Backspace/Delete.")},
                 {.id = "inspector", .style = {.height = Dimension::points(height), .gap = 12.f}}),
             state.split, "panes.resize",
             {.node = {.id = "panes", .style = {.height = Dimension::points(height)}},
              .limits = pane_limits,
              .label = "Resize hierarchy"}),
         ui::Menu("Item actions", "actions", commands, popup("actions-menu", OpenMenu::actions)),
         ui::ContextMenu("Item actions", "selected", state.position, commands, popup("item-menu", OpenMenu::context))},
        {.id = "application",
         .style = {.overflow = Overflow::clip,
                   .height = Dimension::points(viewport.height),
                   .padding = Edges{8, 8, 8, 8},
                   .gap = 8.f,
                   .background = Color{.95f, .95f, .98f, 1}}});
}
using Offsets = std::map<std::string, Point, std::less<>>;
struct View {
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    const LayoutBox& box(std::string_view id) const {
        const auto node = tree->find(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return node == box.node; });
        require(found != layout.boxes.end(), "Inspector control has no layout");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
struct TextService final : TextShaper {
    PlaceholderTextShaper delegate;
    bool reject = false;
    Result<TextMetrics> measure(std::string_view text, const TextStyle& style,
                                const TextConstraints& constraints) override {
        if (reject)
            return {std::nullopt,
                    {{"inspector_measurement", Severity::error, "/text", "Injected measurement rejection"}}};
        return delegate.measure(text, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view text, const TextStyle& style, const TextConstraints& constraints) override {
        return delegate.shape(text, style, constraints);
    }
};
class App final : public CommandHost {
public:
    State state = initial_state();
    ValidationContext context = action_context(state);

private:
    CommandRegistry registry = command_registry(context);
    CommandStates eligibility = command_states(context);

public:
    Size viewport{960, 360};
    View view{take(CommandSnapshot::publish(registry, 1, CommandMode::production, eligibility))};
    FocusDispatcher focus;
    TextService text;
    std::uint64_t executions = 0;

    App() { refresh(); }
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        State candidate = state;
        const auto& command = invocation.command;
        if (command.starts_with("select."))
            candidate.selected = command.substr(7);
        else if (command == "scene.toggle")
            candidate.expanded = !candidate.expanded;
        else if (command == "item.name") {
            require(edit.has_value(), "Name editing needs an owned request");
            auto next = edit_text(candidate.item().name, *edit);
            if (!next) return {CommandOutcomeStatus::error, {}, std::move(next.diagnostics)};
            candidate.item().name = std::move(*next.value);
        } else if (command == "item.enabled")
            candidate.item().enabled = !candidate.item().enabled;
        else if (command == "item.amount") {
            candidate.item().amount = amount.value_or(std::min(100, candidate.item().amount + amount_range.step));
        } else if (command == "panes.resize") {
            require(split.has_value(), "Pane resize needs a size");
            candidate.split = *split;
        } else if (command == "menu.open")
            candidate.menu = OpenMenu::actions;
        else if (command != "item.rename")
            throw std::runtime_error("Unknown Inspector command");
        if (command == "item.rename" || command == "item.enabled") candidate.menu = OpenMenu::none;
        auto candidate_focus = focus;
        if (command == "item.rename") (void)take(candidate_focus.focus(view.hit(), *view.tree->find("name")));
        auto prepared = stage(candidate, viewport, offsets, candidate_focus, true);
        if (!prepared) return {CommandOutcomeStatus::error, {}, std::move(prepared.diagnostics)};
        pending_state = std::move(candidate);
        pending_focus = std::move(candidate_focus);
        pending_view = std::move(*prepared.value);
        return {};
    }
    CommandOutcome operate(const ActionRequest& action, CommandSource source) {
        if (action.action == "item.name" && !edit) return {};
        if (action.action == "panes.resize" && !split) return {};
        const auto semantic = view.semantic();
        const auto invocation =
            take(view.commands.resolve_action(semantic, action, view.commands.generation(), {}, source, ++sequence));
        const auto result = take(executor.execute(view.commands, invocation, *this, &semantic));
        if (pending_view) {
            require(result.outcome.status == CommandOutcomeStatus::success,
                    "A rejected Inspector command cannot publish");
            state = std::move(*pending_state);
            focus = std::move(*pending_focus);
            accept(std::move(*pending_view));
            pending_state.reset();
            pending_focus.reset();
            pending_view.reset();
            ++executions;
            if (invocation.record().command == "item.rename") focus_id("name");
        }
        return result.outcome;
    }
    void action(std::string_view id, CommandSource source = CommandSource::keyboard) {
        const auto request = take(request_semantic_action(view.semantic(), *view.tree->find(id), "activate"));
        const auto outcome = operate(request, source);
        checked(outcome);
    }
    void refresh(bool reveal = false) {
        auto next = state;
        auto candidate_focus = focus;
        auto candidate = take(stage(next, viewport, offsets, candidate_focus, reveal));
        state = std::move(next);
        focus = std::move(candidate_focus);
        accept(std::move(candidate));
    }
    void resize(Size size) {
        auto next = state;
        auto candidate_focus = focus;
        auto candidate = take(stage(next, size, offsets, candidate_focus, true));
        viewport = size;
        state = std::move(next);
        focus = std::move(candidate_focus);
        accept(std::move(candidate));
    }
    void input(InputEvent event, CommandSource source = CommandSource::keyboard) {
        const auto diagnostics = validate(event);
        require(diagnostics.empty(), "Invalid normalized Inspector input");
        auto focused = take(focus.refresh(view.hit())).focused;
        auto transition = take(context_menu.dispatch(view.hit(), event, focused));
        if (!transition.consumed && !transition.dismiss) transition = take(menu.dispatch(view.hit(), event, focused));
        if (transition.focus) (void)take(focus.focus(view.hit(), *transition.focus));
        if (transition.open_at || transition.dismiss) {
            state.menu = transition.open_at ? OpenMenu::context : OpenMenu::none;
            if (transition.open_at) state.position = *transition.open_at;
            pointer.reset();
            slider_pointer.reset();
            refresh();
        }
        if (transition.consumed) {
            observe();
            return;
        }
        const auto adjustment = take(splitter.dispatch(view.hit(), event, take(focus.refresh(view.hit())).focused));
        if (adjustment.handled) {
            slider_pointer.reset();
            pointer.reset();
            if (adjustment.focus) (void)take(focus.focus(view.hit(), *adjustment.focus));
            if (adjustment.action) {
                split = adjustment.size;
                checked(operate(*adjustment.action, source));
                split.reset();
            }
            observe();
            return;
        }
        if (const auto* cancel = std::get_if<PointerCancel>(&event.data); cancel && slider_pointer == cancel->pointer) {
            slider_pointer.reset();
            return;
        }
        if (const auto* move = std::get_if<PointerMove>(&event.data); move && slider_pointer == move->pointer) {
            set_amount(amount_at(move->position), source);
            return;
        }
        if (const auto* up = std::get_if<PointerUp>(&event.data);
            up && slider_pointer == up->pointer && up->button == PointerButton::primary) {
            slider_pointer.reset();
            set_amount(amount_at(up->position), source);
            return;
        }
        if (const auto* down = std::get_if<PointerDown>(&event.data); down && down->button == PointerButton::primary) {
            const auto target = take(hit_test(view.hit(), down->position)).target;
            if (target && (target == view.tree->find("amount-track") || target == view.tree->find("amount-fill") ||
                           target == view.tree->find("amount-thumb"))) {
                pointer.reset();
                (void)take(focus.focus(view.hit(), *view.tree->find("amount")));
                slider_pointer = down->pointer;
                const int value = amount_at(down->position);
                if (value == state.item().amount)
                    refresh(true);
                else
                    set_amount(value, source);
                return;
            }
        }
        if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
            auto requested = offsets;
            store_offsets(requested, view, take(route_scroll(view.hit(), *scroll)).updates);
            auto next = state;
            auto candidate_focus = focus;
            auto candidate = take(stage(next, viewport, requested, candidate_focus, false));
            slider_pointer.reset();
            state = std::move(next);
            focus = std::move(candidate_focus);
            accept(std::move(candidate));
            return;
        }
        if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
            std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
            const auto dispatched = take(pointer.dispatch(view.hit(), event));
            if (std::holds_alternative<PointerDown>(event.data))
                for (const auto& state : dispatched.pointers)
                    if (state.pressed) {
                        auto owner = *state.pressed;
                        const auto& id = *view.tree->get(owner)->id;
                        if (id.starts_with("hierarchy-toggle-"))
                            owner = *view.tree->find("hierarchy-item-" + id.substr(17));
                        (void)take(focus.focus(view.hit(), owner));
                    }
            for (const auto& request : dispatched.actions) checked(operate(request, source));
            if (std::holds_alternative<PointerDown>(event.data)) refresh(true);
            return;
        }
        slider_pointer.reset();
        focused = take(focus.refresh(view.hit())).focused;
        std::optional<decltype(TextEditRequest::operation)> operation;
        if (focused && focused == view.tree->find("name")) {
            if (const auto* committed = std::get_if<tessera::TextInput>(&event.data))
                operation = TextEditInsert{committed->text};
            if (const auto* key = std::get_if<KeyDown>(&event.data)) {
                if (key->key == Key::a && key->modifiers == Modifiers{.control = true} && !key->repeat)
                    operation = TextEditSelection{0, state.item().name.text.size()};
                if (!key->modifiers.control && !key->modifiers.alt && !key->modifiers.super) switch (key->key) {
                        case Key::left:
                            operation = TextEditMove{TextMove::left, key->modifiers.shift};
                            break;
                        case Key::right:
                            operation = TextEditMove{TextMove::right, key->modifiers.shift};
                            break;
                        case Key::home:
                            operation = TextEditMove{TextMove::home, key->modifiers.shift};
                            break;
                        case Key::end:
                            operation = TextEditMove{TextMove::end, key->modifiers.shift};
                            break;
                        case Key::backspace:
                            operation = TextEditErase{TextErase::backward};
                            break;
                        case Key::delete_key:
                            operation = TextEditErase{TextErase::forward};
                            break;
                        default:
                            break;
                    }
            }
        }
        if (operation) {
            const auto& name = state.item().name;
            edit = TextEditRequest{name.owner, name.revision, std::move(*operation)};
            const auto outcome =
                operate(take(request_semantic_action(view.semantic(), *view.tree->find("name"), "activate")), source);
            edit.reset();
            checked(outcome);
            return;
        }
        if (focused && focused == view.tree->find("amount")) {
            std::optional<int> value;
            if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
                if (key->key == Key::left || key->key == Key::down) value = state.item().amount - 5;
                if (key->key == Key::right || key->key == Key::up) value = state.item().amount + 5;
                if (key->key == Key::home) value = 0;
                if (key->key == Key::end) value = 100;
            }
            if (const auto* navigation = std::get_if<Navigate>(&event.data))
                value =
                    state.item().amount +
                    ((navigation->direction == Direction::left || navigation->direction == Direction::down) ? -5 : 5);
            if (value) {
                set_amount(std::clamp(*value, 0, 100), source);
                return;
            }
        }
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->key == Key::tab)
            event.data =
                key->modifiers.shift ? decltype(event.data){FocusPrevious{}} : decltype(event.data){FocusNext{}};
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
            switch (key->key) {
                case Key::left:
                    event.data = Navigate{Direction::left};
                    break;
                case Key::right:
                    event.data = Navigate{Direction::right};
                    break;
                case Key::up:
                    event.data = Navigate{Direction::up};
                    break;
                case Key::down:
                    event.data = Navigate{Direction::down};
                    break;
                default:
                    break;
            }
        }
        if (std::holds_alternative<tessera::TextInput>(event.data)) return;
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->key != Key::home && key->key != Key::end)
            return;
        const auto dispatched = take(focus.dispatch(view.hit(), event));
        for (const auto& request : dispatched.actions) checked(operate(request, source));
        refresh(true);
    }
    void focus_id(std::string_view id) {
        (void)take(focus.focus(view.hit(), *view.tree->find(id)));
        refresh(true);
    }
    std::string observed_name(std::string id) const {
        const auto target = take(resolve_target(view.inspection, AuthorIdTarget{std::move(id)}));
        const auto semantic = view.inspection.elements[target.index].semantic;
        require(semantic.has_value(), "Inspector observation has no semantic entry");
        return view.inspection.semantics.nodes[*semantic].name;
    }
    ReplayReload recorded_view() const { return {view.tree->document(), view.styles}; }
    CommandOutcome try_edit(TextEditRequest request) {
        edit = std::move(request);
        const auto outcome =
            operate(take(request_semantic_action(view.semantic(), *view.tree->find("name"), "activate")),
                    CommandSource::keyboard);
        edit.reset();
        return outcome;
    }
    bool resizing() const { return splitter.dragging(); }

private:
    ui::Session session;
    CommandExecutor executor;
    PointerDispatcher pointer{PointerSnapshotPolicy::compatible_author_ids};
    SplitPaneDispatcher splitter{"panes"};
    MenuDispatcher menu{"actions-menu"}, context_menu{"item-menu"};
    Offsets offsets;
    std::optional<TextEditRequest> edit;
    std::optional<int> amount;
    std::optional<float> split;
    std::optional<PointerId> slider_pointer;
    std::optional<State> pending_state;
    std::optional<FocusDispatcher> pending_focus;
    std::optional<View> pending_view;
    std::uint64_t generation = 1, sequence = 0;

    static void checked(const CommandOutcome& outcome) {
        if (outcome.status == CommandOutcomeStatus::success) return;
        require(!outcome.diagnostics.empty(), "Inspector command failed without diagnostics");
        (void)take(Result<int>{std::nullopt, outcome.diagnostics});
    }
    static void store_offsets(Offsets& offsets, const View& view, const std::vector<ScrollUpdate>& updates) {
        for (const auto& update : updates) {
            const auto& id = view.tree->get(update.container)->id;
            require(id.has_value(), "Inspector scroll owners need stable author IDs");
            offsets[*id] = update.offset;
        }
    }
    Result<View> prepare(const State& state, Size viewport, const Offsets& offsets, const CommandSnapshot& commands) {
        auto authored = session.lower(Form(state, viewport), context);
        if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
        auto document = commands.apply_eligibility(authored.value->document, context);
        if (!document) return {std::nullopt, std::move(document.diagnostics)};
        auto tree = UiTree::create(*document.value, context);
        if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
        const StyleSheet sheet{{{StyleSelector::of_class("control"),
                                 {.padding = Edges{4, 4, 4, 4}, .background = Color{.8f, .9f, 1, 1}}}}};
        auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
        if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
        std::vector<Point> requested((*tree.value)->size());
        for (const auto& [id, offset] : offsets)
            if (const auto node = (*tree.value)->find(id)) requested[node->index] = offset;
        auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text, requested});
        if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
        auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
        if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
        return {View{commands,
                     std::move(*tree.value),
                     std::move(*styles.value),
                     std::move(*layout.value),
                     {},
                     std::move(*paint.value)},
                {}};
    }
    Result<View> stage(State& candidate, Size viewport, Offsets requested, FocusDispatcher& candidate_focus,
                       bool reveal) {
        const auto commands =
            take(CommandSnapshot::publish(registry, ++generation, CommandMode::production, eligibility));
        auto result = prepare(candidate, viewport, requested, commands);
        if (!result) return result;
        const auto geometry = take(split_pane_geometry(result.value->hit(), "panes"));
        require(geometry.has_value(), "Inspector splitter is missing");
        if (candidate.split != geometry->size) {
            candidate.split = geometry->size;
            result = prepare(candidate, viewport, requested, commands);
            if (!result) return result;
        }
        const auto focused = take(candidate_focus.refresh(result.value->hit())).focused;
        if (reveal && focused) {
            const auto updates = take(scroll_into_view(result.value->hit(), *focused));
            if (!updates.empty()) {
                store_offsets(requested, *result.value, updates);
                result = prepare(candidate, viewport, requested, commands);
                if (!result) return result;
            }
        }
        const auto target = take(candidate_focus.refresh(result.value->hit())).focused;
        auto inspection = capture_inspection(
            {result.value->tree.get(), result.value->styles, &result.value->layout, nullptr, target});
        if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
        result.value->inspection = std::move(*inspection.value);
        return result;
    }
    void accept(View candidate) {
        view = std::move(candidate);
        for (const auto& box : view.layout.boxes)
            if (box.scroll) offsets[*view.tree->get(box.node)->id] = box.scroll->offset;
        (void)take(pointer.refresh(view.hit()));
        (void)take(splitter.refresh(view.hit()));
        (void)take(focus.refresh(view.hit()));
        bool dismissed = false;
        for (const auto& transition : {take(menu.refresh(view.hit())), take(context_menu.refresh(view.hit()))}) {
            if (transition.focus) (void)take(focus.focus(view.hit(), *transition.focus));
            dismissed = dismissed || transition.dismiss;
        }
        if (dismissed) {
            state.menu = OpenMenu::none;
            pointer.reset();
            slider_pointer.reset();
            refresh();
            return;
        }
        observe();
        session.publish();
    }
    void observe() {
        view.inspection = take(capture_inspection(
            {view.tree.get(), view.styles, &view.layout, nullptr, take(focus.refresh(view.hit())).focused}));
    }
    int amount_at(Point point) const {
        const auto& track = view.box("amount-track").border_box;
        require(track.size.width > 8, "Inspector Slider has no travel");
        return take(ui::slider_value_at(amount_range, (point.x - track.origin.x - 4) / (track.size.width - 8)));
    }
    void set_amount(int value, CommandSource source) {
        if (value == state.item().amount) return;
        amount = value;
        const auto outcome =
            operate(take(request_semantic_action(view.semantic(), *view.tree->find("amount"), "activate")), source);
        amount.reset();
        checked(outcome);
    }
};

void smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    auto click = [&](std::string_view id) {
        const auto point = app.view.center(id);
        input(PointerDown{{1}, point}, CommandSource::pointer);
        input(PointerUp{{1}, point}, CommandSource::pointer);
    };
    ReplayRecording recording;
    recording.context = app.context;
    recording.viewport = app.viewport;
    recording.document = app.view.tree->document();
    recording.styles = app.view.styles;
    auto replay = take(ReplaySession::open(recording, app.text));
    auto record = [&] { (void)take(replay.apply(app.recorded_view())); };
    click("hierarchy-item-cube");
    app.focus_id("name");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"Edited cube"});
    const auto edited_name = app.state.item().name;
    require(app.observed_name("hierarchy-item-cube") == "Edited cube" &&
                app.observed_name("name").find("Edited cube") != std::string::npos,
            "Accepted name must agree between Hierarchy and Inspector");
    click("enabled");
    app.focus_id("amount");
    input(KeyDown{Key::right});
    const auto track = app.view.box("amount-track").border_box;
    const Point amount_point{track.origin.x + 4 + .75f * (track.size.width - 8),
                             track.origin.y + track.size.height / 2};
    input(PointerDown{{1}, amount_point}, CommandSource::pointer);
    input(PointerUp{{1}, amount_point}, CommandSource::pointer);
    require(app.state.item().amount == 75, "Pointer Slider input must accept the requested stop");
    for (int i = 0; i < 4; ++i) input(KeyDown{Key::left});
    const auto cube = app.state.item();
    require(!cube.enabled && cube.amount == 55, "Checkbox and numeric edits must be accepted");
    record();
    (void)take(replay.apply(ReplaySemanticAction{AuthorIdTarget{"hierarchy-item-sphere"}}));
    click("hierarchy-item-sphere");
    require(app.state.selected == "sphere" && app.state.item().name.text == "Sphere",
            "Selection must show independent values");
    app.focus_id("name");
    input(tessera::TextInput{"!"});
    input(KeyDown{Key::home, {.shift = true}});
    const auto sphere = app.state.item();
    record();
    app.focus_id("hierarchy-item-cube");
    input(Activate{}, CommandSource::gamepad);
    require(app.state.item() == cube && app.state.item().name == edited_name,
            "Returning to an item must retain name, selection, revision, enabled and amount");
    app.action("hierarchy-item-sphere");
    require(app.state.item() == sphere && sphere.name.anchor != sphere.name.caret,
            "Returning to another item must retain its independent noncollapsed byte selection");
    app.action("hierarchy-item-cube");
    const auto stale = app.try_edit({sphere.name.owner, sphere.name.revision, TextEditInsert{"wrong"}});
    require(stale.status == CommandOutcomeStatus::error && app.state.item() == cube,
            "An edit from another selected item must reject without changing values");
    app.text.reject = true;
    const auto accepted_generation = app.view.inspection.generation;
    const auto rejected = app.try_edit({cube.name.owner, cube.name.revision, TextEditInsert{"retry"}});
    require(rejected.status == CommandOutcomeStatus::error && app.state.item() == cube &&
                app.view.inspection.generation == accepted_generation,
            "Rejected presentation must retain the accepted item and inspection");
    app.text.reject = false;
    const auto retry = app.try_edit({cube.name.owner, cube.name.revision, TextEditInsert{"!"}});
    require(retry.status == CommandOutcomeStatus::success && app.state.item().name.text == "Edited cube!",
            "Rejected name edits must remain retryable");
    const auto saved = app.state.item();
    app.focus_id("hierarchy-item-scene");
    input(Navigate{Direction::left}, CommandSource::gamepad);
    require(!app.state.expanded && app.state.selected == "cube" && app.state.item() == saved,
            "Collapse must retain a selected descendant and its edits");
    input(Navigate{Direction::right}, CommandSource::gamepad);
    app.resize({520, 220});
    app.focus_id("amount");
    input(Scroll{app.view.center("inspector"), {0, -1000}}, CommandSource::pointer);
    require(app.view.box("inspector").scroll->offset.y == 0, "Inspector must scroll back to the top");
    input(Scroll{app.view.center("inspector"), {0, 80}}, CommandSource::pointer);
    require(app.view.box("inspector").scroll->offset.y > 0, "Inspector wheel input must change the accepted offset");
    input(Scroll{app.view.center("hierarchy"), {0, 1000}}, CommandSource::pointer);
    require(app.view.box("hierarchy").scroll->offset.y > 0 && app.state.selected == "cube",
            "Hierarchy scrolling must retain the selected item");
    input(Scroll{app.view.center("hierarchy"), {0, -1000}}, CommandSource::pointer);
    app.focus_id("panes-handle");
    const auto split_before = app.state.split;
    input(KeyDown{Key::left});
    require(app.state.split == split_before - 8, "Keyboard pane resize must accept the requested step");
    app.resize({960, 360});
    const auto pointer_split_before = app.state.split;
    const auto handle = app.view.center("panes-handle");
    input(PointerDown{{1}, handle}, CommandSource::pointer);
    input(PointerMove{{1}, {handle.x + 30, handle.y}}, CommandSource::pointer);
    input(PointerUp{{1}, {handle.x + 30, handle.y}}, CommandSource::pointer);
    require(app.state.split == pointer_split_before + 30, "Pointer pane resize must retain the drag distance");
    require(app.state.item() == saved && app.state.selected == "cube",
            "Scroll and pane/window resizing must retain edits");
    click("actions");
    click("actions-menu-item-enabled");
    require(app.state.item().enabled && app.state.menu == OpenMenu::none,
            "Menu must route the ordinary toggle command");
    app.focus_id("selected");
    input(KeyDown{Key::f10, {.shift = true}});
    require(app.state.menu == OpenMenu::context, "Shift+F10 must open selected-item actions");
    input(KeyDown{Key::escape});
    require(app.state.menu == OpenMenu::none, "Escape must dismiss without changing item values");
    app.focus_id("selected");
    const auto position = app.view.center("selected");
    input(PointerDown{{1}, position, PointerButton::secondary}, CommandSource::pointer);
    input(PointerUp{{1}, position, PointerButton::secondary}, CommandSource::pointer);
    require(app.state.menu == OpenMenu::context, "Context action must open at the selected item");
    click("item-menu-item-rename");
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("name"),
            "Rename command must focus the retained name editor");
    app.focus_id("selected");
    input(KeyDown{Key::f10, {.shift = true}});
    app.resize({520, 220});
    const auto inspector = app.view.box("inspector").border_box;
    input(Scroll{{inspector.origin.x + inspector.size.width - 4, inspector.origin.y + inspector.size.height - 4},
                 {0, 1000}},
          CommandSource::pointer);
    require(app.state.menu == OpenMenu::none, "Scrolling the context anchor away must dismiss its menu");
    app.resize({960, 360});
    app.focus_id("name");
    record();
    const auto target = take(resolve_target(app.view.inspection, AuthorIdTarget{"hierarchy-item-cube"}));
    const auto semantic = app.view.inspection.elements[target.index].semantic;
    require(semantic && app.view.inspection.semantics.nodes[*semantic].selected == true &&
                app.observed_name("enabled").find("[x]") != std::string::npos &&
                app.observed_name("amount").find("55") != std::string::npos,
            "Shared inspection and semantics must expose selection and edited properties");
    const auto loaded = take(load_replay(take(save_replay(replay.recording()))));
    const auto output = take(play_replay(loaded, app.text));
    require(compare_replay(replay.output(), output).empty() && output.actions.size() == 1 &&
                output.actions.front().action == "select.sphere",
            "Recorded host views and selection action must reproduce through ordinary replay");
    const auto& nodes = output.semantics.back().nodes;
    require(std::any_of(nodes.begin(), nodes.end(),
                        [](const auto& node) {
                            return node.id == "hierarchy-item-cube" && node.selected == true &&
                                   node.name == "Edited cube!";
                        }),
            "Replay must retain the selected item's edited display name");
    for (const auto& [id, value] :
         std::vector<std::pair<std::string, std::string>>{{"enabled", "[x]"}, {"amount", "55"}})
        require(
            std::any_of(nodes.begin(), nodes.end(),
                        [&](const auto& node) { return node.id == id && node.name.find(value) != std::string::npos; }),
            "Replay must reproduce the accepted enabled and numeric properties");
    std::cout
        << "Inspector: select/edit/return, independent items, rejected staging, input, menus, inspection and replay passed.\n";
}

#ifdef _WIN32
int native(bool smoke) {
    App app;
    settings_native::Glyphs glyphs;
    GlyphCache cache(glyphs);
    CpuFrameCapture frames(&cache);
    settings_native::Window window;
    window.title = L"Tessera Hierarchy / Inspector - select an item, edit its properties";
    window.input = [&](InputEvent event, CommandSource source) { app.input(std::move(event), source); };
    window.capture = [&] {
        return take(frames.capture({app.view.commands.generation(), app.viewport, 1,
                                    take(capture_extent(app.viewport, 1)), &app.view.paint, 0us}));
    };
    window.focused = [&]() -> std::optional<Rect> {
        const auto focused = take(app.focus.refresh(app.view.hit())).focused;
        if (!focused || !take(scroll_into_view(app.view.hit(), *focused)).empty()) return {};
        return app.view.box(*app.view.tree->get(*focused)->id).border_box;
    };
    window.editing = [&] { return take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("name"); };
    window.resized = [&](Size viewport) { app.resize(viewport); };
    window.resize_cursor = [&](Point point) {
        const auto geometry = take(split_pane_geometry(app.view.hit(), "panes"));
        return geometry && geometry->enabled &&
               (app.resizing() || take(hit_test(app.view.hit(), point)).target == geometry->handle);
    };
    auto verify = [&](settings_native::Window& host) {
        auto click = [&](const char* id) {
            const auto point = host.physical(app.view.center(id));
            host.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
            host.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        };
        auto seek = [&](const char* id) {
            for (int i = 0; i < 32; ++i) {
                if (take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id)) return;
                host.message(WM_KEYDOWN, VK_TAB, 1);
            }
            throw std::runtime_error("Native Tab cannot reach Inspector control");
        };
        require(host.presentations > 0, "Inspector window must present");
        click("hierarchy-item-cube");
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.selected == "sphere", "Native arrow/Activate must select the next hierarchy item");
        host.message(WM_KEYDOWN, VK_UP, 1);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.selected == "cube", "Native hierarchy navigation must return to the previous item");
        seek("name");
        host.message(WM_KEYDOWN, VK_END, 1);
        host.message(WM_CHAR, '!');
        seek("enabled");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        seek("amount");
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        const auto cube = app.state.item();
        require(cube.name.text == "Cube!" && !cube.enabled && cube.amount == 55, "Native fields must accept edits");
        click("hierarchy-item-sphere");
        click("hierarchy-item-cube");
        require(app.state.item() == cube, "Native select/edit/return must preserve per-item state");
        const auto selected = host.physical(app.view.center("selected"));
        host.message(WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(selected.x, selected.y));
        host.message(WM_RBUTTONUP, 0, MAKELPARAM(selected.x, selected.y));
        require(app.state.menu == OpenMenu::context, "Native right-click must open selected-item actions");
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(window.editing() && app.state.menu == OpenMenu::none,
                "Native context Rename must focus the name editor");
        const auto handle = host.physical(app.view.center("panes-handle"));
        host.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(handle.x, handle.y));
        require(GetCapture() == host.handle && app.resizing(), "Native splitter must capture");
        host.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(handle.x + 32, handle.y));
        host.message(WM_LBUTTONUP, 0, MAKELPARAM(handle.x + 32, handle.y));
        require(SetWindowPos(host.handle, nullptr, 0, 0, 720, 340, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Native Inspector resize failed");
        require(app.state.item() == cube && app.observed_name("hierarchy-item-cube") == "Cube!" &&
                    host.presentations > 5,
                "Native resize and presentation must preserve accepted edits");
        std::cout << "Inspector native injected-message smoke passed: " << host.presentations << " presentations.\n";
    };
    if (smoke) return window.run(verify);
    return window.run();
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            smoke();
            return 0;
        }
        require(argc == 2 && (std::string_view(argv[1]) == "--native" || std::string_view(argv[1]) == "--native-smoke"),
                "Usage: tessera_inspector_hierarchy [--native | --native-smoke]");
#ifdef _WIN32
        return native(std::string_view(argv[1]) == "--native-smoke");
#else
        throw std::runtime_error("The Inspector native host currently requires Windows");
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
