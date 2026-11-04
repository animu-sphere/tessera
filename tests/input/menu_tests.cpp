#include <tessera/ui/authoring.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
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
const ValidationContext actions{{"open", "target", "one", "two"}};
struct Options {
    bool open = true;
    bool context = false;
    bool anchor = true;
    bool disabled = false;
    bool items_disabled = false;
    bool modal = false;
    Point position{220, 160};
};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{240, 180};
    std::vector<Point> offsets;
    void build(Options options = {}) {
        std::vector<ui::Element> content;
        if (options.anchor) {
            content.push_back(ui::Button(
                "Commands", "open",
                {.node = {.id = "opener", .style = {.height = Dimension::points(24)}}, .disabled = options.disabled}));
            content.push_back(ui::Button(
                "Asset", "target",
                {.node = {.id = "asset", .style = {.height = Dimension::points(56)}}, .disabled = options.disabled}));
        }
        content.push_back(ui::Column({}, {.style = {.height = Dimension::points(180)}}));
        ui::PopupProps props{.node = {.id = "menu",
                                      .style = {.width = Dimension::points(120),
                                                .height = Dimension::points(100),
                                                .background = Color{0, 0, 1, 1}}},
                             .open = options.open};
        std::vector<ui::MenuItem> items{{"one", "First", "one", options.items_disabled},
                                        {"off", "Unavailable", "one", true},
                                        {"two", "Last", "two", options.items_disabled}};
        std::vector<ui::Element> children{
            ui::ScrollView(
                std::move(content),
                {.id = "scroll", .style = {.width = Dimension::points(160), .height = Dimension::points(100)}}),
            ui::Button("Outside", "target",
                       {.node = {.id = "outside",
                                 .style = {.height = Dimension::points(80), .background = Color{1, 0, 0, 1}}}}),
            options.context ? ui::ContextMenu("Commands", "asset", options.position, items, props)
                            : ui::Menu("Commands", "opener", items, props)};
        if (options.modal)
            children.push_back(
                ui::Dialog("Confirm", {ui::Button("OK", "target")}, {.node = {.id = "dialog"}, .open = true}));
        auto authored = take(session.lower(
            ui::Column(std::move(children), {.id = "root", .style = {.overflow = Overflow::clip}}), actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        offsets.assign(tree->size(), {});
        recompute();
    }
    void recompute() { layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets})); }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox* box(std::string_view id) const {
        const auto target = tree->find(id);
        const auto at = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                     [&](const LayoutBox& box) { return box.node == target; });
        return at == layout.boxes.end() ? nullptr : &*at;
    }
    Point center(std::string_view id) const {
        const auto& rect = box(id)->border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing menu semantic entry");
    return *at;
}

void projection_and_codecs() {
    Fixture f;
    f.build({.context = true});
    const auto semantics = take(build_semantic_tree(f.semantic()));
    check(entry(semantics, "menu").role == SemanticRole::menu && entry(semantics, "menu").name == "Commands",
          "Menu must expose its explicit label");
    const auto& first = entry(semantics, "menu-item-one");
    check(first.role == SemanticRole::menu_item && first.name == "First" && !first.selected && !first.expanded &&
              semantics.nodes[first.parent].id == "menu",
          "Menu items must be named commands, not selected list rows or generic buttons");
    check(entry(semantics, "menu-item-off").actions.empty(), "Disabled commands expose no action");
    check(take(request_semantic_action(f.semantic(), first.node, "activate")).action == "one",
          "Menu activation must use the ordinary semantic action");
    const auto snapshot = take(capture_semantics(f.semantic(), 7));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot, "Menu roles must round trip");
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Point placement and menu properties must survive UI JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget target{SemanticRole::menu_item, "First"};
    check(take(resolve_target(inspection, target)) == first.node, "Inspection must resolve menu command roles");
    const std::vector<UiInvariant> invariants{{"command-name", SemanticNameInvariant{target, "First"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept menu role targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{target}, InputEvent{1us, Navigate{Direction::up}}, InputEvent{2us, Activate{}},
                           ReplaySemanticAction{target, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain menu targets and placement");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "two" && output.actions[1].action == "one",
              "Replay must share ordered menu focus and semantic activation");
    }
    const auto stale = first.node;
    f.build({.open = false, .context = true});
    check(has(request_semantic_action(f.semantic(), stale, "activate").diagnostics, "stale_target", "/target"),
          "Replacement must reject old menu actions");
    check(has(request_semantic_action(f.semantic(), f.node("menu-item-one"), "activate").diagnostics, "hidden_target",
              "/target"),
          "Closed menus expose no item actions");
}

void placement_and_paint() {
    Fixture f;
    f.build({.context = true});
    check(f.box("menu")->border_box == Rect{{120, 80}, {120, 100}},
          "Context position must shift inside both viewport edges");
    check(!f.box("menu")->clip || f.box("menu")->clip == Rect{{0, 0}, {240, 180}},
          "Menu must escape the target's scrolling clip while respecting the root");
    check(take(hit_test(f.hit(), f.center("menu-item-two"))).target.has_value(),
          "Context commands must remain hittable beyond the anchor's clip");
    for (auto& style : f.styles) style.color.a = 0;
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {240, 180}, 1, {240, 180}, &paint}));
    const auto at = (110 * 240 + 125) * 4;
    check(std::array{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]} ==
              std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "CPU pixels must put the menu above ordinary content");
    const auto hit = take(hit_test(f.hit(), {125, 110})).target;
    check(hit && hit->index >= f.node("menu").index, "Menu paint and topmost hit order must agree");
    f.viewport = {180, 140};
    f.recompute();
    check(f.box("menu")->border_box.origin == Point{60, 40}, "Resize must reclamp the point placement");
    f.offsets[f.node("scroll").index] = {0, 20};
    f.recompute();
    check(f.box("menu")->border_box.origin == Point{60, 40},
          "A context position is retained while its visible target scrolls");
    f.offsets[f.node("scroll").index] = {0, 100};
    f.recompute();
    check(!f.box("menu"), "A fully clipped target suppresses its context menu");
    f.build({.context = true, .anchor = false});
    check(!f.box("menu"), "An unmounted target suppresses context placement");
    f.build({.context = true, .position = {-10, -20}});
    check(f.box("menu")->border_box.origin == Point{}, "Negative point coordinates clamp to the root boundary");
}

void navigation_and_dismissal() {
    Fixture f;
    f.build({.open = false});
    MenuDispatcher menu("menu");
    check(!take(menu.refresh(f.hit())).focus, "A closed menu must not take focus");
    f.build();
    check(take(menu.refresh(f.hit())).focus == f.node("menu-item-one"), "Opening must focus the first enabled command");
    check(!take(menu.refresh(f.hit())).focus, "Refresh must not repeatedly steal focus");
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("menu-item-one")));
    check(take(focus.dispatch(f.hit(), {1us, Navigate{Direction::up}})).focused == f.node("menu-item-two"),
          "Up at the first menu item must wrap and skip disabled items");
    check(take(focus.dispatch(f.hit(), {2us, Navigate{Direction::down}})).focused == f.node("menu-item-one"),
          "Down at the last item must wrap");
    check(take(focus.dispatch(f.hit(), {3us, KeyDown{Key::end}})).focused == f.node("menu-item-two") &&
              take(focus.dispatch(f.hit(), {4us, KeyDown{Key::home}})).focused == f.node("menu-item-one"),
          "Home/End must use menu order");
    const auto keyboard = take(focus.dispatch(f.hit(), {5us, Activate{}})).actions;
    PointerDispatcher pointer;
    (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center("menu-item-one")}}));
    const auto clicked = take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, f.center("menu-item-one")}})).actions;
    check(clicked == keyboard && clicked.size() == 1,
          "Pointer and keyboard/gamepad must request the same command once");
    check(take(menu.dispatch(f.hit(), {1us, PointerMove{{1}, f.center("menu-item-two")}})).focus ==
              f.node("menu-item-two"),
          "Hover must request the eligible menu item focus");
    check(!take(menu.dispatch(f.hit(), {2us, PointerMove{{1}, f.center("menu-item-off")}})).focus,
          "Hover must skip disabled commands");
    auto result = take(menu.dispatch(f.hit(), {3us, PointerDown{{1}, {200, 150}}}));
    check(result.dismiss && result.consumed && result.focus == f.node("opener"),
          "Outside press must close, prevent click-through and restore the trigger");
    result = take(menu.dispatch(f.hit(), {4us, Cancel{}}));
    check(result.dismiss && result.consumed, "Gamepad Cancel must dismiss");
    result = take(menu.dispatch(f.hit(), {5us, KeyDown{Key::escape}}));
    check(result.dismiss && result.consumed, "Escape must dismiss");
    result = take(menu.dispatch(f.hit(), {6us, FocusNext{}}));
    check(result.dismiss && !result.consumed && result.focus == f.node("opener"),
          "Tab traversal must close before the host forwards the focus command");
    f.build({.items_disabled = true});
    MenuDispatcher empty("menu");
    check(!take(empty.refresh(f.hit())).focus, "All-disabled menus must not invent a focus target");
    check(take(empty.dispatch(f.hit(), {1us, Cancel{}})).dismiss, "An all-disabled menu must still be dismissible");
    f.build({.disabled = true});
    check(take(menu.refresh(f.hit())).dismiss && !take(menu.refresh(f.hit())).focus,
          "Disabled opener must dismiss without restoring ineligible focus");
}

void scroll_and_reopen() {
    Fixture f;
    f.viewport = {160, 70};
    f.build({.context = true});
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("menu-item-one")));
    const auto last = take(focus.dispatch(f.hit(), {1us, KeyDown{Key::end}})).focused;
    check(last == f.node("menu-item-two"), "End must reach an initially clipped last command");
    const auto updates = take(scroll_into_view(f.hit(), *last));
    check(!updates.empty() && updates.front().container == f.node("menu"),
          "Menu's own scroll viewport must reveal the focused command");
    for (const auto& update : updates) f.offsets[update.container.index] = update.offset;
    f.recompute();
    check(take(scroll_into_view(f.hit(), *last)).empty(), "Revealed command must remain in view");
    MenuDispatcher menu("menu");
    const Point position{20, 40};
    f.viewport = {240, 180};
    f.build({.context = true, .position = {180, 130}});
    (void)take(menu.refresh(f.hit()));
    const auto result = take(
        menu.dispatch(f.hit(), {2us, PointerDown{{1}, position, PointerButton::secondary}}, f.node("menu-item-two")));
    check(result.open_at == position && result.focus == f.node("menu-item-one"),
          "Reopening an already open context menu must reset item focus");
    f.build({.context = true, .position = position});
    (void)take(menu.refresh(f.hit()));
    f.build({.open = false, .context = true});
    check(take(menu.refresh(f.hit())).focus == f.node("asset"),
          "Controlled closure must restore the surviving context target");
    f.build({.context = true});
    (void)take(menu.refresh(f.hit()));
    f.build({.context = true, .anchor = false});
    const auto removed = take(menu.refresh(f.hit()));
    check(removed.dismiss && !removed.focus, "Removed targets must dismiss without stale focus restoration");
}

void context_triggers_and_rejection() {
    Fixture f;
    f.build({.open = false, .context = true});
    MenuDispatcher menu("menu");
    (void)take(menu.refresh(f.hit()));
    const Point position{80, 50};
    auto result = take(menu.dispatch(f.hit(), {1us, PointerDown{{1}, position, PointerButton::secondary}}));
    check(result.open_at == position && result.consumed && !result.dismiss,
          "Secondary press in the target must request a controlled context opening");
    check(!take(menu.dispatch(f.hit(), {2us, PointerDown{{1}, position}})).open_at,
          "Primary press must retain the target's ordinary action");
    check(!take(menu.dispatch(f.hit(), {3us, PointerDown{{1}, {200, 150}, PointerButton::secondary}})).open_at,
          "Secondary press outside the target must not open");
    result = take(menu.dispatch(f.hit(), {4us, KeyDown{Key::f10, {true}}}, f.node("asset")));
    check(result.open_at == Point{0, 80} && result.consumed, "Shift+F10 must open at the visible focused target");
    check(!take(menu.dispatch(f.hit(), {5us, KeyDown{Key::f10, {true}, true}}, f.node("asset"))).open_at,
          "Repeating the context key must not reopen");
    check(!take(menu.dispatch(f.hit(), {6us, KeyDown{Key::f10, {true}}}, f.node("outside"))).open_at,
          "Context key requires focus in its own target");
    const auto stale = f.node("asset");
    f.build({.context = true, .position = position});
    check(take(menu.refresh(f.hit())).focus == f.node("menu-item-one"), "Context opening must focus the first command");
    check(has(menu.dispatch(f.hit(), {100us, Cancel{}}, stale).diagnostics, "stale_target", "/focused"),
          "Stale focused handles must be rejected before dispatcher state changes");
    check(has(menu.dispatch(f.hit(), {5us, Cancel{}}).diagnostics, "event_order", "/timestamp"),
          "Out-of-order menu events must be rejected");
    check(take(menu.dispatch(f.hit(), {7us, Cancel{}})).dismiss,
          "A rejected call must not advance the accepted event clock");
    check(has(MenuDispatcher("asset").refresh(f.hit()).diagnostics, "menu_target", "/id"),
          "Ordinary controls cannot stand in for menus");
    f.build({.open = false, .context = true, .disabled = true});
    check(!take(menu.dispatch(f.hit(), {8us, PointerDown{{1}, position, PointerButton::secondary}})).open_at,
          "Disabled targets cannot open context menus");
    f.build({.open = false, .context = true, .modal = true});
    check(!take(menu.dispatch(f.hit(), {9us, KeyDown{Key::f10, {true}}}, f.node("asset"))).open_at,
          "A background target blocked by a Modal cannot open a context menu");
}

void validation_errors() {
    ui::Session session;
    check(!session.lower(ui::Column({ui::Menu("Commands", "anchor", {{"a", "One", "one"}})}), actions),
          "Menu authoring requires a stable ID");
    check(!session.lower(ui::Column({ui::Menu("Commands", "anchor", {{"a", "One", "one"}, {"a", "Two", "two"}},
                                              {.node = {.id = "menu"}})}),
                         actions),
          "Duplicate command keys must be rejected");
    check(!session.lower(ui::Column({ui::Menu("Commands", "anchor", {{"a", "", "one"}}, {.node = {.id = "menu"}})}),
                         actions),
          "Menu labels must be nonempty");
    Fixture f;
    f.build({.context = true});
    auto malformed = f.document;
    malformed.root.children.back().properties.erase("popup_y");
    check(has(validate(malformed, actions), "popup_position", "/root/children/2/properties"),
          "Point coordinates must be paired");
    malformed = f.document;
    malformed.root.children.back().properties["popup_x"] = std::numeric_limits<double>::max();
    check(has(validate(malformed, actions), "popup_position", "/root/children/2/properties/popup_x"),
          "Point coordinates must fit finite float geometry");
    malformed = f.document;
    malformed.root.children.back().children[1].properties["selected"] = true;
    check(has(validate(malformed, actions), "collection_state", "/root/children/2/children/1/properties/selected"),
          "Command menus cannot claim list selection");
    malformed = f.document;
    malformed.root.children.back().children[1].events.clear();
    check(has(validate(malformed, actions), "menu_action", "/root/children/2/children/1/events/activate"),
          "Menu items require a command action");
    malformed = f.document;
    malformed.root.children.back().children[1].children.push_back(UiNode{});
    check(has(validate(malformed, actions), "menu_child", "/root/children/2/children/1/children/1"),
          "Menu items cannot contain nested controls");
}
} // namespace

int main() {
    try {
        projection_and_codecs();
        placement_and_paint();
        navigation_and_dismissal();
        scroll_and_reopen();
        context_triggers_and_rejection();
        validation_errors();
        std::cout << "Menu/ContextMenu placement, commands, focus, triggers, semantics and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
