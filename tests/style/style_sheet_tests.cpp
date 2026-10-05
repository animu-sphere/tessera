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
    for (const auto& style : resolve(*tree, {}).styles) check(style == tessera::ResolvedStyle{}, "Empty sheet changed a style");

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
    const auto a = *tree->find("a"), b = *tree->find("b"), a_label = *tree->find("a-label"), b_label = *tree->find("b-label");
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
    check(styles["a"].border_color == red && styles["panel"].border_color == red && styles["a-label"].border_color == tessera::Color{},
          "Active did not match the owner and its ancestors only");
    check(styles["a"].color == blue && styles["a-label"].color == blue && styles["panel"].color != blue,
          "Focus did not match only the focused node");
    check(styles["a"].corner_radius == 3, "A rule requiring two matching states did not apply");
    const auto unfocused = resolve(*tree, sheet, {{a_label}});
    check(unfocused["a"].corner_radius == 0 && unfocused["a"].color != blue, "A partial state match applied");

    // Disabled is inherited from the node or an ancestor and suppresses every interaction state.
    check(styles["b-label"].opacity == 0.5f && styles["a-label"].opacity == 1, "Disabled did not inherit to descendants");
    const auto stale = resolve(*tree, sheet, {{b_label}, {b}, b});
    check(stale["b"].background == tessera::Color{} && stale["b"].border_color == tessera::Color{} && stale["b"].color != blue,
          "A disabled node matched an interaction state");
    check(stale["panel"].background == green && stale["panel"].border_color == red, "An enabled ancestor lost its state");

    // Multiple pointers mark every hovered/active path; the result is deterministic.
    const auto two = resolve(*tree, sheet, {{a_label, panel}, {}, {}});
    check(two["a"].background == green && two["panel"].background == green, "A second hovered target was ignored");
    check(resolve(*tree, sheet, {{a_label}, {a}, a}).styles == styles.styles, "Repeated resolution differed");
}

void test_overrides() {
    const auto tree = menu();
    using S = tessera::StyleSelector;
    const tessera::StyleSheet sheet{{{S::of_id("a"), background(blue)}, {S::of_class("button", {.hover = true}), background(green)}}};
    std::vector<tessera::StyleDeclarations> overrides(tree->size());
    overrides[tree->find("a")->index].background = red;
    overrides[tree->find("panel")->index].color = white;
    const auto styles = resolve(*tree, sheet, {{*tree->find("a")}}, overrides);
    check(styles["a"].background == red, "An ID or state rule outranked the instance override");
    check(styles["a-label"].color == white, "An override did not provide the inherited value");
    check(styles["b"].background == tessera::Color{} && styles["root"].color != white, "An override reached an unrelated node");
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
    auto result = tessera::resolve_styles({tree.get(), &sheet, {{*other->find("a")}, {{tree->root().tree, 99}}, *other->find("a")}, short_overrides});
    check(!result, "Invalid style input accepted");
    const auto& d = result.diagnostics;
    check(has(d, "invalid_identifier", "/sheet/rules/0/selector/name") && has(d, "invalid_identifier", "/sheet/rules/1/selector/name") &&
              has(d, "invalid_utf8", "/sheet/rules/2/selector/name") && has(d, "unknown_value", "/sheet/rules/3/selector/kind") &&
              has(d, "unknown_value", "/sheet/rules/4/selector/type") && has(d, "out_of_range", "/sheet/rules/4/declarations/opacity") &&
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
    check(!conflicting && has(conflicting.diagnostics, "conflicting_constraints",
                              "/nodes/" + std::to_string(tree->find("a")->index) + "/min_height") &&
              conflicting.diagnostics.size() == 1,
          "Resolved conflict was not located at its node");
}

} // namespace

int main() {
    try {
        test_selectors_and_order();
        test_inheritance();
        test_states();
        test_overrides();
        test_diagnostics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "style sheet checks passed\n";
}
