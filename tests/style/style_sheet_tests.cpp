#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;

namespace {

constexpr tessera::Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1}, white{1, 1, 1, 1};

tessera::UiNode box(std::string id, std::vector<std::string> classes, std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.id = std::move(id);
    result.classes = std::move(classes);
    result.children = std::move(children);
    return result;
}
tessera::UiNode label(std::string id) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.id = std::move(id);
    result.properties["text"] = std::string("label");
    return result;
}

// root > panel(.panel) > { a(.button .wide) > a-label, b(.wide .button, disabled) > b-label }
std::unique_ptr<tessera::UiTree> menu() {
    auto a = box("a", {"button", "wide"}, {label("a-label")});
    auto b = box("b", {"wide", "button"}, {label("b-label")});
    b.properties["disabled"] = true;
    tessera::UiDocument document;
    document.root = box("root", {}, {box("panel", {"panel"}, {std::move(a), std::move(b)})});
    auto created = tessera::UiTree::create(document);
    check(static_cast<bool>(created), "Fixture tree rejected");
    return std::move(*created.value);
}

struct Resolved {
    const tessera::UiTree& tree;
    std::vector<tessera::ResolvedStyle> styles;
    const tessera::ResolvedStyle& operator[](const char* id) const { return styles[tree.find(id)->index]; }
};

Resolved resolve(const tessera::UiTree& tree, const tessera::StyleSheet& sheet, tessera::InteractionState state = {},
                 std::span<const tessera::StyleDeclarations> overrides = {}) {
    auto result = tessera::resolve_styles({&tree, &sheet, std::move(state), overrides});
    check(static_cast<bool>(result), "Resolution rejected");
    check(result.diagnostics.empty(), "Accepted resolution reported diagnostics");
    return {tree, std::move(*result.value)};
}

tessera::StyleDeclarations background(tessera::Color color) {
    tessera::StyleDeclarations result;
    result.background = color;
    return result;
}

void test_selectors_and_order() {
    const auto tree = menu();
    using S = tessera::StyleSelector;
    // Without rules every node has primitive defaults.
    for (const auto& style : resolve(*tree, {}).styles)
        check(style == tessera::ResolvedStyle{}, "Empty sheet changed a style");

    tessera::StyleDeclarations text_rule;
    text_rule.text.size = 20.0f;
    const tessera::StyleSheet sheet{{
        {S::of_class("button", {.hover = true}), background(green)}, // State rules follow base rules.
        {S::of_id("a"), background(blue)},
        {S::of_class("wide"), background(red)}, // A later base rule replaces the ID rule: no specificity.
        {S::of_type(tessera::NodeKind::text), text_rule},
    }};
    const auto plain = resolve(*tree, sheet);
    check(plain["a"].background == red && plain["b"].background == red, "Source order did not decide base rules");
    check(plain["panel"].background == tessera::Color{}, "Unmatched class applied");
    check(plain["a-label"].text.size == 20 && plain["a"].text.size == 16, "Type selector matched the wrong kind");

    const auto hovered = resolve(*tree, sheet, {{*tree->find("a")}});
    check(hovered["a"].background == green, "State rule declared first did not follow base rules");

    // Reordering a node's classes changes nothing: class lists are sets for matching.
    tessera::StyleSheet swapped{{{S::of_class("wide"), background(red)}, {S::of_class("button"), background(blue)}}};
    const auto styles = resolve(*tree, swapped);
    check(styles["a"].background == blue && styles["b"].background == blue, "Class-list order affected precedence");
    std::swap(swapped.rules[0], swapped.rules[1]);
    const auto reversed = resolve(*tree, swapped);
    check(reversed["a"].background == red && reversed["b"].background == red, "Rule order did not decide the tie");
}

void test_gradients() {
    const auto tree = menu();
    const tessera::LinearGradient gradient{{0, 0}, {1, 1}, red, blue, {{.1f, red}, {.4f, white}, {.9f, blue}}};
    tessera::StyleDeclarations fill;
    fill.background_gradient = gradient;
    const tessera::StyleSheet sheet{
        {{tessera::StyleSelector::of_id("panel"), fill}, {tessera::StyleSelector::of_id("a"), fill}}};
    const auto plain = resolve(*tree, sheet);
    check(plain["panel"].background_gradient == tessera::Gradient{gradient} &&
              plain["a"].background_gradient == tessera::Gradient{gradient} && !plain["a-label"].background_gradient &&
              !plain["b"].background_gradient,
          "Gradient declarations must resolve without inheritance");
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index].background_gradient = std::optional<tessera::Gradient>{};
    check(!resolve(*tree, sheet, {}, overrides)["a"].background_gradient,
          "An explicitly empty gradient override must clear a rule's gradient");
    std::get<tessera::LinearGradient>(fill.background_gradient->value()).start.x = NAN;
    check(has(tessera::validate(fill), "invalid_number", "/background_gradient/start/x"),
          "Non-finite gradient geometry must produce located style diagnostics");
    fill.background_gradient = tessera::LinearGradient{{0, 0}, {0, 0}, red, blue};
    check(has(tessera::validate(fill), "degenerate_gradient", "/background_gradient/end"),
          "Style validation must reject coincident gradient endpoints");
    const tessera::RadialGradient radial{
        .center = {.2f, .3f}, .radius = {.8f, 1.2f}, .stops = {{0, white}, {.4f, red}, {1, blue}}};
    overrides[tree->find("a")->index].background_gradient = radial;
    const auto changed = resolve(*tree, sheet, {}, overrides);
    check(changed["a"].background_gradient == tessera::Gradient{radial} &&
              changed["panel"].background_gradient == tessera::Gradient{gradient} &&
              !changed["a-label"].background_gradient,
          "Radial instance overrides must replace linear fills without inheritance");
    fill.background_gradient = radial;
    std::get<tessera::RadialGradient>(fill.background_gradient->value()).radius.height = 0;
    check(has(tessera::validate(fill), "out_of_range", "/background_gradient/radius/height"),
          "Radial style geometry must validate before resolution");
}

void test_outlines() {
    const auto tree = menu();
    const tessera::Outline outline{1.5f, 1, blue, 4, 2, -1};
    tessera::StyleSheet sheet{{{tessera::StyleSelector::of_class("button", {.focus = true}), {.outline = outline}}}};
    auto resolved = resolve(*tree, sheet, {.focused = *tree->find("a")});
    check(resolved["a"].outline == outline && !resolved["a-label"].outline && !resolved["b"].outline,
          "Focus outlines must be local, typed decoration rather than inherited layout state");
    sheet.rules.push_back({tessera::StyleSelector::of_id("a"), {.outline = std::optional<tessera::Outline>{}}});
    check(resolve(*tree, sheet, {.focused = *tree->find("a")})["a"].outline == outline,
          "State outlines must follow the existing base/state cascade");
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index].outline = std::optional<tessera::Outline>{};
    check(!resolve(*tree, sheet, {.focused = *tree->find("a")}, overrides)["a"].outline,
          "Instance overrides must explicitly clear outlines");
    tessera::StyleDeclarations invalid;
    invalid.outline = outline;
    invalid.outline->value().offset = -1;
    invalid.outline->value().gap_length = NAN;
    check(has(tessera::validate(invalid), "out_of_range", "/outline/offset") &&
              has(tessera::validate(invalid), "invalid_number", "/outline/gap_length"),
          "Outline declarations must use located shared validation");
}

void test_fill_shapes() {
    const auto tree = menu();
    tessera::StyleSheet sheet{{{tessera::StyleSelector::of_id("a"),
                                {.background_shape = tessera::FillShape::ellipse, .background_antialias = true}}}};
    const auto resolved = resolve(*tree, sheet);
    check(resolved["a"].background_shape == tessera::FillShape::ellipse && resolved["a"].background_antialias &&
              resolved["a-label"].background_shape == tessera::FillShape::rectangle &&
              !resolved["a-label"].background_antialias,
          "Fill shape and coverage must not inherit into children");
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index] = {.background_shape = tessera::FillShape::rectangle,
                                         .background_antialias = false};
    const auto cleared = resolve(*tree, sheet, {}, overrides);
    check(cleared["a"].background_shape == tessera::FillShape::rectangle && !cleared["a"].background_antialias,
          "Instance declarations must restore rectangle and hard coverage explicitly");
    overrides[0].background_shape = static_cast<tessera::FillShape>(255);
    check(has(tessera::validate(overrides[0]), "unknown_value", "/background_shape"),
          "Unknown fill shapes must reject with a located diagnostic");
}

void test_procedural_fills() {
    const auto tree = menu();
    const tessera::ProceduralFill wave{"example.wave", {.5f}, 2};
    tessera::StyleSheet sheet{{{tessera::StyleSelector::of_id("a"), {.background_procedural = wave}}}};
    const auto resolved = resolve(*tree, sheet);
    check(resolved["a"].background_procedural == wave && !resolved["a-label"].background_procedural,
          "Procedural fills must apply to the matched node without inheriting");
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index].background_procedural = std::optional<tessera::ProceduralFill>{};
    check(!resolve(*tree, sheet, {}, overrides)["a"].background_procedural,
          "An explicit empty declaration must clear a procedural fill");
    overrides[0].background_procedural = tessera::ProceduralFill{"", {}, 0, 2};
    const auto errors = tessera::validate(overrides[0]);
    check(has(errors, "invalid_identifier", "/background_procedural/shader") &&
              has(errors, "out_of_range", "/background_procedural/opacity"),
          "Procedural declarations must use located shared validation");
}

void test_shadows() {
    const auto tree = menu();
    const tessera::BoxShadow shadow{{-2, 4}, 8, {.2f, .3f, .4f, .5f}};
    tessera::StyleDeclarations declarations;
    declarations.box_shadow = shadow;
    tessera::StyleSheet sheet{
        {{tessera::StyleSelector::of_id("panel"), declarations}, {tessera::StyleSelector::of_id("a"), declarations}}};
    auto resolved = resolve(*tree, sheet);
    check(resolved["panel"].box_shadow == shadow && resolved["a"].box_shadow == shadow &&
              !resolved["a-label"].box_shadow && !resolved["b"].box_shadow,
          "Shadows must resolve through ordinary rules without inheriting");
    auto inset = shadow;
    inset.inset = true;
    sheet.rules.push_back({tessera::StyleSelector::of_id("a"), {.box_shadow = inset}});
    const auto inset_resolved = resolve(*tree, sheet);
    check(inset_resolved["panel"].box_shadow == shadow && inset_resolved["a"].box_shadow == inset &&
              !inset_resolved["a-label"].box_shadow,
          "Inset shadows must replace outer shadows without inheriting or changing siblings");
    tessera::StyleDeclarations cleared;
    cleared.box_shadow = std::optional<tessera::BoxShadow>{};
    sheet.rules.push_back({tessera::StyleSelector::of_id("a", {.focus = true}), cleared});
    check(!resolve(*tree, sheet, {.focused = *tree->find("a")})["a"].box_shadow,
          "State rules must explicitly clear a shadow");
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("panel")->index] = cleared;
    check(!resolve(*tree, sheet, {}, overrides)["panel"].box_shadow,
          "Instance overrides must clear shadows without changing other nodes");
    declarations.box_shadow->value().offset.x = NAN;
    declarations.box_shadow->value().softness = -1;
    declarations.box_shadow->value().color.a = 2;
    const auto diagnostics = tessera::validate(declarations);
    check(diagnostics.size() == 3 && has(diagnostics, "invalid_number", "/box_shadow/offset/x") &&
              has(diagnostics, "out_of_range", "/box_shadow/softness") &&
              has(diagnostics, "out_of_range", "/box_shadow/color/a"),
          "Shadow geometry and colors must reject with exact style locations");
}

void test_inheritance() {
    const auto tree = menu();
    using S = tessera::StyleSelector;
    tessera::StyleDeclarations panel;
    panel.color = white;
    panel.text.size = 24.0f;
    panel.text.weight = std::uint16_t{700};
    panel.text.line_height = 30.0f;
    panel.background = red;
    panel.padding = tessera::Edges{4, 4, 4, 4};
    panel.opacity = 0.5f;
    panel.visibility = tessera::Visibility::hidden;
    tessera::StyleDeclarations label;
    label.text.size = 12.0f;
    label.text.line_height = std::optional<float>{};
    const tessera::StyleSheet sheet{{{S::of_class("panel"), panel}, {S::of_id("b-label"), label}}};
    const auto styles = resolve(*tree, sheet);
    const auto& a = styles["a-label"];
    check(a.color == white && a.text.size == 24 && a.text.weight == 700 && a.text.line_height == 30.0f,
          "Text and color did not inherit through descendants");
    check(a.background == tessera::Color{} && a.padding == tessera::Edges{} && a.opacity == 1 &&
              a.visibility == tessera::Visibility::visible && styles["a"].background == tessera::Color{},
          "A non-inherited value reached a descendant");
    const auto& b = styles["b-label"];
    check(b.text.size == 12 && !b.text.line_height && b.text.weight == 700 && b.color == white,
          "A rule did not replace only its declared inherited values");
    check(styles["root"] == tessera::ResolvedStyle{}, "Inheritance flowed toward the root");
}

void test_states() {
    const auto tree = menu();
    using S = tessera::StyleSelector;
    const auto a = *tree->find("a"), b = *tree->find("b"), a_label = *tree->find("a-label"),
               b_label = *tree->find("b-label");
    const auto panel = *tree->find("panel");
    tessera::StyleDeclarations hover, active, focus, disabled, both;
    hover.background = green;
    active.border_color = red;
    focus.color = blue;
    disabled.opacity = 0.5f;
    both.corner_radius = 3.0f;
    const tessera::StyleSheet sheet{{
        {S::of_type(tessera::NodeKind::box, {.hover = true}), hover},
        {S::of_type(tessera::NodeKind::box, {.active = true}), active},
        {S::of_class("button", {.focus = true}), focus},
        {S::of_type(tessera::NodeKind::text, {.disabled = true}), disabled},
        {S::of_class("button", {.hover = true, .focus = true}), both},
    }};

    // Hovering a label matches its box ancestors; focus matches the node only and its descendants inherit.
    const auto styles = resolve(*tree, sheet, {{a_label}, {a}, a});
    check(styles["a"].background == green && styles["panel"].background == green && styles["root"].background == green,
          "Hover did not match the target's ancestors");
    check(styles["a"].border_color == red && styles["panel"].border_color == red &&
              styles["a-label"].border_color == tessera::Color{},
          "Active did not match the owner and its ancestors only");
    check(styles["a"].color == blue && styles["a-label"].color == blue && styles["panel"].color != blue,
          "Focus did not match only the focused node");
    check(styles["a"].corner_radius == 3, "A rule requiring two matching states did not apply");
    const auto unfocused = resolve(*tree, sheet, {{a_label}});
    check(unfocused["a"].corner_radius == 0 && unfocused["a"].color != blue, "A partial state match applied");

    // Disabled is inherited from the node or an ancestor and suppresses every interaction state.
    check(styles["b-label"].opacity == 0.5f && styles["a-label"].opacity == 1,
          "Disabled did not inherit to descendants");
    const auto stale = resolve(*tree, sheet, {{b_label}, {b}, b});
    check(stale["b"].background == tessera::Color{} && stale["b"].border_color == tessera::Color{} &&
              stale["b"].color != blue,
          "A disabled node matched an interaction state");
    check(stale["panel"].background == green && stale["panel"].border_color == red,
          "An enabled ancestor lost its state");

    // Multiple pointers mark every hovered/active path; the result is deterministic.
    const auto two = resolve(*tree, sheet, {{a_label, panel}, {}, {}});
    check(two["a"].background == green && two["panel"].background == green, "A second hovered target was ignored");
    check(resolve(*tree, sheet, {{a_label}, {a}, a}).styles == styles.styles, "Repeated resolution differed");
}

void test_overrides() {
    const auto tree = menu();
    using S = tessera::StyleSelector;
    const tessera::StyleSheet sheet{
        {{S::of_id("a"), background(blue)}, {S::of_class("button", {.hover = true}), background(green)}}};
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index].background = red;
    overrides[tree->find("panel")->index].color = white;
    const auto styles = resolve(*tree, sheet, {{*tree->find("a")}}, overrides);
    check(styles["a"].background == red, "An ID or state rule outranked the instance override");
    check(styles["a-label"].color == white, "An override did not provide the inherited value");
    check(styles["b"].background == tessera::Color{} && styles["root"].color != white,
          "An override reached an unrelated node");
}

void test_diagnostics() {
    const auto tree = menu();
    const auto other = menu();
    using S = tessera::StyleSelector;
    tessera::StyleDeclarations invalid;
    invalid.opacity = 2.0f;
    invalid.min_width = 10.0f;
    invalid.max_width = 5.0f;
    invalid.text.size = 0.0f;
    invalid.text.line_height = std::nanf("");
    invalid.display = static_cast<tessera::Display>(7);
    const auto declared = tessera::validate(invalid, "/x");
    check(has(declared, "out_of_range", "/x/opacity") && has(declared, "conflicting_constraints", "/x/min_width") &&
              has(declared, "out_of_range", "/x/text/size") && has(declared, "invalid_number", "/x/text/line_height") &&
              has(declared, "unknown_value", "/x/display") && declared.size() == 5,
          "Declarations were not validated once per declared value");
    check(tessera::validate(tessera::StyleDeclarations{}).empty(), "Empty declarations reported diagnostics");

    tessera::StyleSelector bad_kind;
    bad_kind.kind = static_cast<tessera::StyleSelector::Kind>(9);
    const tessera::StyleSheet sheet{{
        {S::of_class(""), {}},
        {S::of_id("bad\n"), {}},
        {S::of_class("\xff"), {}},
        {bad_kind, {}},
        {S::of_type(static_cast<tessera::NodeKind>(5)), invalid},
    }};
    std::vector<tessera::StyleDeclarations> short_overrides(1);
    short_overrides[0].gap = -1.0f;
    auto result = tessera::resolve_styles(
        {tree.get(), &sheet, {{*other->find("a")}, {{tree->root().tree, 99}}, *other->find("a")}, short_overrides});
    check(!result, "Invalid style input accepted");
    const auto& d = result.diagnostics;
    check(has(d, "invalid_identifier", "/sheet/rules/0/selector/name") &&
              has(d, "invalid_identifier", "/sheet/rules/1/selector/name") &&
              has(d, "invalid_utf8", "/sheet/rules/2/selector/name") &&
              has(d, "unknown_value", "/sheet/rules/3/selector/kind") &&
              has(d, "unknown_value", "/sheet/rules/4/selector/type") &&
              has(d, "out_of_range", "/sheet/rules/4/declarations/opacity") &&
              has(d, "stale_target", "/state/hovered/0") && has(d, "stale_target", "/state/active/0") &&
              has(d, "stale_target", "/state/focused") && has(d, "override_count", "/overrides") &&
              has(d, "out_of_range", "/overrides/0/gap"),
          "Style input diagnostics missing");
    check(has(tessera::validate(tessera::StyleInput{}), "missing_input", "/tree") &&
              has(tessera::validate(tessera::StyleInput{}), "missing_input", "/sheet"),
          "Missing inputs accepted");

    // Values valid per rule can still conflict once resolved; the node is reported.
    tessera::StyleDeclarations minimum, maximum;
    minimum.min_height = 50.0f;
    maximum.max_height = 20.0f;
    const tessera::StyleSheet conflict{{{S::of_class("button"), minimum}, {S::of_id("a"), maximum}}};
    auto conflicting = tessera::resolve_styles({tree.get(), &conflict, {}, {}});
    check(!conflicting &&
              has(conflicting.diagnostics, "conflicting_constraints",
                  "/nodes/" + std::to_string(tree->find("a")->index) + "/min_height") &&
              conflicting.diagnostics.size() == 1,
          "Resolved conflict was not located at its node");
}

void test_placement_declarations() {
    tessera::StyleDeclarations placed;
    placed.grid_column_span = std::uint16_t{2};
    placed.grid_row_span = std::uint16_t{3};
    placed.position = tessera::Position::absolute;
    placed.top = std::optional<float>{-4.0f};
    placed.right = std::optional<float>{6.0f};
    placed.flex_wrap = tessera::FlexWrap::wrap;
    placed.z_index = std::int32_t{-2};
    tessera::ResolvedStyle style;
    style.left = 1.0f;
    tessera::apply(placed, style);
    check(style.grid_column_span == 2 && style.grid_row_span == 3 && style.position == tessera::Position::absolute &&
              style.top == -4.0f && style.right == 6.0f && style.left == 1.0f && !style.bottom &&
              style.flex_wrap == tessera::FlexWrap::wrap && style.z_index == -2,
          "Placement declarations must replace only the declared span, position, offsets, wrapping and z_index");
    tessera::StyleDeclarations cleared;
    cleared.left = std::optional<float>{};
    cleared.position = tessera::Position::flow;
    tessera::apply(cleared, style);
    check(!style.left && style.position == tessera::Position::flow && style.top == -4.0f,
          "An empty offset declaration must select automatic placement");
    tessera::StyleDeclarations invalid;
    invalid.grid_column_span = std::uint16_t{0};
    invalid.grid_row_span = std::uint16_t{257};
    invalid.position = static_cast<tessera::Position>(5);
    invalid.bottom = std::optional<float>{std::nanf("")};
    invalid.flex_wrap = static_cast<tessera::FlexWrap>(4);
    const auto declared = tessera::validate(invalid, "/x");
    check(has(declared, "out_of_range", "/x/grid_column_span") && has(declared, "out_of_range", "/x/grid_row_span") &&
              has(declared, "unknown_value", "/x/position") && has(declared, "invalid_number", "/x/bottom") &&
              has(declared, "unknown_value", "/x/flex_wrap") && declared.size() == 5,
          "Placement declarations must validate once per declared value");
}

} // namespace

int main() {
    try {
        test_selectors_and_order();
        test_gradients();
        test_shadows();
        test_outlines();
        test_fill_shapes();
        test_procedural_fills();
        test_inheritance();
        test_states();
        test_overrides();
        test_diagnostics();
        test_placement_declarations();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "style sheet checks passed\n";
}
