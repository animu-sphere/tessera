#include <tessera/input/split_pane.hpp>
#include <tessera/ui/authoring.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include "../check.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

using namespace tessera;
namespace ui = tessera::authoring;
using tessera::test::check;

namespace {
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.diagnostics.front().code + ": " + result.diagnostics.front().message);
    return std::move(*result.value);
}
void equal(float actual, float expected, const char* message) {
    check(std::abs(actual - expected) < .001f, message);
}
const ValidationContext actions{{"resize", "other"}};
struct Fixture {
    ui::Session session;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    PlaceholderTextShaper text;
    Size viewport{400, 400};
    FlexDirection direction;
    float size = 140;
    bool disabled = false;
    std::string action = "resize";
    explicit Fixture(FlexDirection axis) : direction(axis) { build(); }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    SplitPaneGeometry geometry() const {
        const auto result = take(split_pane_geometry(hit(), "panes"));
        check(result.has_value(), "Missing splitter geometry");
        return *result;
    }
    void build(bool present = true, bool nested = false) {
        auto first = ui::Column({}, {.id = "content", .style = {.background = Color{1, 0, 0, 1}}});
        auto second = nested ? ui::VerticalSplitPane(ui::Column({}), ui::Column({}), 100, "other",
            {.node = {.id = "nested"}, .limits = {40, 40, 6}}) :
            ui::Column({}, {.style = {.background = Color{0, 0, 1, 1}}});
        const ui::SplitPaneProps props{.node = {.id = "panes", .style = {.padding = Edges{12, 12, 12, 12}}},
            .limits = {80, 100, 8}, .disabled = disabled};
        auto element = !present ? ui::Column({}) : direction == FlexDirection::row ?
            ui::HorizontalSplitPane(std::move(first), std::move(second), size, action, props) :
            ui::VerticalSplitPane(std::move(first), std::move(second), size, action, props);
        auto authored = take(session.lower(element, actions));
        tree = take(UiTree::create(authored.document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, viewport, &text}));
        session.publish();
    }
    Point point(float size_delta = 0) const {
        const auto view = geometry();
        auto point = view.bounds.origin;
        if (direction == FlexDirection::row) { point.x += 3 + size_delta; point.y += 20; }
        else { point.y += 3 + size_delta; point.x += 20; }
        return point;
    }
    void apply(const SplitPaneDispatchResult& result) {
        if (result.size && result.action) { size = *result.size; build(); }
    }
};

void validation() {
    ui::Session session;
    auto panes = [&](float size, ui::SplitPaneProps props) {
        return session.lower(ui::HorizontalSplitPane(ui::Column({}), ui::Column({}), size, "resize", props), actions);
    };
    check(!panes(100, {}), "Missing SplitPane ID accepted");
    const SourceLocation source{"fixture://split.cpp", {1, 4}};
    ui::SplitPaneProps props{.node = {.id = "panes", .source = source}};
    auto invalid = panes(-1, props);
    check(!invalid && invalid.diagnostics.front().source &&
        invalid.diagnostics.front().source->location == source, "Invalid split needs caller-source diagnostics");
    check(!panes(std::numeric_limits<float>::infinity(), props), "Infinite split accepted");
    props.limits.handle_size = 0;
    check(!panes(100, props), "Zero handle accepted");
    props.limits = {-1, 80, 8};
    check(!panes(100, props), "Negative minimum accepted");
    props.limits = {80, std::numeric_limits<float>::quiet_NaN(), 8};
    check(!panes(100, props), "NaN minimum accepted");
    equal(take(ui::split_pane_size_at({80, 100, 8}, 400, -50)), 80, "Lower clamp differs");
    equal(take(ui::split_pane_size_at({80, 100, 8}, 400, 900)), 292, "Upper clamp differs");
    equal(take(ui::split_pane_size_at({80, 100, 8}, 100, 900)), 80, "Infeasible minima policy differs");
    check(!ui::split_pane_size_at({}, -1, 100) &&
        !ui::split_pane_size_at({}, 400, std::numeric_limits<float>::quiet_NaN()), "Invalid clamp input accepted");
}

void geometry_and_semantics(FlexDirection direction) {
    Fixture f(direction);
    auto view = f.geometry();
    equal(view.size, 140, "Initial logical split differs");
    equal(view.maximum, 268, "Parent padding must reduce available travel");
    equal(view.minimum, 80, "First minimum differs");
    check(view.enabled && view.direction == direction, "Orientation/eligibility differs");
    check(take(hit_test(f.hit(), f.point())).target == view.handle, "Resolved handle is not the hit target");
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    check(std::any_of(paint.commands.begin(), paint.commands.end(), [&](const auto& command) {
        const auto* rect = std::get_if<DrawRect>(&command);
        return rect && rect->rect == view.bounds;
    }), "Drawing must use the same handle rectangle as input");
    CpuFrameCapture frames;
    auto capture = [&] { return take(frames.capture({1, f.viewport, 1,
        take(capture_extent(f.viewport, 1)), &paint, std::chrono::microseconds{0}})); };
    check(capture() == capture(), "CPU fixture is not deterministic");
    const auto snapshot = take(capture_semantics(f.semantic(), 7));
    const auto record = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
        [&](const auto& node) { return node.id == "panes-handle"; });
    check(record != snapshot.nodes.end() && record->role == SemanticRole::separator &&
        record->name.find("140") != std::string::npos && record->actions.size() == 1,
        "Adjustable separator name/action projection differs");
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot, "Separator snapshot round trip failed");
    check(take(load_document(take(save_document(f.tree->document(), actions)), actions)) == f.tree->document(),
        "Separator UI marker round trip failed");
    const auto action = take(request_semantic_action(f.semantic(), 7, {7, view.handle.index}));
    check(action.action == "resize", "Semantic adjustment missed the ordinary action");
    check(!request_semantic_action(f.semantic(), 8, {7, view.handle.index}), "Stale generation was accepted");
    ReplayRecording recording;
    recording.context = actions;
    recording.document = f.tree->document();
    recording.styles = f.styles;
    recording.viewport = f.viewport;
    recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::separator, record->name}},
        ReplaySemanticAction{SemanticTarget{SemanticRole::separator, record->name}}};
    check(take(load_replay(take(save_replay(recording)))) == recording, "Separator replay target round trip failed");
    const auto replay = take(play_replay(recording, f.text));
    check(replay.actions.size() == 1 && replay.actions.front().action == "resize" &&
        compare_replay(replay, take(play_replay(recording, f.text))).empty(),
        "Separator replay invocation or deterministic observations differ");
    f.disabled = true; f.build();
    check(!f.geometry().enabled && !request_semantic_action(f.semantic(), f.geometry().handle, "activate"),
        "Disabled separator exposed adjustment");
}

void drag_and_replacement(FlexDirection direction) {
    Fixture f(direction);
    SplitPaneDispatcher dispatcher("panes");
    auto send = [&](auto event) {
        return take(dispatcher.dispatch(f.hit(), {std::chrono::microseconds{0}, event}));
    };
    const auto start = f.point();
    auto down = send(PointerDown{{1}, start});
    check(down.handled && down.focus == f.geometry().handle && !down.action && dispatcher.dragging(),
        "Press must focus/capture without jumping or executing a command");
    check(!send(PointerDown{{2}, start}).handled && !send(PointerMove{{2}, f.point(30)}).handled &&
        !send(PointerUp{{1}, f.point(30), PointerButton::secondary}).handled && dispatcher.dragging(),
        "Foreign pointer or secondary release changed capture");
    auto move = send(PointerMove{{1}, f.point(30)});
    equal(*move.size, 170, "Grab offset or parent logical mapping differs");
    check(move.action->action == "resize", "Drag must request the ordinary host command");
    const auto old_handle = f.geometry().handle;
    f.apply(move);
    equal(*take(dispatcher.refresh(f.hit())).size, 170, "Replacement lost accepted split");
    check(dispatcher.dragging() && old_handle != f.geometry().handle, "Capture must survive replacement without a handle");
    move = send(PointerMove{{1}, f.point(1000)});
    equal(*move.size, 268, "Drag upper clamp differs"); f.apply(move);
    move = send(PointerMove{{1}, f.point(-1000)});
    equal(*move.size, 80, "Drag lower clamp differs"); f.apply(move);
    check(send(PointerCancel{{1}}).handled && !dispatcher.dragging(), "Pointer cancel failed");
    check(!send(PointerUp{{1}, start}).action && !send(PointerMove{{1}, start}).action,
        "Cancellation executed a deferred release");
    (void)send(PointerDown{{1}, f.point()});
    auto up = send(PointerUp{{1}, f.point(20)});
    equal(*up.size, 100, "Release final position differs");
    check(!dispatcher.dragging(), "Release retained capture"); f.apply(up);
    (void)send(PointerDown{{1}, f.point()});
    check(send(Cancel{}).handled && !dispatcher.dragging(), "Logical cancellation failed");
    (void)send(PointerDown{{1}, f.point()});
    auto invalid = dispatcher.dispatch(f.hit(), {std::chrono::microseconds{0},
        PointerMove{{1}, {std::numeric_limits<float>::quiet_NaN(), 0}}});
    check(!invalid && dispatcher.dragging(), "Invalid input mutated accepted capture");
    f.action = "other"; f.build();
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Changed binding retained an old drag");
    (void)send(PointerDown{{1}, f.point()});
    f.build(false); (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Handle removal retained capture");
    f.build(); check(!send(PointerUp{{1}, start}).action, "Reinserted handle accepted a removed drag");
    (void)send(PointerDown{{1}, f.point()});
    f.styles[f.geometry().handle.index].visibility = Visibility::hidden;
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text}));
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Hidden handle retained capture");
    f.disabled = true; f.build();
    check(!send(PointerDown{{1}, f.point()}).handled, "Disabled handle captured input");
}

void keyboard_resize_and_nesting(FlexDirection direction) {
    Fixture f(direction);
    SplitPaneDispatcher dispatcher("panes", 10);
    auto send = [&](auto event, bool focus = true) {
        const auto result = take(dispatcher.dispatch(f.hit(), {std::chrono::microseconds{0}, event},
            focus ? std::optional{f.geometry().handle} : std::nullopt));
        f.apply(result);
        return result;
    };
    const Key increase = direction == FlexDirection::row ? Key::right : Key::down;
    const Key decrease = direction == FlexDirection::row ? Key::left : Key::up;
    check(!send(KeyDown{increase}, false).handled, "Unfocused separator consumed adjustment");
    check(!send(KeyDown{increase, {.control = true}}).handled, "Modified key consumed adjustment");
    (void)send(KeyDown{increase, {}, true}); equal(f.size, 150, "Keyboard repeat step differs");
    (void)send(Navigate{direction == FlexDirection::row ? Direction::left : Direction::up});
    equal(f.size, 140, "Gamepad adjustment differs");
    (void)send(KeyDown{Key::home}); equal(f.size, 80, "Home minimum differs");
    check(!send(KeyDown{decrease}).action, "Clamped keyboard adjustment executed a command");
    (void)send(KeyDown{Key::end}); equal(f.size, 268, "End maximum differs");
    if (direction == FlexDirection::row) f.viewport.width = 250;
    else f.viewport.height = 250;
    f.build();
    f.size = *take(dispatcher.refresh(f.hit())).size;
    equal(f.size, 118, "Parent resize did not clamp retained logical size");
    if (direction == FlexDirection::row) f.viewport.width = 400;
    else f.viewport.height = 400;
    f.build();
    equal(f.geometry().size, 118, "Growth restored an obsolete requested split");
    f.build(true, true);
    check(take(split_pane_geometry(f.hit(), "nested")).has_value(), "Nested SplitPane failed Flex composition");
    if (direction == FlexDirection::row) f.viewport.width = 150;
    else f.viewport.height = 150;
    f.build();
    const auto view = f.geometry();
    equal(view.size, 80, "Infeasible minima did not retain first minimum");
    equal(view.maximum, 80, "Infeasible minima must lock adjustment");
    check(!send(KeyDown{Key::end}).action, "Infeasible minima executed adjustment");
    SplitPaneDispatcher invalid("panes", 0);
    check(!invalid.refresh(f.hit()), "Invalid keyboard step accepted");
}

void scrolled_parent_coordinates() {
    ui::Session session;
    auto element = ui::ScrollView({ui::HorizontalSplitPane(ui::Column({}), ui::Column({}), 140, "resize",
        {.node = {.id = "panes", .style = {.width = Dimension::points(400), .height = Dimension::points(400),
            .margin = Edges{40, 0, 0, 20}, .padding = Edges{12, 12, 12, 12}}}, .limits = {80, 100, 8}})},
        {.id = "scroll"});
    const auto authored = take(session.lower(element, actions));
    const auto tree = take(UiTree::create(authored.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
    PlaceholderTextShaper text;
    std::vector<Point> offsets(tree->size()); offsets[0] = {15, 25};
    const auto layout = take(compute_layout({tree.get(), styles, {280, 200}, &text, offsets}));
    const HitTestInput hit{tree.get(), styles, &layout};
    const auto geometry = *take(split_pane_geometry(hit, "panes"));
    equal(geometry.first_origin.x, 17, "Ancestor scrolling was not included in logical pane origin");
    equal(geometry.first_origin.y, 27, "Ancestor Y scrolling/padding was not included");
    SplitPaneDispatcher dispatcher("panes");
    Point point{geometry.bounds.origin.x + 3, geometry.bounds.origin.y + 20};
    check(!take(dispatcher.dispatch(hit, {std::chrono::microseconds{0}, PointerDown{{1}, {point.x, 300}}})).handled,
        "Clipped handle region captured input");
    check(take(dispatcher.dispatch(hit, {std::chrono::microseconds{0}, PointerDown{{1}, point}})).handled, "Visible scrolled handle did not capture");
    point.x += 40;
    const auto moved = take(dispatcher.dispatch(hit, {std::chrono::microseconds{0}, PointerMove{{1}, point}}));
    equal(*moved.size, 180, "Drag coordinates ignored ancestor scrolling or parent padding");
}
}

int main() {
    try {
        validation();
        scrolled_parent_coordinates();
        for (const auto direction : {FlexDirection::row, FlexDirection::column}) {
            geometry_and_semantics(direction);
            drag_and_replacement(direction);
            keyboard_resize_and_nesting(direction);
        }
        std::cout << "SplitPane geometry, drag, cancellation, retention, keyboard, semantics and CPU checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
