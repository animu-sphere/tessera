#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

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
const ValidationContext actions{{"before", "after", "color.toggle", "color.area", "color.hue"}};
const ui::ColorPickerActions commands{"color.toggle", "color.area", "color.hue"};
struct Options {
    HsvColor color{210, 0.5, 0.8};
    bool open = false;
    bool disabled = false;
};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{480, 360};
    void build(Options options = {}) {
        auto parts = ui::ColorPicker("Display color", options.color, commands,
                                     {.node = {.id = "color"},
                                      .panel = {.style = {.padding = Edges{8, 8, 8, 8}}},
                                      .open = options.open,
                                      .disabled = options.disabled});
        auto authored =
            take(session.lower(ui::Column({ui::Button("Before", "before", {.node = {.id = "before"}}), parts.field,
                                           ui::Button("After", "after", {.node = {.id = "after"}}), parts.panel},
                                          {.id = "root", .style = {.align = Align::start, .gap = 8.f}}),
                               actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, viewport, &text, {}}));
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox* find_box(std::string_view id) const {
        const auto target = node(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == target; });
        return found == layout.boxes.end() ? nullptr : &*found;
    }
    const LayoutBox& box(std::string_view id) const {
        const auto* found = find_box(id);
        check(found != nullptr, "Missing layout box");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
    // Content-box point at the given fractions.
    Point at(std::string_view id, double x, double y) const {
        const auto rect = box(id).content_box();
        return {rect.origin.x + static_cast<float>(x) * rect.size.width,
                rect.origin.y + static_cast<float>(y) * rect.size.height};
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing color picker semantic entry");
    return *at;
}
const UiNode* search(const UiNode& node, std::string_view id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children)
        if (const auto* found = search(child, id)) return found;
    return nullptr;
}
const UiNode& find(const UiNode& node, std::string_view id) {
    const auto* found = search(node, id);
    check(found != nullptr, "Missing authored node");
    return *found;
}
Property text(const char* value) { return Property{std::string(value)}; }
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(const Color& a, const Color& b, double tolerance = 1e-6) {
    return near(a.r, b.r, tolerance) && near(a.g, b.g, tolerance) && near(a.b, b.b, tolerance) &&
           near(a.a, b.a, tolerance);
}

void color_model() {
    check(near(to_color({0, 1, 1}), {1, 0, 0, 1}) && near(to_color({120, 1, 1}), {0, 1, 0, 1}) &&
              near(to_color({240, 1, 1}), {0, 0, 1, 1}) && near(to_color({60, 1, 1}), {1, 1, 0, 1}) &&
              near(to_color({180, 1, 1}), {0, 1, 1, 1}) && near(to_color({300, 1, 1}), {1, 0, 1, 1}),
          "Primary and secondary hues map to their sRGB corners");
    check(near(to_color({360, 1, 1}), to_color({0, 1, 1})) && near(to_color({30, 1, 1}), {1, .5f, 0, 1}) &&
              near(to_color({210, .5, .8}), {.4f, .6f, .8f, 1}) && near(to_color({123, 0, .5}), {.5f, .5f, .5f, 1}) &&
              near(to_color({77, .7, 0}), {0, 0, 0, 1}),
          "Hue 360 equals 0; saturation and value scale the sRGB components; hue is kept for grays");
    check(valid({0, 0, 0}) && valid({360, 1, 1}) && !valid({-1, 0, 0}) && !valid({361, 0, 0}) && !valid({0, 1.5, 0}) &&
              !valid({0, 0, -0.1}) && !valid({NAN, 0, 0}) && !valid({0, INFINITY, 0}) &&
              to_color({400, 2, 2}) == Color{0, 0, 0, 1},
          "Only finite in-range HSV values are valid; invalid ones present black");
    check(format_hex_color({.4f, .6f, .8f, 1}) == "#6699CC" && format_hex_color({.5f, .5f, .5f, 0}) == "#808080" &&
              format_hex_color({-1, 2, NAN, 1}) == "#00FF00" && format_hex_color({1, 0, 0.0019608f, 1}) == "#FF0001",
          "Hex text rounds halves upward, clamps, ignores alpha and reads nonfinite components as zero");
}

void composition_and_semantics() {
    Fixture f;
    f.build();
    const auto& field = find(f.document.root, "color");
    check(field.properties.at("collection_role") == text("color_picker") &&
              field.properties.at("expanded") == Property{false} &&
              field.properties.at("edit_value") == text("#6699CC") &&
              field.properties.at("labelled_by") == Property{NodeReference{"color-title"}} &&
              field.events.at("activate") == "color.toggle" && field.children.size() == 3 &&
              find(field, "color-title").properties.at("text") == text("Display color") &&
              find(field, "color-value").properties.at("text") == text("#6699CC"),
          "The trigger carries its role, state, hex value, label and toggle action");
    const auto& panel = f.document.root.children.back();
    check(panel.id == "color-panel" && panel.properties.at("collection_role") == text("group") &&
              panel.properties.at("popup_anchor") == text("color") &&
              panel.properties.at("labelled_by") == Property{NodeReference{"color-panel-title"}} &&
              panel.children.size() == 3 && find(panel, "color-area").events.at("activate") == "color.area" &&
              find(panel, "color-hue").events.at("activate") == "color.hue" &&
              find(panel, "color-area-label").properties.at("text") == text("Saturation 50%, value 80%") &&
              find(panel, "color-hue-label").properties.at("text") == text("Hue 210 degrees"),
          "The panel is a root-sibling group holding the labelled area and hue strip");
    check(f.styles[f.node("color-swatch").index].background == Color{.4f, .6f, .8f, 1} &&
              f.styles[f.node("color-area").index].background == to_color({210, 1, 1}),
          "The swatch shows the color and the area its pure hue");
    check(!f.find_box("color-panel") && !f.find_box("color-area"), "A closed panel is not displayed");
    const auto closed = take(build_semantic_tree(f.semantic()));
    const auto& trigger = entry(closed, "color");
    check(trigger.role == SemanticRole::color_picker && trigger.name == "Display color" &&
              trigger.name_source == NameSource::relationship && trigger.value == "#6699CC" &&
              trigger.expanded == false && trigger.focusable &&
              trigger.actions == std::vector<SemanticAction>{{"activate", "color.toggle"}},
          "The trigger projects a named color_picker with its value and collapsed state");
    check(std::none_of(closed.nodes.begin(), closed.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text || node.role == SemanticRole::group;
                       }),
          "The trigger's swatch and value are presentational and a closed panel has no entries");

    f.build({.open = true});
    const auto& anchor = f.box("color").border_box;
    const auto& popup = f.box("color-panel").border_box;
    const auto area = f.box("color-area").content_box();
    const auto hue = f.box("color-hue").content_box();
    check(popup.origin.x == anchor.origin.x && popup.origin.y == anchor.origin.y + anchor.size.height + 4 &&
              area.size == Size{160, 120} && hue.size == Size{160, 12} && hue.origin.y == area.origin.y + 128,
          "An open panel sits below the trigger with the area above the hue strip");
    const auto thumb = f.box("color-area-thumb").border_box;
    const auto hue_thumb = f.box("color-hue-thumb").border_box;
    check(thumb.origin.x + 6 == area.origin.x + 80 && near(thumb.origin.y + 6, area.origin.y + 24, 1e-4) &&
              thumb.size == Size{12, 12} && near(hue_thumb.origin.x + 3, hue.origin.x + 160 * 210 / 360., 1e-4) &&
              hue_thumb.origin.y == hue.origin.y - 2,
          "Thumbs center on the saturation/value and hue positions");
    check(take(hit_test(f.hit(), Point{thumb.origin.x + 6, thumb.origin.y + 6})).target == f.node("color-area") &&
              take(hit_test(f.hit(), Point{hue_thumb.origin.x + 3, hue.origin.y + 6})).target == f.node("color-hue"),
          "Thumbs and overlays are input-transparent");
    const auto open = take(build_semantic_tree(f.semantic()));
    const auto& group = entry(open, "color-panel");
    const auto& plane = entry(open, "color-area");
    const auto& strip = entry(open, "color-hue");
    check(entry(open, "color").expanded == true && group.role == SemanticRole::group && group.name == "Display color" &&
              group.actions.empty() && plane.role == SemanticRole::button &&
              plane.name == "Saturation 50%, value 80%" && open.nodes[plane.parent].id == "color-panel" &&
              plane.actions == std::vector<SemanticAction>{{"activate", "color.area"}} &&
              strip.role == SemanticRole::button && strip.name == "Hue 210 degrees" && strip.focusable,
          "An open panel projects a named group with named area and hue buttons");

    f.build({.open = true, .disabled = true});
    const auto disabled = take(build_semantic_tree(f.semantic()));
    check(!entry(disabled, "color").enabled && !entry(disabled, "color-area").enabled &&
              !entry(disabled, "color-hue").enabled && entry(disabled, "color-hue").actions.empty(),
          "Disabling the ColorPicker disables its trigger and both panel controls");
}

struct Pixels {
    CapturedImage image;
    std::array<double, 3> at(Point point) const {
        const auto x = static_cast<std::uint32_t>(point.x), y = static_cast<std::uint32_t>(point.y);
        const auto* pixel = &image.pixels[(std::size_t(y) * image.extent.width + x) * 4];
        return {pixel[0] / 255., pixel[1] / 255., pixel[2] / 255.};
    }
};
Pixels capture(Fixture& f) {
    auto paint = take(build_paint_list({f.tree.get(), f.styles, &f.layout, &f.text}));
    // Placeholder glyphs are not under test; the sampled pixels hold no text.
    std::erase_if(paint.commands, [](const auto& command) { return std::holds_alternative<DrawGlyphRun>(command); });
    CpuFrameCapture frames;
    return {take(frames.capture({0, f.viewport, 1, take(capture_extent(f.viewport, 1)), &paint}))};
}
double difference(const std::array<double, 3>& pixel, const Color& color) {
    return std::max({std::abs(pixel[0] - color.r), std::abs(pixel[1] - color.g), std::abs(pixel[2] - color.b)});
}

void pixels() {
    Fixture f;
    double area_error = 0, extreme_error = 0, hue_error = 0;
    for (const double hue : {0., 30., 75., 210., 300.}) {
        // The thumb sits in the top-right corner, outside the sampled grid.
        f.build({.color = {hue, 1, 1}, .open = true});
        const auto image = capture(f);
        const auto swatch = f.box("color-swatch").content_box();
        check(difference(image.at({swatch.origin.x + 4, swatch.origin.y + 4}), to_color({hue, 1, 1})) <= 0.5 / 255,
              "The swatch shows the exact color");
        const auto area = f.box("color-area").content_box();
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const auto px = std::floor(area.origin.x + (x + 0.5f) * 18),
                           py = std::floor(area.origin.y + (y + 0.5f) * 13);
                const double s = (px + 0.5 - area.origin.x) / area.size.width;
                const double v = 1 - (py + 0.5 - area.origin.y) / area.size.height;
                const auto expected = to_color({hue, s, v});
                const auto pixel = image.at({px, py});
                area_error = std::max(area_error, difference(pixel, expected));
                // The largest and smallest components carry the plane's brightness and whiteness.
                const auto high = std::max({expected.r, expected.g, expected.b});
                const auto low = std::min({expected.r, expected.g, expected.b});
                for (int c = 0; c < 3; ++c) {
                    const float channel = c == 0 ? expected.r : c == 1 ? expected.g : expected.b;
                    if (channel == high || channel == low)
                        extreme_error = std::max(extreme_error, std::abs(pixel[c] - channel));
                }
            }
        const auto strip = f.box("color-hue").content_box();
        for (int x = 0; x < 160; x += 7) {
            if (std::abs(x + 0.5 - hue / 360 * 160) < 6) continue; // Beside the thumb.
            const auto pixel = image.at({strip.origin.x + x, strip.origin.y + 1});
            hue_error = std::max(hue_error, difference(pixel, to_color({(x + 0.5) / 160 * 360, 1, 1})));
        }
        const auto corner = image.at({area.origin.x + area.size.width - 1, area.origin.y + 10});
        const auto white = image.at({area.origin.x, area.origin.y});
        const auto black = image.at({area.origin.x + 40, area.origin.y + area.size.height - 1});
        check(difference(corner, to_color({hue, 1, 1})) <= 0.1 && difference(white, {1, 1, 1, 1}) <= 0.02 &&
                  difference(black, {0, 0, 0, 1}) <= 0.02,
              "Area corners reach the pure hue, white and black");
    }
    std::cout << "ColorPicker CPU deviation: area " << area_error * 255 << "/255, extreme components "
              << extreme_error * 255 << "/255, hue strip " << hue_error * 255 << "/255\n";
    check(area_error <= 28. / 255 && extreme_error <= 9. / 255 && hue_error <= 6. / 255,
          "The CPU area and hue strip approximate the sRGB HSV model within their bounds");
}

void codecs() {
    Fixture f;
    f.build({.open = true});
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "ColorPicker roles, value and expansion survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(f.node("color-area")), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "ColorPicker value and expansion round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget picker{SemanticRole::color_picker, "Display color"};
    check(take(resolve_target(inspection, picker)) == f.node("color") &&
              take(resolve_target(inspection, SemanticTarget{SemanticRole::group, "Display color"})) ==
                  f.node("color-panel"),
          "Inspection resolves color_picker and panel targets");
    const std::vector<UiInvariant> invariants{{"color-name", SemanticNameInvariant{picker, "Display color"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema accepts color_picker targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::button, "Hue 210 degrees"}},
                           ReplaySemanticAction{picker, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs retain color_picker targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 1 && output.actions[0].action == "color.toggle",
              "Replay requests the trigger's toggle through semantic activation");
    }
}

void keyboard_and_pointer() {
    Fixture f;
    HsvColor color{210, 0.5, 0.8};
    f.build({.color = color});
    std::chrono::microseconds time{0};
    const auto event = [&](auto data) {
        time += 1us;
        return InputEvent{time, std::move(data)};
    };
    FocusDispatcher focus;
    ColorPickerDispatcher picker{"color"};
    check(take(picker.refresh(f.hit())) == ColorPickerDispatchResult{}, "A closed ColorPicker requests nothing");
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(take(focus.dispatch(f.hit(), event(FocusNext{}))).focused == f.node("color"),
          "The trigger is an ordinary Tab stop");
    check(take(focus.dispatch(f.hit(), event(Activate{}))).actions ==
              std::vector<ActionRequest>{{"activate", "color.toggle", f.node("color")}},
          "Activate on the trigger requests its toggle action");
    const Modifiers alt{.alt = true};
    auto result = take(picker.dispatch(f.hit(), event(KeyDown{Key::down, alt}), f.node("before"), color));
    check(!result.open && !result.consumed, "Alt+Down elsewhere does not open the panel");
    result = take(picker.dispatch(f.hit(), event(KeyDown{Key::down, {}}), f.node("color"), color));
    check(!result.open && !result.consumed, "Plain Down leaves the trigger to ordinary navigation");
    result = take(picker.dispatch(f.hit(), event(KeyDown{Key::down, alt}), f.node("color"), color));
    check(result.open && result.consumed && !result.dismiss && !result.color,
          "Alt+Down on the focused trigger opens the panel");

    const auto open = [&](HsvColor shown) {
        color = shown;
        f.build({.color = color, .open = true});
        auto opened = take(picker.refresh(f.hit()));
        if (opened.focus) (void)take(focus.focus(f.hit(), *opened.focus));
        return opened;
    };
    // The host applies each requested color and republishes the open panel.
    const auto send = [&](auto data) {
        const auto focused = take(focus.refresh(f.hit())).focused;
        auto sent = take(picker.dispatch(f.hit(), event(std::move(data)), focused, color));
        if (sent.focus) (void)take(focus.focus(f.hit(), *sent.focus));
        if (sent.color) {
            // Report handles in the republished tree, as the host would after accepting the color.
            const auto moved = sent.focus ? f.tree->get(*sent.focus)->id : std::nullopt;
            color = *sent.color;
            f.build({.color = color, .open = true});
            (void)take(picker.refresh(f.hit()));
            if (moved) sent.focus = f.node(*moved);
        }
        return sent;
    };
    result = open(color);
    check(result.focus == f.node("color-area") && !result.dismiss, "Opening focuses the area");
    check(take(picker.refresh(f.hit())).focus == std::nullopt, "Refreshing an open panel requests nothing more");
    result = send(KeyDown{Key::right});
    check(result.consumed && result.color == HsvColor{210, 0.55, 0.8} && result.action &&
              result.action->binding == "activate" && result.action->action == "color.area",
          "Right raises saturation through the area command");
    check(send(Navigate{Direction::down}).color == HsvColor{210, 0.55, 0.75} &&
              send(KeyDown{Key::up}).color == HsvColor{210, 0.55, 0.8} &&
              send(KeyDown{Key::left}).color == HsvColor{210, 0.5, 0.8},
          "Down/Up adjust value and Left lowers saturation, without binary noise");
    open({0, 1, 1});
    result = send(KeyDown{Key::right});
    check(result.consumed && !result.color && !result.action, "A clamped, unchanged value requests nothing");
    check(send(KeyDown{Key::up}).consumed && !send(KeyDown{Key::up}).color, "Value stays within its range");
    result = send(KeyDown{Key::home});
    check(!result.consumed && !result.color, "Home on the area is ordinary input");

    result = send(KeyDown{Key::tab});
    check(result.consumed && !result.dismiss && result.focus == f.node("color-hue"), "Tab moves to the hue strip");
    result = send(KeyDown{Key::right});
    check(result.color == HsvColor{5, 1, 1} && result.action->action == "color.hue", "Right raises hue");
    check(send(Navigate{Direction::up}).color == HsvColor{10, 1, 1} &&
              send(KeyDown{Key::down}).color == HsvColor{5, 1, 1} && send(KeyDown{Key::end}).color->hue == 360 &&
              !send(Navigate{Direction::right}).color && send(KeyDown{Key::home}).color->hue == 0 &&
              !send(KeyDown{Key::left}).color,
          "Hue steps, clamps and selects its ends");
    result = send(FocusPrevious{});
    check(result.consumed && result.focus == f.node("color-area"), "Shift+Tab returns to the area");
    result = take(picker.dispatch(f.hit(), event(KeyDown{Key::tab, {.shift = true}}), f.node("color-area"), color));
    check(result.dismiss && !result.consumed && result.focus == f.node("color"),
          "Shift+Tab before the area closes without consuming");
    result = take(picker.dispatch(f.hit(), event(FocusNext{}), f.node("color-hue"), color));
    check(result.dismiss && !result.consumed && result.focus == f.node("color"),
          "Tab past the hue strip closes and continues from the trigger");
    for (const auto& closing :
         {InputEvent{0us, KeyDown{Key::escape}}, InputEvent{0us, Cancel{}}, InputEvent{0us, KeyDown{Key::up, alt}},
          InputEvent{0us, KeyDown{Key::enter}}, InputEvent{0us, Activate{}}}) {
        auto copy = closing;
        time += 1us;
        copy.timestamp = time;
        result = take(picker.dispatch(f.hit(), copy, f.node("color-hue"), color));
        check(result.dismiss && result.consumed && result.focus == f.node("color") && !result.color,
              "Escape, Cancel, Alt+Up, Enter and Activate close and restore trigger focus");
    }

    open({210, 0.5, 0.8});
    result = send(PointerDown{{1}, f.at("color-area", 0.25, 0.75)});
    check(result.consumed && result.focus == f.node("color-area") && result.color == HsvColor{210, 0.25, 0.25} &&
              result.action->action == "color.area" && picker.dragging(),
          "A press on the area focuses it, captures the pointer and requests the color under it");
    result = send(PointerMove{{2}, f.at("color-area", 0.5, 0.5)});
    check(!result.consumed && !result.color, "Another pointer is ordinary input while one drags");
    result = send(PointerMove{{1}, Point{f.at("color-area", 1, 0).x + 40, f.at("color-area", 1, 0).y - 30}});
    check(result.consumed && result.color == HsvColor{210, 1, 1}, "Dragging past the area clamps to its edges");
    result = send(PointerUp{{1}, f.at("color-area", 0.5, 0.5)});
    check(result.consumed && result.color == HsvColor{210, 0.5, 0.5} && !picker.dragging(),
          "Release requests the final color and ends capture");
    result = send(PointerMove{{1}, f.at("color-area", 0.75, 0.5)});
    check(!result.consumed && !result.color, "Moves after release are ordinary input");
    result = send(PointerDown{{1}, f.at("color-hue", 0.5, 0.5)});
    check(result.focus == f.node("color-hue") && result.color == HsvColor{180, 0.5, 0.5} && picker.dragging(),
          "A press on the hue strip requests its hue");
    result = send(PointerCancel{{1}});
    check(result.consumed && !result.color && !picker.dragging(), "Pointer cancellation ends capture");
    result = send(PointerDown{{1}, f.at("color-hue", 0.5, -0.4)});
    check(!result.dismiss && !result.color && !picker.dragging(), "A press elsewhere in the panel is ordinary input");
    result = send(PointerDown{{1}, f.center("before")});
    check(result.dismiss && result.consumed && result.focus == f.node("color") && !result.color,
          "An outside press closes without clicking through");
    open(color);
    (void)send(PointerDown{{1}, f.at("color-area", 0.5, 0.5)});
    check(picker.dragging(), "Capture starts again");
    result = send(KeyDown{Key::escape});
    check(result.dismiss && !picker.dragging(), "Closing ends capture");

    f.build({.color = color});
    result = take(picker.refresh(f.hit()));
    check(result.focus == f.node("color") && !result.dismiss, "Closing returns focus to the trigger");
    f.build({.color = color, .open = true});
    (void)take(picker.refresh(f.hit()));
    f.build({.color = color, .open = true, .disabled = true});
    result = take(picker.refresh(f.hit()));
    check(result.dismiss, "A panel whose ColorPicker becomes ineligible asks the host to close it");
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::ColorPickerParts parts) {
        return session.lower(ui::Column({std::move(parts.field), std::move(parts.panel)}), actions);
    };
    check(has(lower(ui::ColorPicker("Color", {}, commands)).diagnostics, "color_picker_id", "/root/children/0"),
          "ColorPicker requires a stable ID");
    check(has(lower(ui::ColorPicker("", {}, commands, {.node = {.id = "c"}})).diagnostics, "color_picker_label",
              "/root/children/0"),
          "ColorPicker requires a label");
    for (const auto bad : {HsvColor{-1, 0, 0}, HsvColor{0, 2, 0}, HsvColor{0, 0, NAN}})
        check(has(lower(ui::ColorPicker("Color", bad, commands, {.node = {.id = "c"}})).diagnostics,
                  "color_picker_value", "/root/children/0"),
              "ColorPicker requires a valid HSV value");
    for (const auto bad : {Size{0, 10}, Size{10, -1}, Size{INFINITY, 10}})
        check(has(lower(ui::ColorPicker("Color", {}, commands, {.node = {.id = "c"}, .area = bad})).diagnostics,
                  "color_picker_area", "/root/children/0"),
              "ColorPicker requires a finite positive area");
    check(has(lower(ui::ColorPicker("Color", {}, commands, {.node = {.id = "c"}, .panel = {.id = "x"}})).diagnostics,
              "color_picker_panel_id", "/root/children/1"),
          "The panel ID is generated");

    Fixture f;
    f.build({.open = true});
    // root/children/1 is the trigger; root/children/3 is the panel.
    auto malformed = f.document;
    malformed.root.children[1].properties.erase("expanded");
    check(has(validate(malformed, actions), "color_picker_state", "/root/children/1/properties/expanded"),
          "A color picker declares its expansion state");
    malformed = f.document;
    malformed.root.children[1].properties.erase("edit_value");
    check(has(validate(malformed, actions), "edit_value", "/root/children/1/properties/edit_value"),
          "A color picker declares its value");
    malformed = f.document;
    malformed.root.children[1].events.clear();
    check(has(validate(malformed, actions), "color_picker_action", "/root/children/1/events/activate"),
          "A color picker requires a toggle action");
    malformed = f.document;
    malformed.root.children[1].children.push_back(malformed.root.children[0]);
    check(has(validate(malformed, actions), "color_picker_child", "/root/children/1/children/3"),
          "A color picker contains only noninteractive parts");
    malformed = f.document;
    malformed.root.children[0].properties["expanded"] = true;
    check(has(validate(malformed, actions), "collection_state", "/root/children/0/properties/expanded"),
          "Expansion state stays limited to its roles");

    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto copy = snapshot;
    const auto at = [&](std::string_view id) {
        return std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                            [&](const auto& node) { return node.id == id; });
    };
    at("color")->value.reset();
    check(!validate(snapshot).empty(), "Semantic validation requires a color picker value");
    snapshot = copy;
    at("color")->expanded.reset();
    check(!validate(snapshot).empty(), "Semantic validation requires color picker expansion state");
    snapshot = copy;
    at("color-area")->value = "#000000";
    check(!validate(snapshot).empty(), "Semantic validation rejects a value on the area button");

    Fixture g;
    g.build();
    const HsvColor valid_color{};
    check(!ColorPickerDispatcher("after").refresh(g.hit()) && !ColorPickerDispatcher("").refresh(g.hit()),
          "The dispatcher rejects non-picker and empty IDs");
    check(has(ColorPickerDispatcher("color").dispatch(g.hit(), {1us, Activate{}}, {}, {0, 2, 0}).diagnostics,
              "color_picker_value", "/current"),
          "The dispatcher rejects an invalid accepted color");
    check(has(ColorPickerDispatcher("color", {.hue = 0})
                  .dispatch(g.hit(), {1us, Activate{}}, {}, valid_color)
                  .diagnostics,
              "color_picker_steps", "/steps"),
          "The dispatcher rejects nonpositive steps");
}
} // namespace

int main() {
    try {
        color_model();
        composition_and_semantics();
        pixels();
        codecs();
        keyboard_and_pointer();
        rejection();
        std::cout << "ColorPicker model, composition, semantics, pixels, codecs, input and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
