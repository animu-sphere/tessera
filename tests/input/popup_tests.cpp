#include <tessera/input/popup.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantics.hpp>
#include <tessera/ui/authoring.hpp>
#include "../check.hpp"
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
const ValidationContext actions{{"open", "choose", "under"}};
struct Fixture {
    ui::Session session;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{100, 100};
    std::vector<Point> offsets;

    void build(bool open = true, bool tooltip = false, bool anchor = true, ui::PopupSide side = ui::PopupSide::bottom) {
        std::vector<ui::Element> content;
        if (anchor)
            content.push_back(ui::Button("Open", "open",
                                         {.node = {.id = "anchor",
                                                   .style = {.width = Dimension::points(20),
                                                             .height = Dimension::points(10),
                                                             .margin = Edges{20, 0, 0, 60}}}}));
        content.push_back(ui::Column({}, {.style = {.height = Dimension::points(120)}}));
        ui::PopupProps props{.node = {.id = "popup",
                                      .style = {.width = Dimension::points(60),
                                                .height = Dimension::points(30),
                                                .background = Color{0, 0, 1, 1}}},
                             .open = open,
                             .side = side};
        const auto form = ui::Column(
            {ui::ScrollView(
                 std::move(content),
                 {.id = "scroll", .style = {.width = Dimension::points(80), .height = Dimension::points(50)}}),
             ui::Button("Under", "under",
                        {.node = {.id = "under",
                                  .style = {.height = Dimension::points(50), .background = Color{1, 0, 0, 1}}}}),
             tooltip ? ui::Tooltip("anchor", "Help", props)
                     : ui::Popup("anchor",
                                 {ui::Button("Choose", "choose",
                                             {.node = {.id = "item", .style = {.height = Dimension::points(30)}}})},
                                 props)});
        auto authored = take(session.lower(form, actions));
        tree = take(UiTree::create(authored.document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        offsets.clear();
        recompute();
    }
    void recompute() { layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets})); }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    NodeHandle node(const char* id) const { return *tree->find(id); }
    ResolvedStyle& style(const char* id) { return styles[node(id).index]; }
    const LayoutBox* box(const char* id) const {
        const auto found = tree->find(id);
        for (const auto& box : layout.boxes)
            if (found == box.node) return &box;
        return nullptr;
    }
};

void placement_paint_and_scroll() {
    Fixture f;
    f.build();
    check(f.box("popup")->border_box == Rect{{40, 34}, {60, 30}},
          "Bottom popup must shift horizontally into the viewport without taking flow space");
    check(f.box("under")->border_box.origin.y == 50 && f.box("scroll")->scroll->extent.height == 150,
          "Popup must not affect ordinary flow or scroll extent");
    check(!f.box("popup")->clip && f.box("anchor")->clip == Rect{{0, 0}, {80, 50}},
          "Root popup must not inherit the anchor's scrolling clip");
    check(take(hit_test(f.hit(), {45, 55})).target == f.node("item"),
          "Popup outside the scroll clip must receive the topmost hit");
    for (auto& style : f.styles) style.color.a = 0;
    const auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    CpuFrameCapture frames;
    const auto image = take(frames.capture({0, {100, 100}, 1, {100, 100}, &paint}));
    const auto at = (55 * 100 + 45) * 4;
    check(std::array{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]} ==
              std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "CPU paint must agree with the popup hit beyond ordinary clipping");
    const auto semantic = take(build_semantic_tree({f.tree.get(), f.styles, &f.layout}));
    check(!semantic.nodes.empty(), "Popup snapshot must support ordinary semantics");

    f.offsets.resize(f.tree->size());
    f.offsets[f.node("scroll").index] = {0, 10};
    f.recompute();
    check(f.box("popup")->border_box.origin == Point{40, 24}, "Popup must follow scrolled anchor coordinates");
    f.offsets[f.node("scroll").index] = {0, 40};
    f.recompute();
    check(!f.box("popup") && validate(f.hit()).empty(), "Fully clipped anchor must suppress the entire popup subtree");
    f.build(true, false, false);
    check(!f.box("popup") && validate(f.hit()).empty(),
          "Unmounted anchor must suppress its popup with a coherent snapshot");
    f.build(false);
    check(!f.box("popup"), "Controlled closed popup must have no layout");
}

void sides_and_viewport_limits() {
    for (const auto side : {ui::PopupSide::bottom, ui::PopupSide::top, ui::PopupSide::left, ui::PopupSide::right}) {
        Fixture f;
        f.build(true, false, true, side);
        f.viewport = {200, 200};
        f.style("scroll").width = f.style("scroll").height = Dimension::points(180);
        f.style("anchor").margin = {80, 0, 0, 80};
        f.style("popup").width = Dimension::points(40);
        f.style("popup").height = Dimension::points(20);
        f.recompute();
        const Point expected = side == ui::PopupSide::bottom ? Point{80, 94}
                               : side == ui::PopupSide::top  ? Point{80, 56}
                               : side == ui::PopupSide::left ? Point{36, 80}
                                                             : Point{104, 80};
        check(f.box("popup")->border_box.origin == expected,
              "All four preferred sides must use the same anchor coordinates");
    }
    Fixture f;
    f.build();
    f.style("anchor").margin.top = 40;
    f.recompute();
    check(f.box("popup")->border_box.origin.y == 54, "Preferred side must remain when it fits");
    f.viewport.height = 65;
    f.recompute();
    check(f.box("popup")->border_box.origin.y == 6, "Popup must flip above when below no longer fits");
    f.viewport = {40, 20};
    f.style("anchor").margin = {};
    f.style("popup").width = {};
    f.style("popup").height = {};
    f.recompute();
    check(f.box("popup")->border_box.size.width <= 40 && f.box("popup")->border_box.size.height <= 20,
          "Automatic popup size must be bounded by the viewport");
    const LayoutInput input{f.tree.get(), f.styles, f.viewport, &f.text};
    check(f.layout == take(compute_layout(input)), "Popup layout must be deterministic");
    f.viewport = {100, 100};
    f.build(true, true);
    f.style("popup").width = f.style("popup").height = {};
    f.style("popup").max_width = 16;
    f.recompute();
    check(f.box("popup")->border_box.size == Size{16, 40},
          "Automatic tooltip height must remeasure wrapped text at its assigned width");
    f.styles[0].display = Display::none;
    f.recompute();
    check(f.layout.boxes.empty() && validate(f.hit()).empty(), "A display-none root must suppress every popup safely");
}

void popup_focus_and_dismissal() {
    Fixture f;
    f.build(false);
    PopupDispatcher popup("popup");
    check(!take(popup.refresh(f.hit())).focus, "Closed popup must not move focus");
    f.build();
    check(take(popup.refresh(f.hit())).focus == f.node("item"), "Opening must request the first eligible item");
    check(!take(popup.refresh(f.hit())).focus, "Refresh must not repeatedly steal focus");
    auto result = take(popup.dispatch(f.hit(), {1us, PointerDown{{1}, {45, 55}}}));
    check(!result.dismiss && !result.consumed, "Inside press must reach ordinary pointer dispatch");
    result = take(popup.dispatch(f.hit(), {2us, PointerDown{{1}, {10, 70}}}));
    check(result.dismiss && result.consumed && result.focus == f.node("anchor"),
          "Outside press must dismiss, consume the press and restore the trigger");
    f.build(false);
    check(take(popup.refresh(f.hit())).focus == f.node("anchor"), "Accepted close must recover by stable anchor ID");
    f.build();
    (void)take(popup.refresh(f.hit()));
    check(take(popup.dispatch(f.hit(), {3us, Cancel{}})).dismiss, "Gamepad Cancel must dismiss");
    check(take(popup.dispatch(f.hit(), {4us, KeyDown{Key::escape}})).dismiss, "Unmodified Escape must dismiss");
    check(!take(popup.dispatch(f.hit(), {5us, KeyDown{Key::escape, {.control = true}}})).dismiss,
          "Modified Escape must remain available to the host");
    const auto stale = popup.dispatch(f.hit(), {4us, Cancel{}});
    check(!stale && has(stale.diagnostics, "event_order", "/timestamp"), "Invalid event order must be explicit");
    f.build(true, false, false);
    result = take(popup.refresh(f.hit()));
    check(result.dismiss && !result.focus, "Anchor removal must close without restoring an expired target");
    f.build();
    (void)take(popup.refresh(f.hit()));
    f.style("anchor").visibility = Visibility::hidden;
    f.recompute();
    result = take(popup.refresh(f.hit()));
    check(result.dismiss && !result.focus, "Hidden anchor must close without restoring ineligible focus");
    f.build();
    (void)take(popup.refresh(f.hit()));
    f.style("anchor").visibility = Visibility::visible;
    f.styles[f.node("anchor").index].display = Display::none;
    f.recompute();
    check(take(popup.refresh(f.hit())).dismiss, "Display-none anchor must dismiss");
    PopupDispatcher wrong("under");
    const auto rejected = wrong.refresh(f.hit());
    check(!rejected && has(rejected.diagnostics, "popup_target", "/id"),
          "An ordinary node is not a Popup controller target");
    TooltipDispatcher unnamed("");
    const auto unnamed_result = unnamed.refresh(f.hit());
    check(!unnamed_result && has(unnamed_result.diagnostics, "invalid_identifier", "/id"),
          "Empty controller IDs must fail");
}

void tooltip_hover_focus_and_passthrough() {
    Fixture f;
    f.build(true, true);
    TooltipDispatcher tooltip("anchor");
    check(!take(tooltip.refresh(f.hit())).visible, "Tooltip starts hidden without hover or focus");
    check(take(tooltip.dispatch(f.hit(), {1us, PointerMove{{1}, {65, 25}}})).visible,
          "Hovering an anchor descendant must show the tooltip");
    PointerDispatcher pointer;
    (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, {45, 55}}}));
    const auto activated = take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, {45, 55}}}));
    check(activated.actions.size() == 1 && activated.actions[0].target == f.node("under"),
          "Tooltip and all its descendants must be pointer-transparent");
    const auto semantics = take(build_semantic_tree({f.tree.get(), f.styles, &f.layout}));
    bool help_enabled = false;
    for (const auto& entry : semantics.nodes)
        if (entry.name == "Help" && entry.role == SemanticRole::text) help_enabled = entry.enabled && !entry.focusable;
    check(help_enabled, "Pointer-transparent help must retain enabled, nonfocusable semantic text");
    check(!take(tooltip.dispatch(f.hit(), {2us, Cancel{}}, f.node("anchor"))).visible,
          "Cancel must suppress a hovered and focused tooltip");
    check(!take(tooltip.refresh(f.hit(), f.node("anchor"))).visible,
          "Stationary hover/focus must not immediately reopen dismissed help");
    check(!take(tooltip.dispatch(f.hit(), {3us, PointerMove{{1}, {1, 90}}})).visible,
          "Leaving both hover and focus must clear suppression");
    check(take(tooltip.refresh(f.hit(), f.node("anchor"))).visible, "Keyboard/gamepad focus must show help");
    const auto expired = f.node("anchor");
    f.build(true, true);
    const auto rejected = tooltip.refresh(f.hit(), expired);
    check(!rejected && has(rejected.diagnostics, "stale_target", "/focused"), "Expired focus must fail explicitly");
    (void)take(tooltip.dispatch(f.hit(), {4us, PointerMove{{1}, {65, 25}}}));
    check(!take(tooltip.dispatch(f.hit(), {5us, PointerCancel{{1}}})).visible, "Pointer cancellation must clear hover");
    f.style("anchor").visibility = Visibility::hidden;
    f.recompute();
    check(!take(tooltip.refresh(f.hit(), f.node("anchor"))).visible, "Hidden anchors must not expose help");
}

void validation_and_serialization() {
    Fixture f;
    f.build();
    const auto saved = take(save_document(f.tree->document(), actions));
    check(take(load_document(saved, actions)) == f.tree->document(), "Popup properties must round trip in version 1");
    auto invalid = f.tree->document();
    invalid.root.children.back().properties["popup_side"] = std::string("diagonal");
    check(has(validate(invalid, actions), "popup_side", "/root/children/2/properties/popup_side"),
          "Unknown side must be diagnosed");
    invalid = f.tree->document();
    invalid.root.children.back().properties["popup_gap"] = std::numeric_limits<double>::infinity();
    check(has(validate(invalid, actions), "popup_gap", "/root/children/2/properties/popup_gap"),
          "Non-finite gap must be diagnosed");
    invalid.root.children.back().properties["popup_gap"] = -1.0;
    check(has(validate(invalid, actions), "popup_gap", "/root/children/2/properties/popup_gap"),
          "Negative gap must be diagnosed");
    invalid = f.tree->document();
    invalid.root.children.back().properties["popup_anchor"] = std::string("item");
    check(has(validate(invalid, actions), "popup_anchor", "/root/children/2/properties/popup_anchor"),
          "Popup descendants cannot be cyclic anchors");
    invalid = f.tree->document();
    std::swap(invalid.root.children[1], invalid.root.children[2]);
    check(has(validate(invalid, actions), "popup_order", "/root/children/2"),
          "Ordinary content after a popup must be rejected");
    invalid = f.tree->document();
    auto nested = std::move(invalid.root.children.back());
    invalid.root.children.pop_back();
    invalid.root.children[0].children.push_back(std::move(nested));
    check(has(validate(invalid, actions), "popup_parent", "/root/children/0/children/2"),
          "Nested popup placement must require explicit root composition");
}
} // namespace

int main() {
    try {
        placement_paint_and_scroll();
        sides_and_viewport_limits();
        popup_focus_and_dismissal();
        tooltip_hover_focus_and_passthrough();
        validation_and_serialization();
        std::cout << "Popup/Tooltip placement, clipping, input, lifetime and serialization checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
