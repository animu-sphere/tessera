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
const ValidationContext actions{{"inspector.name", "inspector.category"}};

ui::Element Inspector(ui::Value<TextEditState> name, ui::Value<TextEditState> category) {
    const auto input_props = [](const char* id) {
        return ui::TextInputProps{
            .node = {.id = id, .style = {.padding = Edges{4, 4, 4, 4}, .background = Color{0.8f, 0.9f, 1, 1}}}};
    };
    return ui::Grid(
        {ui::Text("Display name", {.id = "name-caption"}),
         ui::TextInput("Name", name, "inspector.name", input_props("name")),
         ui::Text("Kind", {.id = "category-caption"}),
         ui::TextInput("Group", category, "inspector.category", input_props("category"))},
        {.id = "inspector",
         .style = {.overflow = Overflow::clip,
                   .padding = Edges{8, 8, 8, 8},
                   .text = {.size = 20.f},
                   .grid_columns = std::vector<GridTrack>{GridTrack{.maximum = 80}, GridTrack::fraction(1, 32)},
                   .grid_rows = std::vector<GridTrack>{GridTrack{}, GridTrack{}},
                   .column_gap = 8.f,
                   .row_gap = 8.f}});
}
struct View {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    Rect bounds(NodeHandle node) const {
        for (const auto& box : layout.boxes)
            if (box.node == node) return box.border_box;
        throw std::runtime_error("Inspector node has no layout");
    }
    Rect bounds(const char* id) const {
        const auto node = tree->find(id);
        require(node.has_value(), "Inspector control is missing");
        return bounds(*node);
    }
};
Result<View> prepare(ui::Session& session, const ui::Element& form, Size viewport, TextShaper& text) {
    auto authored = session.lower(form, actions);
    if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
    auto tree = UiTree::create(authored.value->document, actions);
    if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
    const StyleSheet sheet;
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text});
    if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
    return {View{std::move(*tree.value), std::move(*styles.value), std::move(*layout.value)}, {}};
}
CommandSnapshot commands() {
    const auto registry = take(
        CommandRegistry::create({{"inspector.name", "Edit display name"}, {"inspector.category", "Edit category"}},
                                {{"inspector.name", "inspector.name"}, {"inspector.category", "inspector.category"}}));
    return take(CommandSnapshot::publish(registry, 1, CommandMode::production,
                                         {{"inspector.name", {true}}, {"inspector.category", {true}}}));
}
struct TextService final : TextShaper {
    PlaceholderTextShaper delegate;
    bool reject = false;
    Result<TextMetrics> measure(std::string_view text, const TextStyle& style,
                                const TextConstraints& constraints) override {
        if (reject)
            return {std::nullopt,
                    {{"inspector_measurement", Severity::error, "/text",
                      "Injected text-service rejection; retain the accepted Inspector and retry."}}};
        return delegate.measure(text, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view text, const TextStyle& style, const TextConstraints& constraints) override {
        return delegate.shape(text, style, constraints);
    }
};

class Host final : public CommandHost {
public:
    ui::Session session;
    TextEditState name = take(make_text_edit_state(1, "Ada"));
    TextEditState category = take(make_text_edit_state(2, "Tools"));
    Signal<TextEditState> name_value = session.state("name", name);
    Signal<TextEditState> category_value = session.state("category", category);
    ui::Element form = Inspector(name_value, category_value);
    TextService text;
    Size viewport{320, 400};
    View view = take(prepare(session, form, viewport, text));
    FocusDispatcher focus;
    PointerDispatcher pointer{PointerSnapshotPolicy::compatible_author_ids};

    Host() { session.publish(); }

    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        const bool editing_name = invocation.command == "inspector.name";
        require(editing_name || invocation.command == "inspector.category", "Unknown Inspector command");
        if (!edit_) return {}; // Pointer/gamepad activation keeps focus without inserting text.
        const auto& accepted = editing_name ? name : category;
        auto candidate = edit_text(accepted, *edit_);
        if (!candidate) return {CommandOutcomeStatus::error, {}, std::move(candidate.diagnostics)};
        const auto& observation = editing_name ? name_value : category_value;
        session.update([&] { observation.write(*candidate.value); });
        auto presentation = prepare(session, form, viewport, text);
        if (!presentation) {
            session.update([&] { observation.write(accepted); });
            return {CommandOutcomeStatus::error, {}, std::move(presentation.diagnostics)};
        }
        staged_name_ = editing_name;
        staged_edit_ = std::move(*candidate.value);
        staged_view_ = std::move(*presentation.value);
        return {};
    }

    CommandOutcome run(const ActionRequest& action, CommandSource source) {
        CommandExecution executed = [&] {
            const auto semantic = view.semantic();
            const auto invocation =
                take(commands_.resolve_action(semantic, action, commands_.generation(), {}, source, ++sequence_));
            return take(executor_.execute(commands_, invocation, *this, &semantic));
        }();
        if (staged_view_) {
            require(executed.outcome.status == CommandOutcomeStatus::success, "A rejected command cannot publish");
            (staged_name_ ? name : category) = std::move(*staged_edit_);
            accept(std::move(*staged_view_));
            staged_view_.reset();
            staged_edit_.reset();
        }
        return std::move(executed.outcome);
    }

    CommandOutcome insert(const InputEvent& event) {
        const auto diagnostics = validate(event);
        if (!diagnostics.empty()) throw std::runtime_error(diagnostics.front().message);
        const auto* committed = std::get_if<tessera::TextInput>(&event.data);
        require(committed != nullptr, "Expected normalized committed text");
        const auto focused = take(focus.refresh(view.hit())).focused;
        require(focused.has_value(), "Text input needs a focused Inspector field");
        const bool editing_name = focused == view.tree->find("name");
        require(editing_name || focused == view.tree->find("category"), "Focus is not an Inspector field");
        const auto& state = editing_name ? name : category;
        edit_ = TextEditRequest{state.owner, state.revision, TextEditInsert{committed->text}};
        auto outcome = run({"activate", editing_name ? "inspector.name" : "inspector.category", *focused},
                           CommandSource::keyboard);
        edit_.reset();
        return outcome;
    }

    void resize(Size size) {
        auto candidate = take(prepare(session, form, size, text));
        viewport = size;
        accept(std::move(candidate));
    }

    void click(Point point) {
        const auto down = take(pointer.dispatch(view.hit(), {1us, PointerDown{{1}, point}}));
        require(down.actions.empty() && down.pointers.size() == 1 && down.pointers[0].pressed,
                "Pointer press must find an input without activating it");
        (void)take(focus.focus(view.hit(), *down.pointers[0].pressed));
        const auto previous_tree = view.tree->root().tree;
        for (int i = 0; i < 3; ++i) accept(take(prepare(session, form, viewport, text)));
        const auto refreshed = take(pointer.refresh(view.hit()));
        require(view.tree->root().tree != previous_tree && refreshed.actions.empty() &&
                    refreshed.pointers.size() == 1 && refreshed.pointers[0].pressed == view.tree->find("name") &&
                    refreshed.pointers[0].active == view.tree->find("name") &&
                    take(focus.refresh(view.hit())).focused == view.tree->find("name"),
                "Inspector publication while held must preserve the current-snapshot press, hover and focus");
        const auto up = take(pointer.dispatch(view.hit(), {2us, PointerUp{{1}, point}}));
        require(up.actions.size() == 1, "Pointer release must route one Inspector action");
        require(run(up.actions[0], CommandSource::pointer).status == CommandOutcomeStatus::success,
                "Inspector pointer activation failed");
        require(take(pointer.dispatch(view.hit(), {2us, PointerUp{{1}, point}})).actions.empty(),
                "Inspector replacement must not duplicate activation");
    }

private:
    CommandSnapshot commands_ = commands();
    CommandExecutor executor_;
    std::uint64_t sequence_ = 0;
    std::optional<TextEditRequest> edit_;
    std::optional<TextEditState> staged_edit_;
    std::optional<View> staged_view_;
    bool staged_name_ = false;

    void accept(View candidate) {
        session.publish();
        view = std::move(candidate);
        (void)take(pointer.refresh(view.hit()));
        (void)take(focus.refresh(view.hit()));
    }
};

void verify_columns(Host& host) {
    const auto first = host.view.bounds("name");
    const auto second = host.view.bounds("category");
    const auto label = host.view.bounds("name-caption");
    require(label.origin.x == 8 && host.view.bounds("category-caption").origin.x == 8 && first.origin.x == 96 &&
                second.origin.x == first.origin.x && second.size.width == first.size.width &&
                first.origin.x + first.size.width == host.viewport.width - 8 &&
                second.origin.y == first.origin.y + first.size.height + 8,
            "Inspector rows must share label/value columns, row heights and gaps");
    const auto metrics = take(host.text.delegate.measure("Display name", host.view.styles[1].text, {80.f}));
    require(metrics.lines == 2 && label.size.height >= metrics.size.height,
            "Wrapped labels must fit their allocated row");
    for (const auto* id : {"name", "category"}) {
        const auto node = *host.view.tree->find(id);
        const auto field = host.view.bounds(node);
        for (const auto child : host.view.tree->children(node)) {
            const auto bounds = host.view.bounds(child);
            require(bounds.origin.x >= field.origin.x && bounds.origin.y >= field.origin.y &&
                        bounds.origin.x + bounds.size.width <= field.origin.x + field.size.width &&
                        bounds.origin.y + bounds.size.height <= field.origin.y + field.size.height,
                    "Wrapped input content must remain within its Grid field bounds");
        }
    }
}
} // namespace

int tessera::examples::run_grid_inspector(const FrameObserver& observer) {
    try {
        Host host;
        auto capture = [&](std::string_view name) {
            if (observer)
                observer(
                    name, host.viewport,
                    take(build_paint_list({host.view.tree.get(), host.view.styles, &host.view.layout, &host.text})));
        };
        verify_columns(host);
        capture("grid-initial");
        const float wide_height = host.view.bounds("name").size.height;
        auto focused = take(host.focus.dispatch(host.view.hit(), {1us, FocusNext{}}));
        require(focused.focused == host.view.tree->find("name"), "Keyboard must reach the first property");
        require(host.insert({2us, tessera::TextInput{"!"}}).status == CommandOutcomeStatus::success &&
                    host.name.text == "Ada!" && host.name.revision == 2 && host.category.text == "Tools",
                "Editing a property must retain its peer field");
        capture("grid-edited");
        host.resize({168, 400});
        verify_columns(host);
        require(host.view.bounds("name").size.height > wide_height &&
                    take(host.focus.refresh(host.view.hit())).focused == host.view.tree->find("name") &&
                    host.name.text == "Ada!" && host.category.text == "Tools",
                "Narrow resize must remeasure rows while retaining edits and focus");
        capture("grid-narrow");
        const auto field = host.view.bounds("name");
        host.click({field.origin.x + 5, field.origin.y + 5});
        focused = take(host.focus.dispatch(host.view.hit(), {3us, Navigate{Direction::down}}));
        require(focused.focused == host.view.tree->find("category"), "Gamepad must reach the next property");
        const auto activated = take(host.focus.dispatch(host.view.hit(), {4us, Activate{}}));
        require(activated.actions.size() == 1 &&
                    host.run(activated.actions[0], CommandSource::gamepad).status == CommandOutcomeStatus::success,
                "Gamepad activation must use the ordinary Inspector command");

        const auto accepted_tree = host.view.tree->root().tree;
        const auto accepted_name = host.name;
        const auto accepted_category = host.category;
        host.text.reject = true;
        const auto rejected = host.insert({5us, tessera::TextInput{"X"}});
        require(rejected.status == CommandOutcomeStatus::error && !rejected.diagnostics.empty() &&
                    rejected.diagnostics[0].code == "inspector_measurement" &&
                    host.view.tree->root().tree == accepted_tree && host.name == accepted_name &&
                    host.category == accepted_category,
                "Rejected measurement must retain the accepted fields and presentation with diagnostics");
        host.text.reject = false;
        require(host.insert({6us, tessera::TextInput{"?"}}).status == CommandOutcomeStatus::success &&
                    host.category.text == "Tools?" && host.category.revision == accepted_category.revision + 1 &&
                    host.name == accepted_name,
                "A fresh edit must retry without retaining the rejected text");
        host.resize({320, 400});
        verify_columns(host);
        require(take(host.focus.refresh(host.view.hit())).focused == host.view.tree->find("category"),
                "Resize must preserve the edited property's focus by author ID");
        capture("grid-restored");
        std::cout
            << "Grid Inspector: shared columns, narrow wrapping, keyboard edit, pointer/gamepad and rejection passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#ifndef TESSERA_EXAMPLE_LIBRARY
int main() { return tessera::examples::run_grid_inspector({}); }
#endif
