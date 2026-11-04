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
struct Fixture {
    ui::Session session;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    PlaceholderTextShaper text;
    float scale;
    Size viewport;
    Point offset;
    float height;
    int count = 16;
    float minimum_thumb = 18;
    bool disabled = false, hovered = false, focused = false;
    std::string action = "scroll";

    explicit Fixture(float device_scale = 1)
        : scale(device_scale), viewport{640 / scale, 360 / scale}, height(viewport.height - 32) {
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
        check(view.has_value(), "Missing scrollbar geometry");
        return *view;
    }
    void build(bool present = true) {
        auto prepare = [&](float view_height, ScrollGeometry scroll) {
            std::vector<ui::Element> items;
            for (int i = 0; i < count; ++i)
                items.push_back(ui::Button("", "item",
                                           {.node = {.id = "item-" + std::to_string(i),
                                                     .style = {.width = Dimension::points(240),
                                                               .height = Dimension::points(48),
                                                               .background = Color{.8f, .9f, 1, 1}}}}));
            std::vector<ui::Element> inspector{ui::ScrollView(
                std::move(items),
                {.id = "inspector",
                 .style = {.width = Dimension::points(0), .height = Dimension::points(height), .grow = 1.f}})};
            if (present)
                inspector.push_back(ui::VerticalScrollbar("Inspector", view_height, scroll, action,
                                                          {.node = {.id = "bar"},
                                                           .minimum_thumb = minimum_thumb,
                                                           .disabled = disabled,
                                                           .hovered = hovered,
                                                           .focused = focused}));
            auto element = ui::HorizontalSplitPane(
                ui::Column({}), ui::Row(std::move(inspector)), 80, "resize",
                {.node = {.id = "panes", .style = {.padding = Edges{8, 8, 8, 8}}}, .limits = {60, 100, 8}});
            const auto authored = take(session.lower(element, actions));
            tree = take(UiTree::create(authored.document, actions));
            const StyleSheet sheet;
            styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
            std::vector<Point> offsets(tree->size());
            offsets[tree->find("inspector")->index] = offset;
            layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets}));
        };
        prepare(height, {{240, height}, {}});
        const auto accepted = *box("inspector").scroll;
        const auto view_height = box("inspector").padding_box().size.height;
        offset = accepted.offset;
        prepare(view_height, accepted);
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
    Point point(float delta = 0) const {
        const auto bounds = geometry().bounds;
        return {bounds.origin.x + bounds.size.width / 2, bounds.origin.y + 3 + delta};
    }
};

void validation() {
    ui::Session session;
    const ScrollGeometry scroll{{10, 200}, {0, 30}};
    auto lower = [&](float height, ScrollGeometry value, ui::ScrollbarProps props) {
        return session.lower(ui::VerticalScrollbar("Inspector", height, value, "scroll", props), actions);
    };
    check(!lower(100, scroll, {}), "Missing stable ID accepted");
    const SourceLocation source{"fixture://scrollbar.cpp", {1, 4}};
    ui::ScrollbarProps props{.node = {.id = "bar", .source = source}};
    auto invalid = lower(-1, scroll, props);
    check(!invalid && invalid.diagnostics.front().source && invalid.diagnostics.front().source->location == source,
          "Invalid geometry lost caller source");
    check(!lower(100, {{10, 99}, {}}, props) && !lower(100, {{10, 200}, {0, 101}}, props) &&
              !lower(std::numeric_limits<float>::infinity(), scroll, props) &&
              !lower(100, {{10, std::numeric_limits<float>::quiet_NaN()}, {}}, props),
          "Invalid accepted metrics were silently clamped");
    props.minimum_thumb = 0;
    check(!lower(100, scroll, props), "Zero minimum accepted");
    props.minimum_thumb = std::numeric_limits<float>::infinity();
    check(!lower(100, scroll, props), "Infinite minimum accepted");
}

void geometry_wheel_and_reveal(float scale) {
    Fixture f(scale);
    auto view = f.geometry();
    const auto& scroll = f.box("inspector");
    equal(view.track.size.height, f.height, "Track must observe the logical scroll viewport");
    equal(view.bounds.size.height, std::max(18.f, f.height * f.height / scroll.scroll->extent.height),
          "Visible fraction/minimum thumb differs");
    equal(view.bounds.origin.y, view.track.origin.y, "Initial thumb position differs");
    check(view.enabled && take(hit_test(f.hit(), f.point())).target == view.thumb, "Thumb hit/eligibility differs");
    const auto routed = take(route_scroll(f.hit(), {f.box("inspector").border_box.origin, {0, 60}}));
    f.apply(routed.updates);
    view = f.geometry();
    equal(view.offset.y, 60, "Wheel did not update canonical offset");
    equal(view.bounds.origin.y - view.track.origin.y,
          (view.track.size.height - view.bounds.size.height) * 60 / view.maximum, "Wheel left a stale thumb");
    FocusDispatcher focus;
    const auto target = *f.tree->find("item-15");
    check(take(focus.focus(f.hit(), target)).focused == target, "Keyboard focus could not reach clipped content");
    f.apply(take(scroll_into_view(f.hit(), target)));
    view = f.geometry();
    equal(view.offset.y, view.maximum, "Focus reveal did not update canonical offset");
    equal(view.bounds.origin.y + view.bounds.size.height, view.track.origin.y + view.track.size.height,
          "Focus reveal did not move the thumb to the end");
    ScrollbarDispatcher dispatcher("bar", "inspector");
    check(take(dispatcher.refresh(f.hit())).update == ScrollUpdate{view.container, f.offset},
          "Refresh did not expose canonical offsets");
    f.count = 2;
    f.build();
    equal(f.offset.y, 0, "Content shrink did not clamp the host offset");
    check(!f.geometry().enabled && take(dispatcher.refresh(f.hit())).update->offset.y == 0,
          "No-overflow control remained interactive");
    f.count = 16;
    f.build();
    equal(f.offset.y, 0, "Content growth restored an obsolete requested offset");
    f.offset.y = f.geometry().maximum;
    f.build();
    f.height += 40;
    f.build();
    const auto clamped = take(dispatcher.refresh(f.hit())).update->offset;
    equal(clamped.y, f.geometry().maximum, "Resize did not clamp canonical offsets");
    f.height -= 40;
    f.build();
    check(f.offset == clamped, "Viewport shrink restored a discarded offset");
}

void capture_lifecycle(float scale) {
    Fixture f(scale);
    f.offset.x = 10;
    f.build();
    const auto horizontal = f.offset.x;
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
    point.y += 20;
    auto moved = send(PointerMove{{1}, point});
    equal(moved.update->offset.y, 20 * start.maximum / (start.track.size.height - start.bounds.size.height),
          "Logical drag mapping/grab offset differs");
    const auto old_thumb = start.thumb;
    f.apply(moved);
    equal(f.offset.x, horizontal, "Vertical adjustment changed the horizontal offset");
    (void)take(dispatcher.refresh(f.hit()));
    check(dispatcher.dragging() && old_thumb != f.geometry().thumb, "Compatible replacement lost capture");
    check(!send(PointerMove{{1}, point}).action, "Compatible replacement duplicated a stationary command");
    const auto up = send(PointerUp{{1}, point});
    check(up.handled && !up.action && !dispatcher.dragging(), "Release duplicated the accepted command");
    point = f.point();
    (void)send(PointerDown{{1}, point});
    f.apply(send(PointerMove{{1}, {point.x, 10000}}));
    equal(f.offset.y, f.geometry().maximum, "Drag upper clamp differs");
    f.apply(send(PointerMove{{1}, {point.x, -10000}}));
    equal(f.offset.y, 0, "Drag lower clamp differs");
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
    f.apply(take(route_scroll(f.hit(), {f.box("inspector").border_box.origin, {0, 10}})).updates);
    (void)take(dispatcher.refresh(f.hit()));
    check(dispatcher.dragging(), "Compatible wheel update unnecessarily cancelled capture");
    f.height -= 10;
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

void keyboard_and_degenerate() {
    Fixture f;
    ScrollbarDispatcher dispatcher("bar", "inspector", 10);
    auto send = [&](auto event, bool focused = true) {
        auto result = take(
            dispatcher.dispatch(f.hit(), {0us, event}, focused ? std::optional{f.geometry().thumb} : std::nullopt));
        f.apply(result);
        return result;
    };
    check(!send(KeyDown{Key::down}, false).handled && !send(KeyDown{Key::down, {.control = true}}).handled,
          "Unfocused/modified keys were consumed");
    (void)send(KeyDown{Key::down, {}, true});
    equal(f.offset.y, 10, "Keyboard repeat step differs");
    (void)send(Navigate{Direction::down});
    equal(f.offset.y, 20, "Gamepad step differs");
    (void)send(Activate{});
    equal(f.offset.y, 30, "Semantic/logical adjustment differs");
    (void)send(KeyDown{Key::end});
    equal(f.offset.y, f.geometry().maximum, "End did not reach the limit");
    check(!send(KeyDown{Key::down}).action, "Clamped key leaked a command");
    (void)send(KeyDown{Key::home});
    equal(f.offset.y, 0, "Home did not reach the beginning");
    check(!send(Navigate{Direction::left}).handled, "Horizontal navigation adjusted vertical scrolling");
    f.minimum_thumb = f.height * 2;
    f.build();
    equal(f.geometry().bounds.size.height, f.height, "Minimum thumb exceeded the track");
    check(!f.geometry().enabled && !send(KeyDown{Key::end}).action, "Zero-travel adjustment divided by zero");
    f.minimum_thumb = 18;
    f.height = 0;
    f.build();
    check(!f.geometry().enabled && !send(Activate{}, false).action, "Zero viewport remained interactive");
    ScrollbarDispatcher invalid("bar", "inspector", 0);
    check(!invalid.refresh(f.hit()) && !scrollbar_geometry(f.hit(), "", "inspector"), "Invalid dispatcher accepted");
}

void stale_geometry_and_eligibility() {
    Fixture f;
    ScrollbarDispatcher dispatcher("bar", "inspector");
    (void)take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, f.point()}}));
    std::vector<Point> offsets(f.tree->size());
    offsets[f.tree->find("inspector")->index] = {0, 50};
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    auto stale = dispatcher.refresh(f.hit());
    check(!stale && stale.diagnostics.front().code == "scrollbar_geometry" && dispatcher.dragging(),
          "Stale authored thumb was dispatched or mutated capture on failure");
    f.offset.y = 50;
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

void observation_paint_and_replay(float scale) {
    Fixture f(scale);
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

    ScrollbarDispatcher dispatcher("bar", "inspector");
    (void)take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, f.point()}}));
    f.styles[f.tree->root().index].height = Dimension::points(20);
    f.styles[f.tree->root().index].overflow = Overflow::clip;
    std::vector<Point> offsets(f.tree->size());
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    const auto clipped = f.geometry();
    const Point hidden{clipped.bounds.origin.x + 6, 25};
    check(take(hit_test(f.hit(), f.point())).target == clipped.thumb && !take(hit_test(f.hit(), hidden)).target,
          "Clipped thumb captured input outside the ancestor viewport");
    check(!take(dispatcher.dispatch(f.hit(), {0us, PointerDown{{1}, hidden}})).handled, "Clipped thumb started a drag");
    paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    const auto clipped_image =
        take(frames.capture({3, f.viewport, scale, take(capture_extent(f.viewport, scale)), &paint, 0us}));
    const auto x = static_cast<std::uint32_t>(hidden.x * scale);
    const auto y = static_cast<std::uint32_t>(hidden.y * scale);
    check(clipped_image.pixels[(std::size_t(y) * clipped_image.extent.width + x) * 4 + 3] == 0,
          "Clipped thumb leaked CPU pixels outside the ancestor viewport");
    f.styles[f.tree->root().index].height = Dimension::points(4);
    f.layout = take(compute_layout({f.tree.get(), f.styles, f.viewport, &f.text, offsets}));
    (void)take(dispatcher.refresh(f.hit()));
    check(!dispatcher.dragging() && !f.geometry().enabled, "Fully clipped scrollbar retained capture");
}
}

int main() {
    try {
        validation();
        keyboard_and_degenerate();
        stale_geometry_and_eligibility();
        for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
            geometry_wheel_and_reveal(scale);
            capture_lifecycle(scale);
            observation_paint_and_replay(scale);
        }
        std::cout
            << "Controlled vertical scrollbar: geometry, commands, lifecycle and four-scale observations passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
