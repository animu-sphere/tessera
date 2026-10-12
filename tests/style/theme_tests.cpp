#include <tessera/style/style_sheet.hpp>
#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
constexpr Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, blue{0, 0, 1, 1};
ThemeBinding binding(std::string property, std::string name, std::optional<ThemeValue> fallback = {}) {
    return {std::move(property), {std::move(name), std::move(fallback)}};
}
std::unique_ptr<UiTree> tree() {
    UiDocument document;
    document.root.id = "root";
    UiNode button; button.id = "button"; button.classes = {"button"}; button.properties["focusable"] = true;
    UiNode label; label.id = "label"; label.kind = NodeKind::text; label.properties["text"] = std::string("Tools");
    button.children.push_back(label);
    document.root.children.push_back(button);
    auto created = UiTree::create(document);
    check(bool(created), "Theme fixture document is valid");
    return std::move(*created.value);
}

void cascade_and_inheritance() {
    const auto ui = tree();
    Theme theme;
    theme.variables = {{"accent", ThemeValue{red}}, {"alias", ThemeReference{"accent", ThemeValue{blue}}},
                       {"font-size", ThemeValue{20.0f}}, {"pad", ThemeValue{Edges{2, 2, 2, 2}}}};
    theme.defaults.rules = {
        {StyleSelector::of_id("root"), {}, {binding("color", "alias"), binding("text/size", "font-size"), binding("padding", "pad")}},
        {StyleSelector::of_class("button", {.hover = true}), {}, {binding("background", "accent")}},
        {StyleSelector::of_id("button"), {}, {binding("background", "absent", ThemeValue{blue})}}
    };
    StyleSheet sheet;
    auto resolved = resolve_styles({ui.get(), &sheet, {{*ui->find("label")}}, {}, &theme});
    check(resolved && resolved.value->at(1).background == red, "Theme state rules follow theme base rules");
    check(resolved.value->at(2).color == red && resolved.value->at(2).text.size == 20 &&
          resolved.value->at(1).padding == Edges{} && resolved.value->at(0).padding == Edges{2, 2, 2, 2},
          "Theme output uses the ordinary inheritance whitelist");
    StyleDeclarations explicit_rule; explicit_rule.background = green;
    sheet.rules.push_back({StyleSelector::of_id("button"), explicit_rule});
    resolved = resolve_styles({ui.get(), &sheet, {{*ui->find("label")}}, {}, &theme});
    check(resolved && resolved.value->at(1).background == green, "Explicit base rule outranks theme state rule");
    std::vector<StyleDeclarations> overrides(ui->size()); overrides[1].background = blue;
    resolved = resolve_styles({ui.get(), &sheet, {{*ui->find("label")}}, overrides, &theme});
    check(resolved && resolved.value->at(1).background == blue, "Instance override follows both layers");

    // Bindings replace same-rule literals, including aliases declared after their consumers.
    theme.variables = {{"alias", ThemeReference{"accent", {}}}, {"accent", ThemeValue{red}}};
    theme.defaults.rules.clear();
    sheet.rules = {{StyleSelector::of_id("button"), explicit_rule, {binding("background", "alias")}}};
    resolved = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(resolved && resolved.value->at(1).background == red, "Bindings replace literals without declaration-order dependence");
    auto repeated = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(repeated.value == resolved.value, "Repeated theme resolution is deterministic");

    sheet.rules[0].variables = {binding("background", "absent", ThemeValue{blue})};
    resolved = resolve_styles({ui.get(), &sheet});
    check(resolved && resolved.value->at(1).background == blue, "A literal fallback works without a supplied theme");
    theme.variables = {{"alias", ThemeReference{"absent", ThemeValue{red}}}};
    sheet.rules[0].variables = {binding("background", "alias")};
    check(resolve_styles({ui.get(), &sheet, {}, {}, &theme}).value->at(1).background == red, "Missing alias uses its own fallback");
}

void exact_types_and_destinations() {
    const auto ui = tree();
    // Every destination type passes through the ordinary declaration validator.
    const std::vector<std::pair<std::string, ThemeValue>> fields{
        {"display", Display::flex}, {"direction", FlexDirection::row}, {"justify", Justify::center},
        {"align", Align::start}, {"overflow", Overflow::clip}, {"width", Dimension::points(90)},
        {"height", Dimension::points(30)}, {"min_width", 1.0f}, {"min_height", 2.0f},
        {"max_width", std::numeric_limits<float>::infinity()}, {"max_height", 80.0f},
        {"margin", Edges{1, 2, 3, 4}}, {"border", Edges{1, 1, 1, 1}}, {"padding", Edges{2, 2, 2, 2}},
        {"gap", 3.0f}, {"grow", 1.0f}, {"shrink", 0.5f}, {"visibility", Visibility::visible},
        {"opacity", 0.7f}, {"background", red}, {"border_color", green}, {"corner_radius", 4.0f},
        {"text/font", FontId{9}}, {"text/size", 24.0f}, {"text/line_height", std::optional<float>{28.0f}},
        {"text/weight", std::uint16_t{600}}, {"color", blue}};
    Theme theme; StyleRule rule{StyleSelector::of_id("button"), {}};
    for (const auto& [field, value] : fields) {
        theme.variables.push_back({field, value}); rule.variables.push_back(binding(field, field));
    }
    const StyleSheet sheet{{rule}};
    auto result = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(bool(result), "All supported exact types resolve");
    ResolvedStyle expected;
    expected.direction = FlexDirection::row; expected.justify = Justify::center; expected.align = Align::start;
    expected.overflow = Overflow::clip; expected.width = Dimension::points(90); expected.height = Dimension::points(30);
    expected.min_width = 1; expected.min_height = 2; expected.max_height = 80;
    expected.margin = {1, 2, 3, 4}; expected.border = {1, 1, 1, 1}; expected.padding = {2, 2, 2, 2};
    expected.gap = 3; expected.grow = 1; expected.shrink = 0.5f; expected.opacity = 0.7f;
    expected.background = red; expected.border_color = green; expected.corner_radius = 4;
    expected.text = {FontId{9}, 24, 28.0f, 600}; expected.color = blue;
    check(result.value->at(1) == expected, "Every token reaches the declared style destination");
    theme.variables[24].value = ThemeValue{std::optional<float>{}};
    result = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(result && !result.value->at(1).text.line_height, "Empty optional selects default line height");

    StyleSheet wrong{{{StyleSelector::of_id("button"), {}, {binding("width", "number", ThemeValue{Dimension::points(3)})}}}};
    theme.variables = {{"number", ThemeValue{3.0f}}}; theme.defaults.rules.clear();
    auto rejected = resolve_styles({ui.get(), &wrong, {}, {}, &theme});
    check(!rejected && has(rejected.diagnostics, "theme_type", "/sheet/rules/0/variables/0/variable"),
          "Fallback cannot conceal a type mismatch or convert float to Dimension");
    wrong.rules[0].variables = {binding("text/weight", "number")};
    check(!resolve_styles({ui.get(), &wrong, {}, {}, &theme}), "Float does not convert to text weight");
    wrong.rules[0].variables = {binding("text/size", "number")}; theme.variables[0].value = ThemeValue{0.0f};
    rejected = resolve_styles({ui.get(), &wrong, {}, {}, &theme});
    check(has(rejected.diagnostics, "out_of_range", "/sheet/rules/0/declarations/text/size"), "Binding applies destination constraints");
    theme.variables[0].value = ThemeValue{std::numeric_limits<float>::infinity()};
    check(!resolve_styles({ui.get(), &wrong, {}, {}, &theme}), "Infinity is allowed only at a valid destination");
}

void diagnostic_and_candidate_boundaries() {
    const auto ui = tree(); StyleSheet sheet;
    Theme theme;
    theme.variables = {{"unused", ThemeReference{"missing", {}}}};
    auto rejected = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(!rejected && has(rejected.diagnostics, "theme_missing", "/theme/variables/0/value/name"),
          "The complete candidate validates unused aliases");
    theme.variables = {{"a", ThemeReference{"b", ThemeValue{red}}}, {"b", ThemeReference{"a", ThemeValue{blue}}}};
    rejected = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(!rejected && has(rejected.diagnostics, "theme_cycle", "/theme/variables/1/value/name"), "Fallback does not conceal a two-node cycle");
    theme.variables = {{"a", ThemeReference{"a", ThemeValue{red}}}};
    check(has(validate(theme), "theme_cycle", "/theme/variables/0/value/name"), "Self alias is a located cycle");
    theme.variables = {{"same", ThemeValue{red}}, {"same", ThemeValue{blue}}, {"bad\n", ThemeValue{red}},
                       {"\xff", ThemeValue{red}}, {std::string(129, 'x'), ThemeValue{red}}, {"", ThemeValue{red}}};
    auto errors = validate(theme);
    check(has(errors, "theme_duplicate_variable", "/theme/variables/1/name") &&
          has(errors, "invalid_identifier", "/theme/variables/2/name") && has(errors, "invalid_utf8", "/theme/variables/3/name") &&
          has(errors, "theme_name_bound", "/theme/variables/4/name") && has(errors, "invalid_identifier", "/theme/variables/5/name"),
          "Names, exact duplicate identity, and name bounds are checked");
    theme.variables = {{"unused", ThemeValue{Color{2, 0, 0, 1}}}};
    errors = validate(theme);
    check(has(errors, "out_of_range", "/theme/variables/0/value/r"), "Unused literals validate their primitive type");
    theme.variables[0].value = ThemeValue{std::numeric_limits<float>::quiet_NaN()};
    check(has(validate(theme), "invalid_number", "/theme/variables/0/value"), "Unused NaN is rejected");

    theme = {};
    sheet.rules = {{StyleSelector::of_id("unmatched"), {}, {binding("background", "missing")}}};
    rejected = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(has(rejected.diagnostics, "theme_missing", "/sheet/rules/0/variables/0/variable/name"), "Unmatched rules are validated");
    sheet.rules[0].variables = {binding("unknown", "x"), binding("opacity", "x", ThemeValue{red}),
                               binding("opacity", "x", ThemeValue{0.5f})};
    errors = validate(sheet);
    check(has(errors, "theme_property", "/rules/0/variables/0/property") &&
          has(errors, "theme_type", "/rules/0/variables/1/variable/fallback") &&
          has(errors, "theme_duplicate_binding", "/rules/0/variables/2/property"), "Property, fallback type and duplicate bindings diagnose syntax");
    sheet.rules[0].variables = {binding("opacity", "missing", ThemeValue{2.0f})};
    check(!resolve_styles({ui.get(), &sheet}), "Invalid destination fallback is rejected");
    theme.variables = {{"opacity", ThemeValue{0.5f}}};
    sheet.rules[0].variables[0].variable.name = "opacity";
    check(!resolve_styles({ui.get(), &sheet, {}, {}, &theme}), "Unused invalid fallback is rejected");

    // Bounds precede traversal/copies; an exactly bounded alias chain is valid.
    theme = {}; sheet = {};
    for (std::size_t i = 0; i < max_theme_variables; ++i) {
        const auto name = std::to_string(i);
        if (i + 1 == max_theme_variables) theme.variables.push_back({name, ThemeValue{red}});
        else theme.variables.push_back({name, ThemeReference{std::to_string(i + 1), {}}});
    }
    sheet.rules = {{StyleSelector::of_id("button"), {}, {binding("background", "0")}}};
    check(resolve_styles({ui.get(), &sheet, {}, {}, &theme}).value->at(1).background == red, "Exactly bounded alias chain resolves");
    theme.variables.push_back({"extra", ThemeValue{red}});
    check(has(validate(theme), "theme_variable_bound", "/theme/variables"), "Over-limit variables reject before alias traversal");
    theme = {}; theme.defaults.rules.resize(max_theme_rules + 1);
    check(has(validate(theme), "theme_rule_bound", "/theme/defaults/rules"), "Theme rule bound is enforced");
    theme = {}; theme.defaults.rules = {{StyleSelector::of_id("root"), {}, {binding("gap", "space", ThemeValue{1.0f})}}};
    sheet.rules.clear();
    for (std::size_t i = 0; i < max_theme_bindings; ++i)
        sheet.rules.push_back({StyleSelector::of_id("button"), {}, {binding("gap", "space", ThemeValue{1.0f})}});
    check(bool(resolve_styles({ui.get(), &sheet})), "Exactly bounded binding table is valid");
    check(has(resolve_styles({ui.get(), &sheet, {}, {}, &theme}).diagnostics, "theme_binding_bound", "/theme/bindings"),
          "Theme and sheet share one binding budget");

    // A cross-layer conflict is located at the final node, with no candidate output.
    theme = {}; sheet = {};
    StyleDeclarations minimum; minimum.min_height = 50.0f;
    theme.defaults.rules = {{StyleSelector::of_id("button"), minimum}};
    StyleDeclarations maximum; maximum.max_height = 20.0f;
    sheet.rules = {{StyleSelector::of_id("button"), maximum}};
    rejected = resolve_styles({ui.get(), &sheet, {}, {}, &theme});
    check(!rejected && has(rejected.diagnostics, "conflicting_constraints", "/nodes/1/min_height"), "Combined theme/sheet constraints are checked");
}

void component_presentation_and_rejected_switch() {
    ReactiveRuntime runtime;
    KeyedCollection instances(runtime.owner(runtime.root(), "rows"));
    const auto keyed = instances.reconcile(runtime, std::vector<KeyedChild>{{"row", "item"}});
    check(bool(keyed), "Keyed component owner created");
    const auto owner = keyed.value->instances[0].owner;
    auto pinned = runtime.signal(owner, "pinned", true);
    int evaluations = 0;
    auto caption = runtime.computed<std::string>(owner, "caption", [&] { ++evaluations; return pinned.read() ? "Tools (pinned)" : "Tools"; });
    runtime.flush();
    ComponentDescription component; component.kind = "item";
    component.document.root.id = "row";
    UiNode label; label.kind = NodeKind::text; label.id = "label"; label.properties["text"] = std::string{};
    component.document.root.children.push_back(label);
    component.document.root.properties["focusable"] = true;
    component.props = {{"caption", PropertyType::string, {}}}; component.properties = {{"caption", "label", "text"}};
    StyleDeclarations override; override.height = Dimension::points(40);
    component.styles = {{"row", override}}; component.style_bindings = {{"row", "row"}};
    ComponentArguments arguments; arguments.props["caption"] = caption;
    auto lowered = lower_component(runtime, component, arguments);
    check(bool(lowered), "Component lowers to the ordinary document");
    auto ui = UiTree::create(lowered.value->document);
    StyleSheet sheet{{{StyleSelector::of_id("row"), {}, {binding("color", "ink"), binding("text/size", "size")}}}};
    Theme theme; theme.variables = {{"ink", ThemeValue{red}}, {"size", ThemeValue{16.0f}}};
    PlaceholderTextShaper text; FocusDispatcher focus;
    auto styles = resolve_styles({ui.value->get(), &sheet, {}, lowered.value->overrides, &theme});
    auto layout = compute_layout({ui.value->get(), *styles.value, {240, 80}, &text});
    check(styles && layout && bool(focus.focus({ui.value->get(), *styles.value, &*layout.value}, (*ui.value)->root())), "Initial full presentation focused");
    const auto old_styles = *styles.value; const auto old_layout = layout.value->boxes;
    theme.variables[0].value = ThemeValue{blue}; theme.variables[1].value = ThemeValue{20.0f};
    auto switched = resolve_styles({ui.value->get(), &sheet, {}, lowered.value->overrides, &theme});
    check(switched && switched.value->at(0).height == Dimension::points(40), "Component overrides remain above theme rules");
    StyleDeclarations authored = override; authored.color = blue; authored.text.size = 20.0f;
    std::vector<StyleDeclarations> direct((*ui.value)->size()); direct[0] = authored; const StyleSheet empty;
    auto reference = resolve_styles({ui.value->get(), &empty, {}, direct});
    check(reference.value == switched.value, "Theme switch agrees with literal full-tree style reference");
    auto actual_layout = compute_layout({ui.value->get(), *switched.value, {240, 80}, &text});
    auto reference_layout = compute_layout({ui.value->get(), *reference.value, {240, 80}, &text});
    check(actual_layout && reference_layout && actual_layout.value->boxes == reference_layout.value->boxes &&
          actual_layout.value->boxes != old_layout, "Text-size theme change recomputes full geometry");
    const HitTestInput hit{ui.value->get(), *switched.value, &*actual_layout.value};
    const auto refreshed = focus.refresh(hit);
    check(refreshed && refreshed.value->focused == (*ui.value)->root() && pinned.read() && evaluations == 1 &&
          instances.current("row", keyed.value->instances[0].lifetime), "Theme switch preserves focus, local state and owner without reactive evaluation");
    check(hit_test(hit, {1, 1}).value == hit_test({ui.value->get(), *reference.value, &*reference_layout.value}, {1, 1}).value,
          "Theme switch and reference agree on pointer targeting");
    auto ap = build_paint_list({ui.value->get(), *switched.value, &*actual_layout.value, &text});
    auto rp = build_paint_list({ui.value->get(), *reference.value, &*reference_layout.value, &text});
    auto as = capture_semantics({ui.value->get(), *switched.value, &*actual_layout.value}, 2);
    auto rs = capture_semantics({ui.value->get(), *reference.value, &*reference_layout.value}, 2);
    check(ap && rp && ap.value == rp.value && as && rs && as.value == rs.value, "Theme switch paint/semantics match the authored reference");
    theme.variables[1].value = ThemeValue{0.0f};
    const auto bad = resolve_styles({ui.value->get(), &sheet, {}, lowered.value->overrides, &theme});
    check(!bad && *styles.value == old_styles && layout.value->boxes == old_layout &&
          instances.current("row", keyed.value->instances[0].lifetime) && evaluations == 1,
          "Rejected candidate leaves accepted owned styles and geometry untouched");
}
} // namespace

int main() {
    try {
        cascade_and_inheritance();
        exact_types_and_destinations();
        diagnostic_and_candidate_boundaries();
        component_presentation_and_rejected_switch();
        std::cout << "Theme variables and presentation checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
