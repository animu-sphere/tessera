#include <tessera/input/modal.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/ui/authoring.hpp>
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
const ValidationContext actions{{"open", "under", "cancel", "confirm"}};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{200, 120};
    std::vector<Point> offsets;

    void build(bool open = true, bool mounted = true, bool trigger = true, bool empty = false) {
        std::vector<ui::Element> content;
        if (trigger)
            content.push_back(
                ui::Button("Reset", "open", {.node = {.id = "trigger", .style = {.height = Dimension::points(20)}}}));
        content.push_back(ui::Button(
            "Background", "under",
            {.node = {.id = "under", .style = {.height = Dimension::points(180), .background = Color{1, 0, 0, 1}}}}));
        std::vector<ui::Element> root{
            ui::ScrollView(std::move(content), {.id = "scroll", .style = {.height = Dimension::points(60)}})};
        if (mounted) {
            std::vector<ui::Element> buttons;
            if (!empty) {
                buttons.push_back(ui::Button("Cancel", "cancel",
                                             {.node = {.id = "cancel", .style = {.height = Dimension::points(20)}}}));
                buttons.push_back(ui::Button("Reset", "confirm",
                                             {.node = {.id = "confirm", .style = {.height = Dimension::points(20)}}}));
            }
            root.push_back(ui::Dialog("Reset settings?", std::move(buttons),
                                      {.node = {.id = "modal", .style = {.background = Color{0, 0, 1, 1}}},
                                       .panel = {.style = {.max_width = 120.f, .padding = Edges{4, 4, 4, 4}}},
                                       .open = open}));
        }
        auto authored = take(
            session.lower(ui::Column(std::move(root), {.id = "root", .style = {.overflow = Overflow::clip}}), actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        offsets.clear();
        recompute();
    }
    void recompute() { layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets})); }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(const char* id) const { return *tree->find(id); }
    const LayoutBox* box(const char* id) const {
        const auto found = tree->find(id);
        for (const auto& box : layout.boxes)
            if (found == box.node) return &box;
        return nullptr;
    }
    Point center(const char* id) const {
        const auto rect = box(id)->border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};

void geometry_paint_and_semantics() {
    Fixture f;
    f.build();
    check(f.box("modal")->border_box == Rect{{0, 0}, {200, 120}},
          "Modal must fill the root-content/viewport boundary outside normal flow");
    check(f.box("scroll")->border_box.size.height == 60 && f.box("scroll")->scroll->extent.height == 200,
          "Modal must not affect normal flow or scroll extent");
    const auto panel = f.box("modal-panel")->border_box;
    check(panel.size.width == 120 && panel.origin.x == 40 && panel.origin.y == (120 - panel.size.height) / 2,
          "Dialog panel must be centered");
    check(take(hit_test(f.hit(), {1, 1})).target == f.node("modal"), "Backdrop must win hits over background content");
    check(take(hit_test(f.hit(), f.center("confirm"))).target != f.node("under"),
          "Dialog hits must share its painted geometry");
    check(take(route_scroll(f.hit(), {{1, 1}, {0, 30}})).updates.empty(),
          "Wheel routing on the backdrop must not scroll the background");
    for (auto& style : f.styles) style.color.a = 0;
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {200, 120}, 1, {200, 120}, &paint}));
    const auto at = (1 * 200 + 1) * 4;
    check(std::array{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]} ==
              std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "CPU backdrop pixels must agree with modal hit order");
    const auto semantic = take(build_semantic_tree(f.semantic()));
    const auto dialog = std::find_if(semantic.nodes.begin(), semantic.nodes.end(),
                                     [](const SemanticNode& node) { return node.role == SemanticRole::dialog; });
    check(dialog != semantic.nodes.end() && dialog->name == "Reset settings?" &&
              dialog->name_source == NameSource::relationship,
          "Dialog must expose a title-labelled semantic role");
    check(has(request_semantic_action(f.semantic(), f.node("under"), "activate").diagnostics, "disabled_target",
              "/target"),
          "Semantic actions must not bypass the modal scope");
    check(take(request_semantic_action(f.semantic(), f.node("confirm"), "activate")).action == "confirm",
          "Dialog controls must retain ordinary semantic actions");
    const auto snapshot = take(capture_semantics(f.semantic(), 7));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Dialog role and disabled background state must round trip through Semantic JSON v1");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget dialog_target{SemanticRole::dialog, "Reset settings?"};
    check(take(resolve_target(inspection, dialog_target)) == f.node("modal"),
          "Inspection must resolve the ordinary dialog role/name target");
    const std::vector<UiInvariant> invariants{{"dialog-title", SemanticNameInvariant{dialog_target, "Reset settings?"}},
                                              {"background-blocked", EnabledInvariant{AuthorIdTarget{"under"}, false}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant observation must accept dialog roles and background exclusion");

    f.viewport = {80, 40};
    f.recompute();
    check(f.box("modal")->border_box.size == f.viewport && f.box("modal-panel")->border_box.size.width <= 80 &&
              f.box("modal-panel")->border_box.size.height <= 40 && f.box("modal-panel")->scroll_limit().y > 0,
          "Narrow Dialog must wrap and scroll instead of escaping the viewport");
    check(!take(route_scroll(f.hit(), {f.center("modal-panel"), {0, 20}})).updates.empty(),
          "Wheel input inside a constrained Dialog must scroll the panel");
    f.offsets.resize(f.tree->size());
    f.offsets[f.node("scroll").index] = {0, 30};
    f.recompute();
    check(f.box("modal")->border_box.origin == Point{}, "Background scrolling must not move the Modal");
    f.styles[0].padding = {2, 4, 6, 8};
    f.recompute();
    check(f.box("modal")->border_box == Rect{{8, 2}, {68, 32}}, "Modal placement must respect root padding");
    f.styles[0].display = Display::none;
    f.recompute();
    check(f.layout.boxes.empty() && validate(f.hit()).empty(), "Hidden root must safely suppress the Modal subtree");
}

void focus_scope_and_pointer_lifetime() {
    Fixture f;
    f.build();
    FocusDispatcher focus;
    auto focused = take(focus.dispatch(f.hit(), {1us, FocusNext{}})).focused;
    check(focused == f.node("cancel"), "First Tab must enter the Modal, not the background");
    focused = take(focus.dispatch(f.hit(), {2us, FocusNext{}})).focused;
    check(focused == f.node("confirm"), "Tab must visit the next dialog control");
    check(take(focus.dispatch(f.hit(), {3us, FocusNext{}})).focused == f.node("cancel"),
          "Tab must wrap within the Modal");
    check(take(focus.dispatch(f.hit(), {4us, FocusPrevious{}})).focused == f.node("confirm"),
          "Shift-Tab must wrap within the Modal");
    check(take(focus.dispatch(f.hit(), {5us, Navigate{Direction::down}})).focused == f.node("confirm"),
          "Gamepad navigation at the edge must not escape to background geometry");
    check(take(focus.dispatch(f.hit(), {6us, Activate{}})).actions.front().action == "confirm",
          "Gamepad activation must use ordinary actions");
    check(has(focus.focus(f.hit(), f.node("under")).diagnostics, "modal_target", "/target"),
          "Programmatic focus cannot bypass modal eligibility");
    f.build();
    check(take(focus.refresh(f.hit())).focused == f.node("confirm"),
          "Compatible replacement must retain focus by stable ID");

    f.build(false);
    PointerDispatcher pointer;
    (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center("trigger")}}));
    f.styles[f.node("modal").index].display = Display::stack;
    f.recompute();
    check(take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, {1, 1}}})).actions.empty(),
          "Opening a Modal in the same tree must invalidate a pressed background owner");
    check(take(pointer.dispatch(f.hit(), {3us, PointerDown{{1}, {1, 1}}})).actions.empty(),
          "Backdrop press must not activate background content");
    check(take(pointer.dispatch(f.hit(), {4us, PointerUp{{1}, f.center("confirm")}})).actions.empty(),
          "Dragging a backdrop press into a control must not activate it");
    f.build(true, true, true, true);
    check(!take(focus.refresh(f.hit())).focused && take(focus.dispatch(f.hit(), {7us, Activate{}})).actions.empty(),
          "A Modal without controls must clear focus rather than activate the background");
}

void transitions_and_rejection() {
    Fixture f;
    f.build(false);
    ModalDispatcher modal("modal");
    (void)take(modal.refresh(f.hit(), f.node("trigger")));
    f.build();
    auto result = take(modal.refresh(f.hit()));
    check(result.active && result.reset_pointer && result.focus == f.node("cancel"),
          "Opening must focus the first dialog control and cancel pre-existing presses");
    check(!take(modal.refresh(f.hit(), f.node("confirm"))).focus,
          "Ordinary refresh must not steal eligible dialog focus");
    const auto stale = f.node("confirm");
    result = take(modal.dispatch(f.hit(), {10us, Cancel{}}, f.node("confirm")));
    check(result.active && result.dismiss && result.consumed && result.reset_pointer && !result.focus,
          "Cancel requests controlled closure without prematurely focusing the blocked background");
    check(has(modal.dispatch(f.hit(), {9us, Cancel{}}).diagnostics, "event_order", "/timestamp"),
          "Out-of-order events must fail explicitly");
    InputEvent invalid{11us, PointerDown{{1}, {std::numeric_limits<float>::quiet_NaN(), 0}}};
    check(!modal.dispatch(f.hit(), invalid), "Invalid events must be rejected");
    check(!modal.refresh({}), "Incoherent snapshots must be rejected");
    f.build(false);
    check(has(modal.refresh(f.hit(), stale).diagnostics, "stale_target", "/focused"),
          "Stale focus must fail without consuming the close transition");
    result = take(modal.refresh(f.hit()));
    check(!result.active && result.reset_pointer && result.focus == f.node("trigger"),
          "Closing must restore only the surviving current-snapshot opener");
    (void)take(modal.refresh(f.hit(), f.node("trigger")));
    f.build();
    (void)take(modal.refresh(f.hit()));
    check(!take(modal.dispatch(f.hit(), {11us, PointerDown{{1}, {1, 1}}})).dismiss,
          "Backdrop presses must not confirm or dismiss a destructive dialog");
    check(!take(modal.dispatch(f.hit(), {12us, KeyDown{Key::escape, {.shift = true}}})).consumed,
          "Modified Escape is a host shortcut, not an implicit dismissal");
    check(take(modal.dispatch(f.hit(), {13us, KeyDown{Key::escape}})).dismiss,
          "Unmodified Escape must request dismissal");
    f.build(false, true, false);
    check(!take(modal.refresh(f.hit())).focus, "Removed opener must not produce an expired recovery handle");
    f.build(false);
    (void)take(modal.refresh(f.hit(), f.node("trigger")));
    f.build();
    (void)take(modal.refresh(f.hit()));
    f.build(false, false);
    result = take(modal.refresh(f.hit()));
    check(result.dismiss && result.reset_pointer && result.focus == f.node("trigger"),
          "Unmounting an open Modal must dismiss and recover to a surviving opener");
    f.build();
    (void)take(modal.refresh(f.hit()));
    f.styles[f.node("modal").index].visibility = Visibility::hidden;
    f.recompute();
    check(take(modal.refresh(f.hit())).dismiss, "Hidden Modal must request controlled dismissal");
    f.build(false);
    (void)take(modal.refresh(f.hit(), f.node("trigger")));
    f.build();
    (void)take(modal.refresh(f.hit()));
    f.styles[f.node("cancel").index].display = Display::none;
    f.recompute();
    check(take(modal.refresh(f.hit())).focus == f.node("confirm"),
          "Removed first control must recover to another eligible dialog control");
    f.styles[f.node("modal").index].visibility = Visibility::hidden;
    f.recompute();
    (void)take(modal.refresh(f.hit()));
    f.build(false);
    (void)take(modal.refresh(f.hit(), f.node("trigger")));
    f.build();
    (void)take(modal.refresh(f.hit()));
    f.styles[f.node("modal").index].visibility = Visibility::hidden;
    f.styles[f.node("trigger").index].visibility = Visibility::hidden;
    f.recompute();
    result = take(modal.refresh(f.hit()));
    check(result.dismiss && !result.focus, "Ineligible opener must not be restored");
    check(!ModalDispatcher("").refresh(f.hit()) && !ModalDispatcher("under").refresh(f.hit()),
          "Empty or non-Modal dispatcher IDs must fail");

    f.build(false);
    ModalDispatcher boundary("modal");
    (void)take(boundary.refresh(f.hit(), f.node("trigger")));
    f.build();
    result = take(boundary.dispatch(f.hit(), {1us, Activate{}}));
    check(result.active && result.consumed && result.focus == f.node("cancel"),
          "An unreconciled open transition must consume input aimed at the previous view");
    f.document.root.children.back().properties["disabled"] = true;
    f.tree = take(UiTree::create(f.document, actions));
    f.recompute();
    result = take(boundary.dispatch(f.hit(), {2us, Activate{}}));
    check(result.dismiss && result.consumed && result.reset_pointer && result.focus == f.node("trigger"),
          "Disabling an open owner must dismiss without routing stale activation to the background");
}

void documents_and_replay() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Modal marker and title relationship must round trip through UI JSON v1");
    auto document = f.document;
    auto& modal = document.root.children.back();
    modal.properties["modal"] = std::string("true");
    check(has(validate(document, actions), "property_type", "/root/children/1/properties/modal"),
          "Modal marker must reject coercion");
    document = f.document;
    document.root.children.back().id.reset();
    check(has(validate(document, actions), "modal_id", "/root/children/1/id"), "Modal must require a stable ID");
    document = f.document;
    std::swap(document.root.children.front(), document.root.children.back());
    check(has(validate(document, actions), "modal_order", "/root/children/0"), "Modal must be the last root child");
    document = f.document;
    auto nested = std::move(document.root.children.back());
    document.root.children.pop_back();
    document.root.children.front().children.push_back(std::move(nested));
    check(has(validate(document, actions), "modal_parent", "/root/children/0/children/2"),
          "Nested Modal must be rejected rather than behaving like an in-parent Stack");
    document = f.document;
    UiNode second;
    second.id = "second";
    second.properties["modal"] = true;
    document.root.children.push_back(std::move(second));
    check(has(validate(document, actions), "modal_count", "/root"), "Multiple Modal lifetimes are not supported");
    document = f.document;
    document.root.children.back().events["activate"] = "under";
    check(has(validate(document, actions), "modal_action", "/root/children/1/events/activate"),
          "Dialog actions belong to controls, not the layer");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {InputEvent{1us, FocusNext{}}, InputEvent{2us, Activate{}}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay must preserve modal geometry properties and styles");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 1 && output.actions.front().action == "cancel",
              "Replay's ordinary focus scope must not activate background controls");
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::dialog, "Reset settings?"}}};
        check(take(load_replay(take(save_replay(recording)))) == recording,
              "Replay role/name targets must accept the additive dialog role");
    }
}
} // namespace

int main() {
    try {
        geometry_paint_and_semantics();
        focus_scope_and_pointer_lifetime();
        transitions_and_rejection();
        documents_and_replay();
        std::cout << "Modal layout, scope, lifetime, semantics and replay checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
