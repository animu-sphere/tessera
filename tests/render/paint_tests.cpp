#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using tessera::Dimension;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.children = std::move(children);
    return result;
}
tessera::UiNode text(std::string content) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.properties["text"] = std::move(content);
    return result;
}

struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;

    explicit Fixture(tessera::UiNode root) {
        tessera::UiDocument document;
        document.root = std::move(root);
        auto created = tessera::UiTree::create(document);
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
    }
    void compute(tessera::Size viewport = {100, 80}) {
        auto result = tessera::compute_layout({tree.get(), styles, viewport, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::PaintInput input() { return {tree.get(), styles, &layout, &shaper}; }
    tessera::UiDrawList paint() {
        const auto result = tessera::build_paint_list(input());
        check(result && result.diagnostics.empty(), "Valid paint rejected");
        const auto repeated = tessera::build_paint_list(input());
        check(repeated && *repeated.value == *result.value, "Repeated paint differs");
        check(tessera::validate(*result.value).empty(), "Paint produced an invalid draw list");
        return *result.value;
    }
};

void order_geometry_opacity_and_ownership() {
    Fixture f(box({text("A\nあ"), box()}));
    auto& root = f.styles[0];
    root.background = {1, 0, 0, 0.5f};
    root.border_color = {0, 1, 0, 1};
    root.border = {1, 2, 3, 4};
    root.padding = {5, 6, 7, 8};
    root.corner_radius = 6;
    root.opacity = 0.5f;
    auto& label = f.styles[1];
    label.background = {0, 0, 1, 1};
    label.border_color = {1, 1, 1, 1};
    label.border = {2, 2, 2, 2};
    label.padding = {3, 3, 3, 3};
    label.color = {0.25f, 0.5f, 1, 0.5f};
    label.opacity = 0.5f;
    label.text.font = {17};
    label.text.size = 20;
    // The last sibling overflows the root. Paint must preserve it without inserting a clip.
    f.styles[2].height = Dimension::points(100);
    f.styles[2].background = {1, 1, 0, 1};
    f.compute();
    auto list = f.paint();
    check(list.commands.size() == 6, "Expected parent background/border, text background/border/glyphs, sibling");
    check(std::get<tessera::DrawRect>(list.commands[0]) ==
              tessera::DrawRect{f.layout.boxes[0].border_box, {1, 0, 0, 0.25f}, 6},
          "Root paint differs");
    check(std::get<tessera::DrawBorder>(list.commands[1]) ==
              tessera::DrawBorder{f.layout.boxes[0].border_box, root.border, {0, 1, 0, 0.5f}, 6},
          "Border paint differs");
    check(std::get<tessera::DrawRect>(list.commands[2]).color == tessera::Color{0, 0, 1, 0.25f},
          "Ancestor opacity must multiply child alpha without changing RGB");
    check(std::holds_alternative<tessera::DrawBorder>(list.commands[3]), "Text border must precede glyphs");
    const auto& glyphs = std::get<tessera::DrawGlyphRun>(list.commands[4]);
    auto shaped = f.shaper.shape("A\nあ", label.text);
    check(shaped && glyphs.run == *shaped.value, "Paint must preserve the shaper's owned run and metrics");
    check(glyphs.origin == f.layout.boxes[1].content_box().origin, "Glyph origin must exclude border and padding");
    check(glyphs.color == tessera::Color{0.25f, 0.5f, 1, 0.125f}, "Glyph alpha differs");
    check(std::get<tessera::DrawRect>(list.commands[5]).rect == f.layout.boxes[2].border_box,
          "Overflowing sibling must paint last at layout coordinates");
    f.tree.reset();
    f.styles.clear();
    f.layout.boxes.clear();
    check(tessera::validate(list).empty() && std::get<tessera::DrawGlyphRun>(list.commands[4]).run.glyphs.size() == 2,
          "Returned draw list must outlive all borrowed inputs");
}

void fill_shapes() {
    Fixture f(box({box()}));
    auto& style = f.styles[0];
    style.width = Dimension::points(40);
    style.height = Dimension::points(20);
    style.background = {1, 0, 0, .8f};
    style.opacity = .5f;
    style.border = {1, 2, 3, 4};
    style.border_color = {0, 1, 0, 1};
    style.corner_radius = 7;
    f.compute();
    const auto original = f.layout.boxes[0].border_box;
    style.background_shape = tessera::FillShape::ellipse;
    style.background_antialias = true;
    f.compute();
    auto paint = f.paint();
    const auto fill = std::get<tessera::DrawRect>(paint.commands[0]);
    check(fill.rect == original && fill.shape == tessera::FillShape::ellipse && fill.antialias && fill.color.a == .4f &&
              f.layout.boxes[0].border_box == original,
          "Analytic fill must retain the layout box and inherited command opacity");
    check(std::get<tessera::DrawBorder>(paint.commands[1]).corner_radius == 7,
          "Background shape must not replace the independent rectangular border contract");
    style.background_gradient = tessera::LinearGradient{.start_color = {1, 1, 1, 1}, .end_color = {0, 0, 1, 1}};
    const auto gradient_paint = f.paint();
    const auto& gradient = std::get<tessera::DrawRect>(gradient_paint.commands[0]);
    check(gradient.shape == tessera::FillShape::ellipse && gradient.antialias && gradient.gradient &&
              std::get<tessera::LinearGradient>(*gradient.gradient).start_color.a == .5f,
          "Gradient and solid fills must carry the same shape and coverage");
    style.background_shape = tessera::FillShape::rectangle;
    style.background_antialias = false;
    check(fill.shape == tessera::FillShape::ellipse && fill.antialias &&
              std::get<tessera::DrawRect>(f.paint().commands[0]).shape == tessera::FillShape::rectangle,
          "Resolved paint must own its shape after style replacement");
}

void procedural_fills() {
    Fixture f(box({box()}));
    auto& style = f.styles[0];
    style.width = Dimension::points(40);
    style.height = Dimension::points(20);
    style.background = {1, 0, 0, .8f};
    style.opacity = .5f;
    style.corner_radius = 7;
    style.background_shape = tessera::FillShape::ellipse;
    style.background_antialias = true;
    style.background_procedural =
        tessera::ProceduralFill{"example.wave", {.25f, tessera::Point{1, 2}, tessera::Color{0, 0, 1, 1}}, 1.5f, .6f};
    f.compute();
    auto paint = f.paint();
    const auto fill = std::get<tessera::DrawRect>(paint.commands[0]);
    check(fill.procedural && fill.procedural->shader == "example.wave" && fill.procedural->time == 1.5f &&
              std::abs(fill.procedural->opacity - .3f) < 1e-6f && fill.procedural->parameters.size() == 3 &&
              fill.color.a == .4f && fill.shape == tessera::FillShape::ellipse && fill.antialias &&
              fill.corner_radius == 7 && fill.rect == f.layout.boxes[0].border_box,
          "Procedural fill must keep shape, coverage, faded fallback and inherited opacity in one command");
    style.background_gradient = tessera::LinearGradient{.start_color = {1, 1, 1, 1}, .end_color = {0, 0, 1, 1}};
    const auto gradient = std::get<tessera::DrawRect>(f.paint().commands[0]);
    check(gradient.procedural && gradient.gradient &&
              std::get<tessera::LinearGradient>(*gradient.gradient).start_color.a == .5f,
          "A gradient background must become the procedural fill's declared fallback");
    style.background_gradient.reset();
    style.background = {};
    check(std::get<tessera::DrawRect>(f.paint().commands[0]).procedural.has_value(),
          "A transparent fallback must still request the procedural fill");
    style.background_procedural->opacity = 0;
    check(f.paint().commands.empty(), "An invisible procedural fill with a transparent fallback paints nothing");
}

tessera::DrawRect fill_at(Fixture& f, double host_time) {
    auto input = f.input();
    input.host_time = host_time;
    const auto result = tessera::build_paint_list(input);
    check(result && result.diagnostics.empty(), "Valid timed paint rejected");
    return std::get<tessera::DrawRect>(result.value->commands[0]);
}

void procedural_motion() {
    Fixture f(box({box()}));
    auto& style = f.styles[0];
    style.width = Dimension::points(40);
    style.height = Dimension::points(20);
    style.background = {0, 0, 1, 1};
    style.background_procedural = tessera::ProceduralFill{"example.wave", {.25f}, 1.5f};
    f.compute();
    const auto layout = f.layout;
    check(fill_at(f, 120).procedural->time == 1.5f && !tessera::time_dependent(f.paint()),
          "A zero time rate must keep the declared static time at every host time");
    style.background_procedural->time_rate = -.5f;
    const auto moving = fill_at(f, 4);
    check(moving.procedural->time == -.5f && moving.procedural->time_rate == -.5f &&
              tessera::time_dependent(tessera::UiDrawList{{moving}}),
          "Paint must evaluate phase + rate * host time and keep the rate as a motion request");
    check(fill_at(f, 0).procedural->time == 1.5f, "Host time 0 must draw the declared phase");
    // Shader time is evaluated in double and rounded once, so long-running hosts lose only float rounding.
    check(fill_at(f, 86400.25).procedural->time == float(1.5 - .5 * 86400.25),
          "Large host times must round the evaluated time once");
    check(f.layout == layout && fill_at(f, 9).rect == moving.rect && fill_at(f, 9).color == moving.color,
          "Host time must change only the fill's evaluated time, not layout, geometry or fallback");

    auto input = f.input();
    input.host_time = std::numeric_limits<double>::infinity();
    auto rejected = tessera::build_paint_list(input);
    check(!rejected && has(rejected.diagnostics, "invalid_number", "/host_time"), "Nonfinite host time accepted");
    input.host_time = 1e300;
    rejected = tessera::build_paint_list(input);
    check(!rejected && has(rejected.diagnostics, "time_overflow", "/nodes/0/background_procedural/time"),
          "An evaluated time beyond float range must reject paint without a partial list");
    style.background_procedural->time_rate = 0;
    check(static_cast<bool>(tessera::build_paint_list(input)), "Static fills must ignore host time");
    style.background_procedural->time_rate = std::numeric_limits<float>::quiet_NaN();
    rejected = tessera::build_paint_list(f.input());
    check(!rejected && has(rejected.diagnostics, "invalid_number", "/styles/0/background_procedural/time_rate"),
          "Resolved styles must validate the time rate");
}

void box_shadows() {
    Fixture f(box({box()}));
    f.styles[0].overflow = tessera::Overflow::clip;
    f.styles[0].padding = {4, 4, 4, 4};
    f.styles[0].opacity = .5f;
    auto& style = f.styles[1];
    style.width = Dimension::points(20);
    style.height = Dimension::points(20);
    style.overflow = tessera::Overflow::clip;
    style.border = {1, 1, 1, 1};
    style.border_color = {0, 0, 0, 1};
    style.corner_radius = 4;
    style.background_gradient = tessera::RadialGradient{.start_color = {1, 1, 1, 1}, .end_color = {0, 0, 1, 1}};
    style.opacity = .5f;
    f.compute();
    const auto box = f.layout.boxes[1];
    style.box_shadow = tessera::BoxShadow{{-2, 4}, 8, {.2f, .3f, .4f, .5f}};
    f.compute();
    check(f.layout.boxes[1].border_box == box.border_box && f.layout.boxes[1].clip == box.clip,
          "Shadows must not expand allocation, scroll geometry or hit-test clips");
    const auto list = f.paint();
    check(list.commands.size() == 5 && std::get<tessera::PushClip>(list.commands[0]).rect == *box.clip &&
              std::get<tessera::DrawShadow>(list.commands[1]) ==
                  tessera::DrawShadow{{{box.border_box.origin.x - 2, box.border_box.origin.y + 4}, box.border_box.size},
                                      {.2f, .3f, .4f, .125f},
                                      4,
                                      8} &&
              std::holds_alternative<tessera::DrawRect>(list.commands[2]) &&
              std::holds_alternative<tessera::DrawBorder>(list.commands[3]) &&
              std::holds_alternative<tessera::PopClip>(list.commands[4]),
          "Shadow must precede fill/border under the ancestor clip, with multiplied opacity and owned data");
    style.box_shadow->inset = true;
    f.compute();
    const auto inset = f.paint();
    check(f.layout.boxes[1].border_box == box.border_box && f.layout.boxes[1].clip == box.clip &&
              inset.commands.size() == 5 && std::holds_alternative<tessera::DrawRect>(inset.commands[1]) &&
              std::get<tessera::DrawShadow>(inset.commands[2]) ==
                  tessera::DrawShadow{box.border_box, {.2f, .3f, .4f, .125f}, 4, 8, true, {-2, 4}} &&
              std::holds_alternative<tessera::DrawBorder>(inset.commands[3]),
          "Inset shadows must follow the fill and precede borders, with retained bounds, offset and opacity");
    style.box_shadow->inset = false;
    check(std::get<tessera::DrawShadow>(inset.commands[2]).inset,
          "Paint must own inset parameters independently of subsequent style edits");
    style.box_shadow->color.a = 0;
    check(f.paint().commands.size() == 4, "Transparent shadows must emit nothing");
    style.box_shadow->color.a = 1;
    style.visibility = tessera::Visibility::hidden;
    f.compute();
    check(f.paint().commands.empty(), "Hidden casters must not emit shadows");
    style.visibility = tessera::Visibility::visible;
    style.box_shadow->softness = std::numeric_limits<float>::max();
    f.compute();
    const auto rejected = tessera::build_paint_list(f.input());
    check(!rejected && has(rejected.diagnostics, "geometry_overflow", "/nodes/1/commands/0/rect"),
          "Expanded shadow overflow must reject the whole paint list");
}

void gradient_backgrounds() {
    Fixture f(box({box()}));
    f.styles[0].opacity = .5f;
    auto& style = f.styles[1];
    style.height = Dimension::points(20);
    style.opacity = .5f;
    style.background = {0, 1, 0, 1};
    style.background_gradient = tessera::LinearGradient{{0, 0}, {1, 0}, {1, 0, 0, .5f}, {0, 0, 1, 1}};
    style.border = {1, 2, 3, 4};
    style.border_color = {1, 1, 1, 1};
    style.corner_radius = 5;
    f.compute();
    const auto paint = f.paint();
    check(paint.commands.size() == 2, "Gradient replaces solid background without adding layers");
    const auto& rect = std::get<tessera::DrawRect>(paint.commands[0]);
    check(rect.rect == f.layout.boxes[1].border_box && rect.corner_radius == 5 && rect.gradient &&
              std::get<tessera::LinearGradient>(*rect.gradient).start_color == tessera::Color{1, 0, 0, .125f} &&
              std::get<tessera::LinearGradient>(*rect.gradient).end_color == tessera::Color{0, 0, 1, .25f} &&
              std::holds_alternative<tessera::DrawBorder>(paint.commands[1]),
          "Gradient must preserve box geometry, border order and inherited per-command opacity");
    auto& gradient = std::get<tessera::LinearGradient>(*style.background_gradient);
    gradient.start_color.a = 0;
    gradient.end_color.a = 0;
    check(f.paint().commands.size() == 1, "Transparent gradient must not reveal the replaced solid background");
    gradient.stops = {{.1f, {1, 0, 0, .5f}}, {.4f, {0, 1, 0, 1}}, {.9f, {0, 0, 1, .25f}}};
    const auto multi = f.paint();
    const auto& stops =
        std::get<tessera::LinearGradient>(*std::get<tessera::DrawRect>(multi.commands[0]).gradient).stops;
    check(multi.commands.size() == 2 && stops[0] == tessera::GradientStop{.1f, {1, 0, 0, .125f}} &&
              stops[1] == tessera::GradientStop{.4f, {0, 1, 0, .25f}} &&
              stops[2] == tessera::GradientStop{.9f, {0, 0, 1, .0625f}} && gradient.stops[1].color.a == 1,
          "Every explicit stop must receive opacity in the owned draw list without mutating its style");
    for (auto& stop : gradient.stops) stop.color.a = 0;
    gradient.start_color.a = 1;
    check(f.paint().commands.size() == 1,
          "Explicit transparent stops must replace, not fall back to, shorthand colors");
    style.background_gradient =
        tessera::RadialGradient{.center = {.25f, .75f},
                                .radius = {1, .5f},
                                .stops = {{0, {1, 0, 0, .5f}}, {.4f, {0, 1, 0, 1}}, {1, {0, 0, 1, .25f}}}};
    const auto radial_paint = f.paint();
    const auto& radial_rect = std::get<tessera::DrawRect>(radial_paint.commands[0]);
    const auto& radial = std::get<tessera::RadialGradient>(*radial_rect.gradient);
    check(radial_paint.commands.size() == 2 && radial.center == tessera::Point{.25f, .75f} &&
              radial.radius == tessera::Size{1, .5f} && radial.stops[1].color.a == .25f &&
              std::get<tessera::RadialGradient>(*style.background_gradient).stops[1].color.a == 1 &&
              std::holds_alternative<tessera::DrawBorder>(radial_paint.commands[1]),
          "Radial fills must preserve geometry, owned stops, opacity and border order");
    style.background_gradient.reset();
    check(!std::get<tessera::DrawRect>(f.paint().commands[0]).gradient,
          "Removing a gradient must restore the ordinary solid background");
}

void outlines() {
    Fixture f(box({box()}));
    auto& style = f.styles[0];
    style.background = {1, 1, 1, 1};
    style.border = {1, 1, 1, 1};
    style.border_color = {0, 0, 1, 1};
    style.corner_radius = 100;
    style.opacity = .5f;
    style.outline = tessera::Outline{2, 3, {1, 0, 0, .8f}, 4, 2, -1};
    f.compute({40, 20});
    const auto boxes = f.layout.boxes;
    const auto list = f.paint();
    check(list.commands.size() == 3 && std::holds_alternative<tessera::DrawBorder>(list.commands[1]),
          "Outlines must follow the background and unchanged inside border");
    const auto& stroke = std::get<tessera::DrawStroke>(list.commands[2]);
    check(stroke.rect == tessera::Rect{{-5, -5}, {50, 30}} && stroke.corner_radius == 15 && stroke.width == 2 &&
              stroke.color == tessera::Color{1, 0, 0, .4f} && stroke.dash_length == 4 && stroke.gap_length == 2 &&
              stroke.dash_offset == -1,
          "Outline expansion must preserve the clamped box radius, gap, pattern and inherited alpha");
    style.outline.reset();
    f.compute({40, 20});
    check(f.layout.boxes == boxes && f.paint().commands.size() == 2,
          "Outlines must not alter layout boxes, padding, descendants or the legacy paint list when absent");
    style.corner_radius = 0;
    style.outline = tessera::Outline{1, 0, {1, 0, 0, 1}};
    f.compute({40, 20});
    check(std::get<tessera::DrawStroke>(f.paint().commands[2]).corner_radius == 0,
          "Square outlines must retain square joins");
    style.outline->width = 0;
    check(f.paint().commands.size() == 2, "Zero-width outlines must not emit commands");
    style.outline->width = 1;
    style.outline->offset = std::numeric_limits<float>::max();
    const auto rejected = tessera::build_paint_list(f.input());
    check(!rejected && has(rejected.diagnostics, "geometry_overflow", "/nodes/0/outline"),
          "Unrepresentable outside decoration must reject without a partial paint list");
}

void visibility_display_and_empty() {
    Fixture f(box({box({text("omitted")}), box({text("visible child")}), text(""), text("transparent")}));
    f.styles[0].background = {1, 0, 0, 1};
    f.styles[1].display = tessera::Display::none;
    f.styles[3].visibility = tessera::Visibility::hidden;
    f.styles[3].opacity = 0.5f;
    f.styles[3].background = {0, 1, 0, 1};
    f.styles[6].color.a = 0;
    f.compute();
    auto list = f.paint();
    check(list.commands.size() == 2, "None subtrees, hidden boxes, empty and transparent text must emit nothing");
    check(std::get<tessera::DrawGlyphRun>(list.commands[1]).color.a == 0.5f,
          "Locally visible child of hidden box must paint with ancestor opacity");
    f.styles[0].opacity = 0;
    check(f.paint().commands.empty(), "Zero ancestor opacity must suppress descendant commands");
    f.styles[0].display = tessera::Display::none;
    f.compute();
    check(f.layout.boxes.empty() && f.paint().commands.empty(), "Non-displayed root must produce an empty list");

    Fixture defaults(box());
    defaults.styles[0].border = {2, 2, 2, 2};
    defaults.compute();
    check(defaults.paint().commands.empty(), "Transparent backgrounds/borders must emit nothing");
    defaults.styles[0].border_color = {1, 1, 1, 1};
    defaults.styles[0].border = {};
    defaults.compute({0, 0});
    check(defaults.paint().commands.empty(), "Zero-width border must emit nothing even when opaque");
}

void clip_runs() {
    // Preorder: 0 root (clips), 1 first, 2 nested (clips), 3 inner, 4 last.
    Fixture f(box({box(), box({box()}), box()}));
    f.styles[0].padding = {4, 4, 4, 4};
    f.styles[0].overflow = tessera::Overflow::clip;
    f.styles[2].height = Dimension::points(20);
    f.styles[2].overflow = tessera::Overflow::clip;
    f.styles[3].height = Dimension::points(30);
    for (auto& style : f.styles) style.background = {1, 1, 1, 1};
    f.styles[1].background = {};
    f.compute({100, 30});
    const auto& boxes = f.layout.boxes;
    const auto list = f.paint();
    const tessera::UiDrawList expected{{
        tessera::DrawRect{boxes[0].border_box, {1, 1, 1, 1}},
        tessera::PushClip{*boxes[2].clip},
        tessera::DrawRect{boxes[2].border_box, {1, 1, 1, 1}},
        tessera::PopClip{},
        tessera::PushClip{*boxes[3].clip},
        tessera::DrawRect{boxes[3].border_box, {1, 1, 1, 1}},
        tessera::PopClip{},
        tessera::PushClip{*boxes[4].clip},
        tessera::DrawRect{boxes[4].border_box, {1, 1, 1, 1}},
        tessera::PopClip{},
    }};
    check(list == expected, "Each emitting box must paint inside its own recorded clip");
    check(*boxes[3].clip == tessera::intersect(*boxes[2].clip, boxes[2].padding_box()), "Nested clip differs");

    f.styles[3].background = {};
    f.styles[4].background = {};
    f.styles[1].background = {1, 1, 1, 1};
    check(f.paint().commands.size() == 5, "Adjacent boxes under one clip must share a push/pop pair");

    f.styles[2].overflow = tessera::Overflow::visible;
    check(has(tessera::validate(f.input()), "layout_clip", "/layout/boxes/3/clip"), "Stale clip accepted");
    f.compute({100, 30});
    f.layout.boxes[1].clip->size.width = std::numeric_limits<float>::infinity();
    const auto invalid = tessera::validate(f.input());
    check(has(invalid, "invalid_number", "/layout/boxes/1/clip/size/width") &&
              has(invalid, "layout_clip", "/layout/boxes/1/clip"),
          "Non-finite clip accepted");
}

void invalid_input_and_topology() {
    const auto missing = tessera::build_paint_list({});
    check(!missing && missing.diagnostics.size() == 3 && has(missing.diagnostics, "missing_input", "/layout"),
          "Missing paint dependencies accepted");
    Fixture f(box({text("label"), box()}));
    f.compute();
    auto input = f.input();
    input.styles = input.styles.first(1);
    check(has(tessera::validate(input), "style_count", "/styles"), "Wrong style count accepted");
    f.styles[2].opacity = 2;
    check(has(tessera::validate(f.input()), "out_of_range", "/styles/2/opacity"), "Invalid style accepted");
    f.styles[2].opacity = 1;
    const auto original = f.layout;
    Fixture other(box());
    f.layout.boxes[0].node = other.tree->root();
    check(has(tessera::validate(f.input()), "layout_node", "/layout/boxes/0/node"), "Foreign tree handle accepted");
    f.layout = original;
    std::swap(f.layout.boxes[1], f.layout.boxes[2]);
    check(has(tessera::validate(f.input()), "layout_node", "/layout/boxes/1/node"), "Reordered boxes accepted");
    f.layout = original;
    f.layout.boxes[1].parent = 2;
    check(has(tessera::validate(f.input()), "layout_parent", "/layout/boxes/1/parent"), "Forward parent accepted");
    f.layout = original;
    f.layout.boxes.pop_back();
    check(has(tessera::validate(f.input()), "layout_count", "/layout/boxes"), "Missing box accepted");
    f.layout = original;
    f.layout.boxes.push_back(original.boxes[0]);
    check(!tessera::build_paint_list(f.input()), "Extra box accepted");
    f.layout = original;
    f.styles[1].display = tessera::Display::none;
    check(!tessera::build_paint_list(f.input()), "Stale display topology accepted");
    f.styles[1].display = tessera::Display::flex;
    f.styles[1].padding.left = 2;
    check(has(tessera::validate(f.input()), "layout_style", "/layout/boxes/1"), "Stale padding accepted");
    f.styles[1].padding.left = 0;
    f.layout.boxes[1].visible = false;
    check(has(tessera::validate(f.input()), "layout_style", "/layout/boxes/1"), "Stale visibility accepted");
    f.layout = original;
    f.layout.boxes[1].border_box.size.width = -1;
    check(has(tessera::validate(f.input()), "out_of_range", "/layout/boxes/1/border_box/size/width"),
          "Negative layout size accepted");
    f.layout = original;
    f.layout.boxes[1].border_box.origin.x = std::numeric_limits<float>::infinity();
    check(has(tessera::validate(f.input()), "invalid_number", "/layout/boxes/1/border_box/origin/x"),
          "Non-finite layout geometry accepted");
    check(!tessera::build_paint_list(f.input()), "Invalid input exposed a partial draw list");
}

class InjectedShaper final : public tessera::TextShaper {
public:
    enum class Mode { failure, silent_failure, malformed, warning, error_with_value } mode = Mode::failure;
    int calls = 0;
    tessera::PlaceholderTextShaper placeholder;
    tessera::Result<tessera::TextMetrics> measure(std::string_view text, const tessera::TextStyle& style,
                                                  const tessera::TextConstraints& constraints) override {
        return placeholder.measure(text, style, constraints);
    }
    tessera::Result<tessera::GlyphRun> shape(std::string_view text, const tessera::TextStyle& style,
                                             const tessera::TextConstraints& constraints) override {
        ++calls;
        if (mode == Mode::failure)
            return {std::nullopt, {{"font_missing", tessera::Severity::error, "/style/font", "Font unavailable.", {}}}};
        if (mode == Mode::silent_failure) return {};
        auto result = placeholder.shape(text, style, constraints);
        if (mode == Mode::malformed) result.value->glyphs[0].position.x = std::numeric_limits<float>::quiet_NaN();
        if (mode == Mode::warning || mode == Mode::error_with_value)
            result.diagnostics.push_back({"font_fallback",
                                          mode == Mode::warning ? tessera::Severity::warning : tessera::Severity::error,
                                          "/style/font",
                                          "Fallback font used.",
                                          {}});
        return result;
    }
};

void shaping_failures_and_warnings() {
    Fixture f(box({text("abc")}));
    f.styles[0].background = {1, 0, 0, 1};
    f.compute();
    InjectedShaper injected;
    auto input = f.input();
    input.text = &injected;
    auto result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "font_missing", "/nodes/1/style/font"),
          "Shaping failure must be located and discard preceding background commands");
    injected.mode = InjectedShaper::Mode::silent_failure;
    result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "text_shape_failed", "/nodes/1/text"), "Silent shaping failure accepted");
    injected.mode = InjectedShaper::Mode::malformed;
    result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "invalid_number", "/nodes/1/commands/0/run/glyphs/0/position/x"),
          "Malformed injected glyph positions accepted");
    injected.mode = InjectedShaper::Mode::warning;
    result = tessera::build_paint_list(input);
    check(result && result.diagnostics.size() == 1 && result.diagnostics[0].path == "/nodes/1/style/font" &&
              result.diagnostics[0].severity == tessera::Severity::warning,
          "Successful shaping warning lost");
    injected.mode = InjectedShaper::Mode::error_with_value;
    check(!tessera::build_paint_list(input), "Error diagnostic accompanied by a glyph run must still fail");
    const int before = injected.calls;
    f.styles[1].color.a = 0;
    check(tessera::build_paint_list(input) && injected.calls == before, "Transparent text must not call the shaper");
}

} // namespace

int main() {
    try {
        order_geometry_opacity_and_ownership();
        gradient_backgrounds();
        box_shadows();
        outlines();
        fill_shapes();
        procedural_fills();
        procedural_motion();
        visibility_display_and_empty();
        clip_runs();
        invalid_input_and_topology();
        shaping_failures_and_warnings();
        std::cout << "Paint order/geometry/opacity, ownership, topology, and shaping checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
