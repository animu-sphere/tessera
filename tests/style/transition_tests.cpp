#include <tessera/style/style_sheet.hpp>
#include <tessera/style/style_transitions.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <tuple>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
constexpr Color white{1, 1, 1, 1}, blue{.2f, .4f, .8f, 1}, clear{0, 0, 0, 0};
bool near(double a, double b, double tolerance = 1e-5) { return std::abs(a - b) <= tolerance; }
bool near(const Color& a, const Color& b, float tolerance = 1e-5f) {
    return near(a.r, b.r, tolerance) && near(a.g, b.g, tolerance) && near(a.b, b.b, tolerance) &&
           near(a.a, b.a, tolerance);
}

// root > button ("button", focusable) > label (text); plus an anonymous box beside the button.
std::unique_ptr<UiTree> tree(bool with_extra = false) {
    UiDocument document;
    document.root.id = "root";
    UiNode button;
    button.id = "button";
    button.classes = {"button"};
    button.properties["focusable"] = true;
    UiNode label;
    label.id = "label";
    label.kind = NodeKind::text;
    label.properties["text"] = std::string("Tools");
    button.children.push_back(label);
    document.root.children.push_back(button);
    document.root.children.push_back(UiNode{});
    if (with_extra) {
        UiNode extra;
        extra.id = "extra";
        document.root.children.push_back(extra);
    }
    auto created = UiTree::create(document);
    check(bool(created), "Transition fixture document is valid");
    return std::move(*created.value);
}

void easing_curves() {
    for (auto easing : {Easing::linear, Easing::ease, Easing::ease_in, Easing::ease_out, Easing::ease_in_out}) {
        check(ease(easing, 0) == 0 && ease(easing, 1) == 1 && ease(easing, -2) == 0 && ease(easing, 3) == 1 &&
                  ease(easing, std::numeric_limits<double>::quiet_NaN()) == 0,
              "Every curve starts at exactly 0, ends at exactly 1 and clamps outside [0, 1]");
        double previous = 0;
        for (int i = 1; i <= 100; ++i) {
            const double value = ease(easing, i / 100.0);
            check(value >= previous && value <= 1, "Every curve is monotonic within [0, 1]");
            previous = value;
        }
    }
    check(ease(Easing::linear, .3) == .3, "Linear easing is the identity");
    // Reference values of the CSS cubic-bezier presets.
    check(near(ease(Easing::ease, .5), .8024033877, 1e-6) && near(ease(Easing::ease_in, .5), .3153568, 1e-6) &&
              near(ease(Easing::ease_out, .5), .6846432, 1e-6) && near(ease(Easing::ease_in_out, .5), .5, 1e-9),
          "Cubic presets match the CSS control points");
    check(near(ease(Easing::ease_in_out, .2) + ease(Easing::ease_in_out, .8), 1, 1e-9),
          "ease_in_out is symmetric about its midpoint");
}

std::vector<ResolvedStyle> styles_for(const UiTree& ui, Color background, std::optional<Transition> transition) {
    std::vector<ResolvedStyle> styles(ui.size());
    styles[1].background = background;
    styles[1].transition = transition;
    // The anonymous box changes the same way and must never transition.
    styles[3].background = background;
    styles[3].transition = transition;
    return styles;
}

void interpolation_and_retargeting() {
    const auto ui = tree();
    const Transition linear{{.background = true}, 1, Easing::linear};
    StyleTransitions transitions;
    check(transitions.present(0).empty() && !transitions.settles_at(), "An empty presentation has no transitions");
    const auto first = styles_for(*ui, white, linear);
    check(transitions.update(*ui, first, 10).empty() && transitions.present(10) == first && !transitions.running(10),
          "The first generation presents its targets immediately");

    const auto second = styles_for(*ui, blue, linear);
    check(transitions.update(*ui, second, 10).empty(), "A changed generation is accepted");
    check(transitions.settles_at() == 11.0 && transitions.running(10.5) && !transitions.running(11),
          "A started transition reports when it settles");
    auto shown = transitions.present(10);
    check(shown[1].background == white && shown[3].background == blue,
          "A transition starts at the presented value; an anonymous node presents its target");
    shown = transitions.present(10.5);
    check(near(shown[1].background, {.6f, .7f, .9f, 1}) && shown[1].transition == linear,
          "Linear progress interpolates sRGB-encoded components halfway");
    check(transitions.present(9) == transitions.present(10), "Times before the start present the start value");
    check(transitions.present(11) == second && transitions.present(12) == second,
          "The end of a transition presents exactly the target");
    check(transitions.present(std::numeric_limits<double>::infinity()) == second,
          "A non-finite presentation time presents the targets");

    // Interruption: returning to white at 10.5 starts from the presented midpoint, without a jump.
    const auto midpoint = transitions.present(10.5)[1].background;
    check(transitions.update(*ui, first, 10.5).empty(), "A retargeting generation is accepted");
    check(transitions.present(10.5)[1].background == midpoint && transitions.settles_at() == 11.5,
          "Retargeting restarts the duration from the presented value");
    check(near(transitions.present(11)[1].background, {.8f, .85f, .95f, 1}),
          "The retargeted transition moves from the midpoint toward the new target");

    // An unrelated generation leaves the running transition untouched.
    auto unrelated = first;
    unrelated[2].color = blue;
    check(transitions.update(*ui, unrelated, 11).empty() &&
              near(transitions.present(11)[1].background, {.8f, .85f, .95f, 1}) &&
              transitions.present(11)[2].color == blue && transitions.settles_at() == 11.5,
          "An unchanged target continues its transition through later generations");

    // Equal inputs present equal styles in a separate instance.
    StyleTransitions replica;
    check(replica.update(*ui, first, 10).empty() && replica.update(*ui, second, 10).empty() &&
              replica.update(*ui, first, 10.5).empty() && replica.update(*ui, unrelated, 11).empty(),
          "A replica accepts the same generations");
    for (double t : {11.0, 11.2, 11.5})
        check(replica.present(t) == transitions.present(t), "Equal inputs present equal styles");

    transitions.settle();
    check(transitions.present(11.1) == unrelated && !transitions.settles_at(), "Settling presents the targets");
    transitions.clear();
    check(transitions.update(*ui, second, 0).empty() && transitions.present(0) == second,
          "Clearing forgets nodes and the previous time, so the next generation snaps");
}

void layer_composition() {
    const auto ui = tree();
    const Transition fade{{.opacity = true}, 1, Easing::linear};
    auto resting = styles_for(*ui, white, fade);
    auto dimmed = resting;
    dimmed[1].opacity = .4f;
    dimmed[1].composition = Composition::layer;
    StyleTransitions transitions;
    check(transitions.update(*ui, resting, 0).empty() && transitions.update(*ui, dimmed, 0).empty(),
          "Dimming a group is accepted");
    auto shown = transitions.present(.5);
    check(near(shown[1].opacity, .7f) && shown[1].composition == Composition::layer,
          "A group fading out composes as a layer from the start");
    check(transitions.update(*ui, resting, .5).empty(), "Restoring the group is accepted");
    shown = transitions.present(1);
    check(near(shown[1].opacity, .85f) && shown[1].composition == Composition::layer,
          "A group fading back keeps composing as a layer until opacity settles");
    auto unrelated = resting;
    unrelated[2].color = blue;
    check(transitions.update(*ui, unrelated, 1).empty() && transitions.present(1.2)[1].composition == Composition::layer,
          "The layer persists through unrelated generations while opacity eases");
    check(transitions.present(1.5)[1] == unrelated[1], "Settled opacity presents the target composition");
    auto recolored = resting;
    recolored[1].composition = Composition::layer;
    recolored[1].transition = Transition{{.background = true}, 1, Easing::linear};
    recolored[1].background = blue;
    StyleTransitions other;
    check(other.update(*ui, recolored, 0).empty() && other.update(*ui, resting, 0).empty() &&
              other.present(.5)[1].composition == Composition::direct,
          "Only an easing opacity holds a layer");
}

void snapping_rules() {
    const auto ui = tree();
    StyleTransitions transitions;
    // Fields a transition does not name, zero durations and absent transitions snap.
    const Transition outline_only{{.outline = true}, 1, Easing::linear};
    check(transitions.update(*ui, styles_for(*ui, white, outline_only), 0).empty() &&
              transitions.update(*ui, styles_for(*ui, blue, outline_only), 0).empty() &&
              transitions.present(0)[1].background == blue && !transitions.settles_at(),
          "A field the transition does not name presents its target immediately");
    const Transition instant{{.background = true}, 0, Easing::linear};
    check(transitions.update(*ui, styles_for(*ui, white, instant), 1).empty() &&
              transitions.present(1)[1].background == white && !transitions.settles_at(),
          "A zero duration presents its target immediately");
    // The target's transition applies: leaving a transitioned state without one snaps.
    const Transition linear{{.background = true}, 1, Easing::linear};
    check(transitions.update(*ui, styles_for(*ui, blue, linear), 2).empty() && transitions.settles_at() == 3.0 &&
              transitions.update(*ui, styles_for(*ui, white, std::nullopt), 2.5).empty() &&
              transitions.present(2.5)[1].background == white && !transitions.settles_at(),
          "A target without a transition ends a running one at its own value");
    // A new node presents its target; a replaced tree matches by author ID.
    const auto grown = tree(true);
    auto styles = styles_for(*grown, blue, linear);
    styles[4].background = blue;
    styles[4].transition = linear;
    check(transitions.update(*grown, styles, 3).empty(), "A grown tree is accepted");
    const auto shown = transitions.present(3);
    check(shown[1].background == white && shown[4].background == blue,
          "Matching nodes transition by author ID across trees; new nodes present their target");
}

void effects_and_paint_fields() {
    const auto ui = tree();
    const Transition all{TransitionProperties::all(), 1, Easing::linear};
    std::vector<ResolvedStyle> start(ui->size()), end(ui->size());
    start[1].transition = end[1].transition = all;
    start[1].opacity = 0;
    end[1].opacity = 1;
    start[1].corner_radius = 2;
    end[1].corner_radius = 6;
    start[1].border_color = clear;
    end[1].border_color = blue;
    start[1].color = Color{0, 0, 0, 1};
    end[1].color = white;
    end[1].box_shadow = BoxShadow{{0, 4}, 6, {0, 0, 0, .4f}};
    end[1].outline = Outline{2, 1, blue, 4, 2, 0};
    StyleTransitions transitions;
    check(transitions.update(*ui, start, 0).empty() && transitions.update(*ui, end, 0).empty(),
          "Every paint field accepts a transition");
    auto mid = transitions.present(.5)[1];
    check(near(mid.opacity, .5) && near(mid.corner_radius, 4) && near(mid.color, {.5f, .5f, .5f, 1}),
          "Opacity, corner radius and text color interpolate");
    check(near(mid.border_color, {.2f, .4f, .8f, .5f}),
          "Premultiplied interpolation from transparent keeps the target hue");
    check(mid.box_shadow && mid.box_shadow->offset == Point{0, 4} && mid.box_shadow->softness == 6 &&
              near(mid.box_shadow->color, {0, 0, 0, .2f}) && !mid.box_shadow->inset,
          "An absent shadow fades in as the target geometry with a transparent color");
    check(mid.outline && mid.outline->width == 2 && mid.outline->dash_length == 4 &&
              near(mid.outline->color, {.2f, .4f, .8f, .5f}),
          "An absent outline fades in as the target outline");
    check(validate(mid).empty(), "Intermediate styles are valid resolved styles");
    check(transitions.present(1)[1] == end[1], "The end presents the exact target, including effects");

    // Fading an effect out ends with no effect.
    check(transitions.update(*ui, start, 2).empty(), "Fading out is accepted");
    mid = transitions.present(2.5)[1];
    check(mid.box_shadow && near(mid.box_shadow->color, {0, 0, 0, .2f}) && mid.outline &&
              near(mid.outline->color, {.2f, .4f, .8f, .5f}),
          "A removed effect fades out with its own geometry");
    check(!transitions.present(3)[1].box_shadow && !transitions.present(3)[1].outline,
          "A faded-out effect ends absent");

    // Shadows of different kinds switch; a solid outline becoming dashed keeps a valid pattern.
    auto inset = end;
    inset[1].box_shadow->inset = true;
    inset[1].outline = Outline{2, 1, blue, 0, 0, 0};
    check(transitions.update(*ui, end, 4).empty() && transitions.update(*ui, inset, 5).empty(),
          "A shadow kind change is accepted");
    mid = transitions.present(5.5)[1];
    check(mid.box_shadow == inset[1].box_shadow && mid.outline->dash_length == 2 && mid.outline->gap_length == 1,
          "Outer and inset shadows cannot blend and present the target; dash lengths interpolate");
    check(validate(mid).empty(), "Interpolated dash patterns stay valid");

    // Presented styles never change layout or semantics: only paint differs.
    PlaceholderTextShaper text;
    const auto observe = [&](const std::vector<ResolvedStyle>& styles) {
        auto layout = compute_layout({ui.get(), styles, {200, 60}, &text});
        check(bool(layout), "Transition fixture lays out");
        auto semantics = capture_semantics({ui.get(), styles, &*layout.value}, 1);
        auto paint = build_paint_list({ui.get(), styles, &*layout.value, &text});
        check(semantics && paint, "Transition fixture projects semantics and paints");
        return std::tuple{layout.value->boxes, std::move(*semantics.value), std::move(*paint.value)};
    };
    const auto [target_boxes, target_semantics, target_paint] = observe(inset);
    const auto [shown_boxes, shown_semantics, shown_paint] = observe(transitions.present(5.5));
    check(shown_boxes == target_boxes && shown_semantics == target_semantics && !(shown_paint == target_paint),
          "An intermediate presentation keeps target layout and semantics and changes only paint");
}

void diagnostics_and_resolution() {
    const auto ui = tree();
    StyleTransitions transitions;
    const auto first = styles_for(*ui, white, Transition{{.background = true}, 1});
    check(transitions.update(*ui, first, 5).empty(), "A valid generation is accepted");
    const auto before = transitions.present(5);
    std::vector<ResolvedStyle> short_styles(2);
    check(has(transitions.update(*ui, short_styles, 6), "transition_style_count", "/styles"),
          "A style count that differs from the tree rejects");
    check(has(transitions.update(*ui, first, 4), "transition_time", "/now") &&
              has(transitions.update(*ui, first, std::numeric_limits<double>::quiet_NaN()), "transition_time", "/now"),
          "Earlier and non-finite times reject");
    auto invalid = styles_for(*ui, blue, Transition{{.background = true}, -1});
    check(has(transitions.update(*ui, invalid, 6), "out_of_range", "/styles/1/transition/duration"),
          "Negative durations reject at their style");
    invalid[1].transition->duration = std::numeric_limits<float>::infinity();
    check(has(transitions.update(*ui, invalid, 6), "invalid_number", "/styles/1/transition/duration"),
          "Non-finite durations reject");
    invalid[1].transition = Transition{{.background = true}, 1, static_cast<Easing>(9)};
    check(has(transitions.update(*ui, invalid, 6), "unknown_value", "/styles/1/transition/easing"),
          "Unknown easings reject");
    check(transitions.present(5) == before && !transitions.settles_at(), "Rejected updates leave state unchanged");

    // Rules declare transitions like any other field; state rules can replace them.
    const Transition quick{{.background = true, .outline = true}, .12f, Easing::ease_out};
    const Transition slow{{.background = true}, .3f, Easing::ease_in_out};
    StyleSheet sheet{{{StyleSelector::of_class("button"), {.background = white, .transition = quick}},
                      {StyleSelector::of_class("button", {.hover = true}), {.background = blue, .transition = slow}}}};
    auto resolved = resolve_styles({ui.get(), &sheet, {}});
    check(resolved && resolved.value->at(1).transition == quick && !resolved.value->at(2).transition,
          "A rule declares a transition and it does not inherit");
    resolved = resolve_styles({ui.get(), &sheet, {{*ui->find("button")}}});
    check(resolved && resolved.value->at(1).transition == slow, "A state rule replaces the transition");
    StyleDeclarations cleared;
    cleared.transition = std::optional<Transition>{};
    std::vector<StyleDeclarations> overrides(ui->size());
    overrides[1] = cleared;
    resolved = resolve_styles({ui.get(), &sheet, {}, overrides});
    check(resolved && !resolved.value->at(1).transition, "An empty declaration clears the transition");
    sheet.rules[0].declarations.transition = Transition{{.background = true}, -1};
    check(has(validate(sheet), "out_of_range", "/rules/0/declarations/transition/duration"),
          "Rule declarations validate their transition");

    // A theme token supplies the whole transition, so a host can switch motion off in one place.
    Theme theme{{{"motion", ThemeValue{std::optional<Transition>{quick}}}}};
    sheet.rules[0].declarations.transition.reset();
    sheet.rules[0].variables = {{"transition", {"motion", {}}}};
    resolved = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(resolved && resolved.value->at(1).transition == quick, "A theme token binds the transition");
    theme.variables[0].value = ThemeValue{std::optional<Transition>{}};
    resolved = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(resolved && !resolved.value->at(1).transition, "An empty token clears it");
    theme.variables[0].value = ThemeValue{std::optional<Transition>{Transition{{}, -2}}};
    check(has(validate(theme), "out_of_range", "/theme/variables/0/value/duration"),
          "Theme transition tokens validate their duration");
    theme.variables[0].value = ThemeValue{.2f};
    resolved = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(!resolved && has(resolved.diagnostics, "theme_type", "/sheet/rules/0/variables/0/variable"),
          "Transition bindings require the exact token type");
}
} // namespace

int main() {
    try {
        layer_composition();
        easing_curves();
        interpolation_and_retargeting();
        snapping_rules();
        effects_and_paint_fields();
        diagnostics_and_resolution();
        std::cout << "Style transition checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
