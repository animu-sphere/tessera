#include <tessera/input/scrollbar.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/ui/authoring.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include "../check.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace tessera;
namespace ui = tessera::authoring;
using tessera::test::check;
using namespace std::chrono_literals;

namespace {
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.diagnostics.front().code + ": " + result.diagnostics.front().message);
    return std::move(*result.value);
}
void equal(float actual, float expected, const char* message) { check(std::abs(actual - expected) < .002f, message); }
const ValidationContext actions{{"scroll", "item", "other", "resize"}};
constexpr auto vertical = ScrollbarAxis::vertical;
constexpr auto horizontal = ScrollbarAxis::horizontal;
float along(ScrollbarAxis axis, Point value) { return axis == horizontal ? value.x : value.y; }
float along(ScrollbarAxis axis, Size value) { return axis == horizontal ? value.width : value.height; }
float& main_axis(ScrollbarAxis axis, Point& value) { return axis == horizontal ? value.x : value.y; }
float& cross_axis(ScrollbarAxis axis, Point& value) { return axis == horizontal ? value.y : value.x; }
// Vertical: a column of 240x48 items beside the bar. Horizontal: a row of 60x160 items above the bar.
// Both overflow on the cross axis too, so cross-axis offsets are observable.
struct Fixture {
    ui::Session session;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    PlaceholderTextShaper text;
    ScrollbarAxis axis;
    float scale;
    Size viewport;
    Point offset;
    float length; // Authored main-axis ScrollView size.
    int count = 16;
    float minimum_thumb = 18;
    bool disabled = false, hovered = false, focused = false;
    std::string action = "scroll";

    explicit Fixture(ScrollbarAxis bar_axis = vertical, float device_scale = 1)
        : axis(bar_axis), scale(device_scale), viewport{640 / scale, 360 / scale},
          length(axis == horizontal ? viewport.width - 180 : viewport.height - 32) {
        build();
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    const LayoutBox& box(std::string_view id) const {
        const auto node = tree->find(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const auto& value) { return value.node == node; });
        check(found != layout.boxes.end(), "Missing fixture box");
        return *found;
    }
    ScrollbarGeometry geometry() const {
        const auto view = take(scrollbar_geometry(hit(), "bar", "inspector"));
        check(view.has_value() && view->axis == axis, "Missing scrollbar geometry");
        return *view;
    }
    float along(Point value) const { return ::along(axis, value); }
    float along(Size value) const { return ::along(axis, value); }
    // Main-axis scroll delta or logical point displacement.
    Point delta(float value) const { return axis == horizontal ? Point{value, 0} : Point{0, value}; }
    void build(bool present = true) {
        auto prepare = [&](float view_length, ScrollGeometry scroll) {
            std::vector<ui::Element> items;
            const auto item = axis == horizontal ? Size{60, 160} : Size{240, 48};
            for (int i = 0; i < count; ++i)
                items.push_back(ui::Button("", "item",
                                           {.node = {.id = "item-" + std::to_string(i),
                                                     .style = {.width = Dimension::points(item.width),
                                                               .height = Dimension::points(item.height),
                                                               .background = Color{.8f, .9f, 1, 1}}}}));
            ui::ScrollbarProps props{.node = {.id = "bar"},
                                     .minimum_thumb = minimum_thumb,
                                     .disabled = disabled,
                                     .hovered = hovered,
                                     .focused = focused};
            std::vector<ui::Element> inspector;
            if (axis == horizontal) {
                inspector.push_back(ui::ScrollView(
                    {ui::Row(std::move(items), {.style = {.width = Dimension::points(item.width * count)}})},
                    {.id = "inspector",
                     .style = {.width = Dimension::points(length), .height = Dimension::points(120)}}));
                if (present)
                    inspector.push_back(ui::HorizontalScrollbar("Inspector", view_length, scroll, action, props));
            } else {
                inspector.push_back(ui::ScrollView(
                    std::move(items),
                    {.id = "inspector",
                     .style = {.width = Dimension::points(0), .height = Dimension::points(length), .grow = 1.f}}));
                if (present)
                    inspector.push_back(ui::VerticalScrollbar("Inspector", view_length, scroll, action, props));
            }
            auto content = axis == horizontal ? ui::Column(std::move(inspector)) : ui::Row(std::move(inspector));
            auto element = ui::HorizontalSplitPane(
                ui::Column({}), std::move(content), 80, "resize",
                {.node = {.id = "panes", .style = {.padding = Edges{8, 8, 8, 8}}}, .limits = {60, 100, 8}});
            const auto authored = take(session.lower(element, actions));
            tree = take(UiTree::create(authored.document, actions));
            const StyleSheet sheet;
            styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
            std::vector<Point> offsets(tree->size());
            offsets[tree->find("inspector")->index] = offset;
            layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
        };
        prepare(length, {axis == horizontal ? Size{length, 120} : Size{240, length}, {}});
        const auto accepted = *box("inspector").scroll;
        const auto view_length = along(box("inspector").padding_box().size);
        offset = accepted.offset;
        prepare(view_length, accepted);
        session.publish();
    }
    void apply(const std::vector<ScrollUpdate>& updates) {
        for (const auto& update : updates) {
            check(update.container == tree->find("inspector"), "Wrong scroll owner");
            offset = update.offset;
        }
        build();
    }
    void apply(const ScrollbarDispatchResult& result) {
        if (result.action) {
            check(result.update.has_value() && result.action->action == action, "Scroll command lost its payload");
            apply(std::vector<ScrollUpdate>{*result.update});
        }
    }
    // Three units into the thumb along the main axis, centered across it.
    Point point(float shift = 0) const {
        const auto bounds = geometry().bounds;
        return axis == horizontal ? Point{bounds.origin.x + 3 + shift, bounds.origin.y + bounds.size.height / 2}
                                  : Point{bounds.origin.x + bounds.size.width / 2, bounds.origin.y + 3 + shift};
    }
    Point moved(Point point, float shift) const {
        main_axis(axis, point) += shift;
        return point;
    }
};

void validation() {
    ui::Session session;
    const SourceLocation source{"fixture://scrollbar.cpp", {1, 4}};
    for (const bool across : {false, true}) {
        const auto bar = [&](float length, ScrollGeometry value, ui::ScrollbarProps props) {
            return across ? ui::HorizontalScrollbar("Inspector", length, value, "scroll", std::move(props))
                          : ui::VerticalScrollbar("Inspector", length, value, "scroll", std::move(props));
        };
        // Main-axis metrics; the other axis is deliberately inconsistent and must not matter.
        const auto geometry = [&](float extent, float offset) {
            return across ? ScrollGeometry{{extent, 10}, {offset, 30}} : ScrollGeometry{{10, extent}, {30, offset}};
        };
        auto lower = [&](float length, ScrollGeometry value, ui::ScrollbarProps props) {
            return session.lower(bar(length, value, std::move(props)), actions);
        };
        const auto scroll = geometry(200, 30);
        check(!lower(100, scroll, {}), "Missing stable ID accepted");
        ui::ScrollbarProps props{.node = {.id = "bar", .source = source}};
        check(static_cast<bool>(lower(100, scroll, props)), "Valid main-axis metrics rejected");
        auto invalid = lower(-1, scroll, props);
        check(!invalid && invalid.diagnostics.front().source && invalid.diagnostics.front().source->location == source,
              "Invalid geometry lost caller source");
        check(!lower(100, geometry(99, 0), props) && !lower(100, geometry(200, 101), props) &&
                  !lower(std::numeric_limits<float>::infinity(), scroll, props) &&
                  !lower(100, geometry(std::numeric_limits<float>::quiet_NaN(), 0), props),
              "Invalid accepted metrics were silently clamped");
        props.minimum_thumb = 0;
        check(!lower(100, scroll, props), "Zero minimum accepted");
        props.minimum_thumb = std::numeric_limits<float>::infinity();
        check(!lower(100, scroll, props), "Infinite minimum accepted");
    }
}

void geometry_wheel_and_reveal(ScrollbarAxis axis, float scale) {
    Fixture f(axis, scale);
    auto view = f.geometry();
    const auto& scroll = f.box("inspector");
    equal(f.along(view.track.size), f.length, "Track must observe the logical scroll viewport");
    equal(f.along(view.bounds.size), std::max(18.f, f.length * f.length / f.along(scroll.scroll->extent)),
          "Visible fraction/minimum thumb differs");
    equal(f.along(view.bounds.origin), f.along(view.track.origin), "Initial thumb position differs");
    check(view.enabled && take(hit_test(f.hit(), f.point())).target == view.thumb, "Thumb hit/eligibility differs");
    const auto routed = take(route_scroll(f.hit(), {f.box("inspector").border_box.origin, f.delta(60)}));
    f.apply(routed.updates);
    view = f.geometry();
    equal(f.along(view.offset), 60, "Wheel did not update canonical offset");
    equal(f.along(view.bounds.origin) - f.along(view.track.origin),
          (f.along(view.track.size) - f.along(view.bounds.size)) * 60 / view.maximum, "Wheel left a stale thumb");
    FocusDispatcher focus;
    const auto target = *f.tree->find("item-15");
    check(take(focus.focus(f.hit(), target)).focused == target, "Keyboard focus could not reach clipped content");
    f.apply(take(scroll_into_view(f.hit(), target)));
    view = f.geometry();
    equal(f.along(view.offset), view.maximum, "Focus reveal did not update canonical offset");
    equal(f.along(view.bounds.origin) + f.along(view.bounds.size),
          f.along(view.track.origin) + f.along(view.track.size), "Focus reveal did not move the thumb to the end");
    ScrollbarDispatcher dispatcher("bar", "inspector");
    check(take(dispatcher.refresh(f.hit())).update == ScrollUpdate{view.container, f.offset},
          "Refresh did not expose canonical offsets");
    f.count = 2;
    f.build();
    equal(f.along(f.offset), 0, "Content shrink did not clamp the host offset");
    check(!f.geometry().enabled && f.along(take(dispatcher.refresh(f.hit())).update->offset) == 0,
          "No-overflow control remained interactive");
    f.count = 16;
    f.build();
    equal(f.along(f.offset), 0, "Content growth restored an obsolete requested offset");
    main_axis(axis, f.offset) = f.geometry().maximum;
    f.build();
    f.length += 40;
    f.build();
    const auto clamped = take(dispatcher.refresh(f.hit())).update->offset;
    equal(f.along(clamped), f.geometry().maximum, "Resize did not clamp canonical offsets");
    f.length -= 40;
    f.build();
    check(f.offset == clamped, "Viewport shrink restored a discarded offset");
}

void capture_lifecycle(ScrollbarAxis axis, float scale) {
    Fixture f(axis, scale);
    cross_axis(axis, f.offset) = 10;
    f.build();
    auto cross = f.offset;
    const auto retained = cross_axis(axis, cross);
    ScrollbarDispatcher dispatcher("bar", "inspector", 10);
    auto send = [&](auto event) { return take(dispatcher.dispatch(f.hit(), {0us, event})); };
    auto point = f.point();
    const auto down = send(PointerDown{{1}, point});
    check(down.handled && down.focus == f.geometry().thumb && !down.action && dispatcher.dragging(),
          "Press must focus and capture without jumping");
    check(!send(PointerMove{{1}, point}).action, "Stationary press moved the accepted offset");
    check(!send(PointerDown{{2}, point}).handled && !send(PointerMove{{2}, f.point(40)}).handled &&
              !send(PointerUp{{1}, point, PointerButton::secondary}).handled && dispatcher.dragging(),
          "Foreign pointer or secondary release stole capture");
    const auto start = f.geometry();
    point = f.moved(point, 20);
    auto moved = send(PointerMove{{1}, point});
    equal(f.along(moved.update->offset), 20 * start.maximum / (f.along(start.track.size) - f.along(start.bounds.size)),
          "Logical drag mapping/grab offset differs");
    const auto old_thumb = start.thumb;
    f.apply(moved);
    equal(cross_axis(axis, f.offset), retained, "Main-axis adjustment changed the cross-axis offset");
    (void)take(dispatcher.refresh(f.hit()));
    check(dispatcher.dragging() && old_thumb != f.geometry().thumb, "Compatible replacement lost capture");
    check(!send(PointerMove{{1}, point}).action, "Compatible replacement duplicated a stationary command");
    const auto up = send(PointerUp{{1}, point});
    check(up.handled && !up.action && !dispatcher.dragging(), "Release duplicated the accepted command");
    point = f.point();
    (void)send(PointerDown{{1}, point});
    f.apply(send(PointerMove{{1}, f.moved(point, 10000)}));
    equal(f.along(f.offset), f.geometry().maximum, "Drag upper clamp differs");
    f.apply(send(PointerMove{{1}, f.moved(point, -10000)}));
    equal(f.along(f.offset), 0, "Drag lower clamp differs");
    check(!send(PointerCancel{{2}}).handled && dispatcher.dragging(), "Foreign cancel stole capture");
    check(send(PointerCancel{{1}}).handled && !dispatcher.dragging(), "Capture cancellation failed");
    check(!send(PointerUp{{1}, point}).action, "Cancelled capture leaked a release command");
    (void)send(PointerDown{{1}, f.point()});
    check(send(KeyDown{Key::escape}).handled && !dispatcher.dragging(), "Escape retained capture");
    (void)send(PointerDown{{1}, f.point()});
    check(send(Cancel{}).handled && !dispatcher.dragging(), "Logical Cancel retained capture");
    (void)send(PointerDown{{1}, f.point()});
    check(!send(FocusNext{}).handled && !dispatcher.dragging(), "Focus traversal retained a pointer drag");
    (void)send(PointerDown{{1}, f.point()});
    const auto invalid =
        dispatcher.dispatch(f.hit(), {0us, PointerMove{{1}, {0, std::numeric_limits<float>::quiet_NaN()}}});
    check(!invalid && dispatcher.dragging(), "Invalid input mutated accepted capture");
    f.action = "other";
    f.build();
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Changed action retained capture");
    (void)send(PointerDown{{1}, f.point()});
    f.build(false);
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Removed scrollbar retained capture");
    f.build();
    check(!send(PointerUp{{1}, f.point()}).action, "Reinserted scrollbar accepted an obsolete release");
    (void)send(PointerDown{{1}, f.point()});
    f.apply(take(route_scroll(f.hit(), {f.box("inspector").border_box.origin, f.delta(10)})).updates);
    (void)take(dispatcher.refresh(f.hit()));
    check(dispatcher.dragging(), "Compatible wheel update unnecessarily cancelled capture");
    f.length -= 10;
    f.build();
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Changed travel retained capture");
    (void)send(PointerDown{{1}, f.point()});
    f.count = 1;
    f.build();
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging() && !send(PointerUp{{1}, point}).action,
          "Content shrink retained capture or leaked a command");
    f.count = 16;
    f.build();
    f.disabled = true;
    f.build();
    check(!send(PointerDown{{1}, f.point()}).handled &&
              !request_semantic_action(f.semantic(), f.geometry().thumb, "activate"),
          "Disabled scrollbar exposed input/action");
}

void keyboard_and_degenerate(ScrollbarAxis axis) {
    Fixture f(axis);
    const bool across = axis == horizontal;
    const auto forward_key = across ? Key::right : Key::down;
    const auto forward = across ? Direction::right : Direction::down;
    ScrollbarDispatcher dispatcher("bar", "inspector", 10);
    auto send = [&](auto event, bool focused = true) {
        auto result = take(
            dispatcher.dispatch(f.hit(), {0us, event}, focused ? std::optional{f.geometry().thumb} : std::nullopt));
        f.apply(result);
        return result;
    };
    check(!send(KeyDown{forward_key}, false).handled && !send(KeyDown{forward_key, {.control = true}}).handled,
          "Unfocused/modified keys were consumed");
    (void)send(KeyDown{forward_key, {}, true});
    equal(f.along(f.offset), 10, "Keyboard repeat step differs");
    (void)send(Navigate{forward});
    equal(f.along(f.offset), 20, "Gamepad step differs");
    (void)send(Activate{});
    equal(f.along(f.offset), 30, "Semantic/logical adjustment differs");
    (void)send(KeyDown{across ? Key::left : Key::up});
    equal(f.along(f.offset), 20, "Backward key step differs");
    (void)send(KeyDown{Key::end});
    equal(f.along(f.offset), f.geometry().maximum, "End did not reach the limit");
    check(!send(KeyDown{forward_key}).action, "Clamped key leaked a command");
    (void)send(KeyDown{Key::home});
    equal(f.along(f.offset), 0, "Home did not reach the beginning");
    check(!send(Navigate{across ? Direction::down : Direction::left}).handled &&
              !send(KeyDown{across ? Key::down : Key::left}).handled,
          "Cross-axis navigation adjusted main-axis scrolling");
    f.minimum_thumb = f.length * 2;
    f.build();
    equal(f.along(f.geometry().bounds.size), f.length, "Minimum thumb exceeded the track");
    check(!f.geometry().enabled && !send(KeyDown{Key::end}).action, "Zero-travel adjustment divided by zero");
    f.minimum_thumb = 18;
    f.length = 0;
    f.build();
    check(!f.geometry().enabled && !send(Activate{}, false).action, "Zero viewport remained interactive");
    ScrollbarDispatcher invalid("bar", "inspector", 0);
    check(!invalid.refresh(f.hit()) && !scrollbar_geometry(f.hit(), "", "inspector"), "Invalid dispatcher accepted");
}

void stale_geometry_and_eligibility(ScrollbarAxis axis) {
    Fixture f(axis);
    ScrollbarDispatcher dispatcher("bar", "inspector");
    (void)take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, f.point()}}));
    std::vector<Point> offsets(f.tree->size());
    offsets[f.tree->find("inspector")->index] = f.delta(50);
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    auto stale = dispatcher.refresh(f.hit());
    check(!stale && stale.diagnostics.front().code == "scrollbar_geometry" && dispatcher.dragging(),
          "Stale authored thumb was dispatched or mutated capture on failure");
    f.offset = f.delta(50);
    f.build();
    (void)take(dispatcher.refresh(f.hit()));
    check(dispatcher.dragging(), "Synchronized geometry lost compatible capture");
    f.styles[f.tree->find("bar-thumb")->index].visibility = Visibility::hidden;
    offsets.assign(f.tree->size(), {});
    offsets[f.tree->find("inspector")->index] = f.offset;
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging(), "Hidden thumb retained capture");
    f.build();
    const auto track = f.tree->find("bar")->index;
    const auto direction = f.styles[track].direction;
    f.styles[track].direction = direction == FlexDirection::row ? FlexDirection::column : FlexDirection::row;
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    const auto flipped = scrollbar_geometry(f.hit(), "bar", "inspector");
    check(!flipped && flipped.diagnostics.front().code == "scrollbar_geometry",
          "A flipped track was dispatched on the other axis");
    f.styles[track].direction = direction;
    auto document = f.tree->document();
    document.root.properties["disabled"] = true;
    f.tree = take(UiTree::create(document, actions));
    offsets.assign(f.tree->size(), {});
    offsets[f.tree->find("inspector")->index] = f.offset;
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    check(!f.geometry().enabled, "Disabled ancestry allowed scrollbar adjustment");
    document.root.properties["disabled"] = false;
    auto& thumb = document.root.children[2].children[0].children[1].children[1];
    thumb.properties["separator"] = true;
    check(!UiTree::create(document, actions), "Conflicting scrollbar role markers accepted");
}

void observation_paint_and_replay(ScrollbarAxis axis, float scale) {
    Fixture f(axis, scale);
    const auto plain = f.styles[f.geometry().thumb.index].background;
    f.hovered = true;
    f.build();
    const auto hover = f.styles[f.geometry().thumb.index].background;
    f.focused = true;
    f.build();
    check(plain != hover && hover != f.styles[f.geometry().thumb.index].background,
          "Hover/focus feedback is indistinct");
    const auto view = f.geometry();
    auto snapshot = take(capture_semantics(f.semantic(), 7));
    const auto record = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                     [&](const auto& node) { return node.id == "bar-thumb"; });
    check(record != snapshot.nodes.end() && record->role == SemanticRole::scrollbar && record->focusable &&
              record->name.find("Inspector: 0") != std::string::npos && record->actions.size() == 1,
          "Scrollbar semantic name/role/action differs");
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot, "Semantic scrollbar round trip failed");
    check(take(load_document(take(save_document(f.tree->document(), actions)), actions)) == f.tree->document(),
          "Scrollbar UI marker round trip failed");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const std::vector<UiInvariant> invariants{
        {"scrollbar", FocusableInvariant{SemanticTarget{SemanticRole::scrollbar, record->name}}},
        {"inside", InsideViewportInvariant{AuthorIdTarget{"bar-thumb"}}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Inspection/invariants lost scrollbar");
    auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    const auto drawn = std::find_if(paint.commands.begin(), paint.commands.end(), [&](const auto& command) {
        const auto* rect = std::get_if<DrawRect>(&command);
        return rect && rect->rect == view.bounds;
    });
    check(drawn != paint.commands.end(), "Paint and hit testing use different thumb geometry");
    CpuFrameCapture frames;
    const auto image =
        take(frames.capture({1, f.viewport, scale, take(capture_extent(f.viewport, scale)), &paint, 0us}));
    check(image == take(frames.capture({2, f.viewport, scale, take(capture_extent(f.viewport, scale)), &paint, 0us})),
          "Scaled CPU rendering is nondeterministic");
    ReplayRecording recording;
    recording.context = actions;
    recording.document = f.tree->document();
    recording.styles = f.styles;
    recording.viewport = f.viewport;
    recording.environment.scale = scale;
    recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::scrollbar, record->name}},
                       ReplaySemanticAction{SemanticTarget{SemanticRole::scrollbar, record->name}}};
    check(take(load_replay(take(save_replay(recording)))) == recording, "Scrollbar replay target round trip failed");
    const auto replay = take(play_replay(recording, f.text));
    check(replay.actions.size() == 1 && replay.actions.front().action == "scroll" &&
              compare_replay(replay, take(play_replay(recording, f.text))).empty(),
          "Scrollbar replay differs");
    auto changed = replay;
    auto& nodes = changed.semantics.back().nodes;
    const auto entry =
        std::find_if(nodes.begin(), nodes.end(), [](const auto& node) { return node.id == "bar-thumb"; });
    check(entry != nodes.end(), "Replay lost the scrollbar node");
    entry->name += " changed";
    check(!compare_replay(replay, changed).empty(), "Replay comparison missed changed scrollbar semantics");
    recording.version = replay_host_version;
    check(take(load_replay(take(save_replay(recording)))) == recording &&
              compare_replay(replay, take(play_replay(recording, f.text))).empty(),
          "Scrollbar replay v2 differs from v1 without host slots");

    // Clip pane content eight units below the thumb's top (the root keeps its 8-unit bottom padding);
    // heights here never change horizontal allocation.
    ScrollbarDispatcher dispatcher("bar", "inspector");
    (void)take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, f.point()}}));
    auto& root = f.styles[f.tree->root().index];
    root.height = Dimension::points(view.bounds.origin.y + 16);
    root.overflow = Overflow::clip;
    std::vector<Point> offsets(f.tree->size());
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    const auto clipped = f.geometry();
    const Point hidden{f.point().x, view.bounds.origin.y + 10};
    check(take(hit_test(f.hit(), f.point())).target == clipped.thumb &&
              take(hit_test(f.hit(), hidden)).target != clipped.thumb,
          "Clipped thumb captured input outside the ancestor viewport");
    check(!take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, hidden}})).handled, "Clipped thumb started a drag");
    paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    const auto clipped_image =
        take(frames.capture({3, f.viewport, scale, take(capture_extent(f.viewport, scale)), &paint, 0us}));
    const auto x = static_cast<std::uint32_t>(hidden.x * scale);
    const auto y = static_cast<std::uint32_t>(hidden.y * scale);
    check(clipped_image.pixels[(std::size_t(y) * clipped_image.extent.width + x) * 4 + 3] == 0,
          "Clipped thumb leaked CPU pixels outside the ancestor viewport");
    root.height = Dimension::points(4);
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging() && !f.geometry().enabled, "Fully clipped scrollbar retained capture");
}
}

int main() {
    try {
        validation();
        for (const auto axis : {vertical, horizontal}) {
            keyboard_and_degenerate(axis);
            stale_geometry_and_eligibility(axis);
            for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
                geometry_wheel_and_reveal(axis, scale);
                capture_lifecycle(axis, scale);
                observation_paint_and_replay(axis, scale);
            }
        }
        std::cout << "Controlled vertical/horizontal scrollbars: geometry, commands, lifecycle and four-scale "
                     "observations passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
