#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

static_assert(std::is_constructible_v<ComponentStyleValue, Signal<StyleDeclarations>>);
static_assert(std::is_constructible_v<ComponentStyleValue, Computed<StyleDeclarations>>);
static_assert(!std::is_constructible_v<ComponentStyleValue, Signal<ResolvedStyle>>);

namespace {
ComponentDescription panel() {
    ComponentDescription c; c.kind = "styled-panel"; c.document.root.id = "panel";
    UiNode slot; slot.id = "slot";
    UiNode caption; caption.kind = NodeKind::text; caption.id = "caption";
    caption.properties["text"] = std::string("Tools");
    c.document.root.children = {slot, caption};
    c.slots = {{"body", "slot", NodeKinds::text, 0, 3}};
    StyleDeclarations inherited; inherited.color = Color{1, 0, 0, 1}; inherited.text.size = 18.0f;
    c.styles = {{"inherited", inherited}, {"caption", {}}};
    c.style_bindings = {{"inherited", "panel"}, {"caption", "caption"}};
    return c;
}
ComponentArguments arguments() {
    ComponentArguments a;
    StyleDeclarations style; style.height = Dimension::points(32); style.background = Color{0, 0, 1, 1};
    a.styles["caption"] = style;
    UiNode row; row.kind = NodeKind::text; row.id = "row"; row.properties["text"] = std::string("Map");
    row.extensions["host:item"].value = std::string("map");
    a.slots["body"] = {row};
    return a;
}

void full_tree_parity_and_alignment() {
    ReactiveRuntime r;
    auto height = r.signal(r.root(), "height", 20.0f);
    int evaluations = 0;
    const auto style = r.computed<StyleDeclarations>(r.root(), "style", [&] {
        ++evaluations; StyleDeclarations s; s.height = Dimension::points(height.read());
        s.background = Color{0, 0, 1, 1}; return s;
    });
    auto c = panel(); auto a = arguments(); a.styles["caption"] = style;
    check(has(lower_component(r, c, a).diagnostics, "reactive_unsettled", "/reactive/root/values/style") && evaluations == 0,
          "Style lowering cannot demand-evaluate unsettled calculations");
    r.batch([&] { height.write(28); height.write(32); }); r.flush();
    const auto lowered = lower_component(r, c, a);
    check(lowered && evaluations == 1, "Settled style is captured without recalculation");
    auto reference = c.document; reference.root.children[0].children = a.slots.at("body");
    check(lowered.value->document == reference && c.document == panel().document, "Styles stay outside the document and template");
    const auto encoded = save_document(lowered.value->document);
    check(encoded && load_document(*encoded.value).value == std::optional<UiDocument>{reference}, "Serialized document retains metadata without style syntax");
    auto tree = UiTree::create(reference);
    check(bool(tree) && lowered.value->overrides.size() == 4 && lowered.value->overrides[2] == StyleDeclarations{},
          "Overrides align to composed preorder including opaque slot nodes");
    auto expected = std::vector<StyleDeclarations>(4);
    expected[0] = *c.styles[0].default_value;
    expected[3].height = Dimension::points(32); expected[3].background = Color{0, 0, 1, 1};
    check(lowered.value->overrides == expected, "Mapped caption follows inserted slot content");
    StyleDeclarations competing; competing.color = Color{0, 1, 0, 1}; competing.background = Color{1, 1, 0, 1};
    StyleSheet sheet{{{StyleSelector::of_id("panel"), competing},
                      {StyleSelector::of_id("caption", {.hover = true}), competing}}};
    InteractionState state; state.hovered.push_back(*(*tree.value)->find("caption"));
    auto actual = resolve_styles({tree.value->get(), &sheet, state, lowered.value->overrides});
    auto authored = resolve_styles({tree.value->get(), &sheet, state, expected});
    check(actual && authored && *actual.value == *authored.value && actual.value->at(2).color == Color{1, 0, 0, 1} &&
          actual.value->at(2).text.size == 18 && actual.value->at(3).background == Color{0, 0, 1, 1},
          "Overrides follow state rules and inherited text reaches opaque slot children");
    PlaceholderTextShaper text;
    auto al = compute_layout({tree.value->get(), *actual.value, {240, 120}, &text});
    auto rl = compute_layout({tree.value->get(), *authored.value, {240, 120}, &text});
    check(al && rl && al.value->boxes == rl.value->boxes && al.value->boxes[3].border_box.size.height == 32,
          "Bound styles match authored full-tree geometry");
    auto ap = build_paint_list({tree.value->get(), *actual.value, &*al.value, &text});
    auto rp = build_paint_list({tree.value->get(), *authored.value, &*rl.value, &text});
    auto as = capture_semantics({tree.value->get(), *actual.value, &*al.value}, 1);
    auto rs = capture_semantics({tree.value->get(), *authored.value, &*rl.value}, 1);
    check(ap && rp && *ap.value == *rp.value && as && rs && *as.value == *rs.value, "Paint and semantics match authored style overrides");
    c.styles = {{"empty", StyleDeclarations{}}}; c.style_bindings = {{"empty", "panel"}, {"empty", "caption"}};
    a.styles.clear(); a.slots.clear();
    auto empty = lower_component(r, c, a);
    check(empty && empty.value->stages == PropertyStages::none && empty.value->overrides.size() == 3,
          "An empty style can supply several targets without reporting changed stages");
    c.style_bindings.clear();
    check(lower_component(r, c, a).value->overrides.empty(), "Without mappings the ordinary empty override span is preserved");
}

void declarations_arguments_and_bounds() {
    ReactiveRuntime r; auto c = panel(); auto a = arguments();
    r.begin_batch();
    a.styles.erase("caption"); a.styles["unknown/~"] = StyleDeclarations{};
    auto bad = c; bad.styles.push_back(c.styles[0]); bad.style_bindings.push_back(c.style_bindings[0]);
    bad.style_bindings.push_back({"missing", "row"}); // Slot content is outside template mappings.
    StyleDeclarations invalid; invalid.opacity = 2.0f;
    bad.styles.push_back({"unused", invalid});
    const auto rejected = lower_component(r, bad, a);
    check(has(rejected.diagnostics, "component_duplicate_style", "/component/styles/2/name") &&
          has(rejected.diagnostics, "component_duplicate_style_target", "/component/style_bindings/2/target") &&
          has(rejected.diagnostics, "component_unknown_style", "/component/style_bindings/3/style") &&
          has(rejected.diagnostics, "component_target", "/component/style_bindings/3/target") &&
          has(rejected.diagnostics, "out_of_range", "/component/styles/3/default/opacity"), "Schema errors precede reactive reads");
    auto rejected_arguments = lower_component(r, c, a);
    check(has(rejected_arguments.diagnostics, "component_missing_style", "/styles/caption") &&
          has(rejected_arguments.diagnostics, "component_unknown_style", "/styles/unknown~1~0"), "Style argument names are exact and located");
    a = arguments(); a.styles["caption"] = invalid;
    check(has(lower_component(r, c, a).diagnostics, "out_of_range", "/styles/caption/opacity"), "Literals validate before runtime entry");
    bad = c; bad.styles[0].name = std::string("\xff", 1);
    check(has(lower_component(r, bad, arguments()).diagnostics, "component_name", "/component/styles/0/name"), "Style names require UTF-8");
    bad = c; bad.styles.resize(max_component_styles + 1);
    check(has(lower_component(r, bad, arguments()).diagnostics, "component_limit", "/component"), "Style declarations are bounded");
    a = {}; for (std::size_t i = 0; i <= max_component_styles; ++i) a.styles[std::to_string(i)] = StyleDeclarations{};
    check(has(lower_component(r, c, a).diagnostics, "component_limit", "/component"), "Style arguments are bounded");
    bad = c; bad.properties.resize(max_property_bindings - 1); bad.events.resize(1);
    check(has(lower_component(r, bad, arguments()).diagnostics, "component_limit", "/component"), "Styles share the combined mapping budget");
    bad = c; bad.style_bindings.resize(max_property_bindings + 1);
    check(has(lower_component(r, bad, arguments()).diagnostics, "component_limit", "/component"), "Style-only mapping budget is bounded");
    r.end_batch();
    a = arguments(); invalid = {}; invalid.display = static_cast<Display>(255); a.styles["caption"] = invalid;
    check(has(lower_component(r, c, a).diagnostics, "unknown_value", "/styles/caption/display"), "Typed styles reject invalid enums");
    invalid = {}; invalid.text.size = std::numeric_limits<float>::infinity(); a.styles["caption"] = invalid;
    check(has(lower_component(r, c, a).diagnostics, "invalid_number", "/styles/caption/text/size"), "Typed styles reject nonfinite text sizes");
    // A valid declaration can conflict after cascade; presentation must validate before publication.
    a = arguments(); StyleDeclarations minimum; minimum.min_height = 40.0f; a.styles["caption"] = minimum;
    const auto candidate = lower_component(r, c, a); auto tree = UiTree::create(candidate.value->document);
    StyleDeclarations maximum; maximum.max_height = 20.0f;
    StyleSheet sheet{{{StyleSelector::of_id("caption"), maximum}}};
    check(has(resolve_styles({tree.value->get(), &sheet, {}, candidate.value->overrides}).diagnostics,
              "conflicting_constraints", "/nodes/3/min_height"), "Cascade conflicts reject full presentation");
}

void weak_lifetimes_faults_and_publication() {
    ReactiveRuntime r; KeyedCollection rows(r.owner(r.root(), "rows"));
    const auto first = rows.reconcile(r, {{"item", "styled-panel"}});
    StyleDeclarations initial; initial.opacity = 0.5f;
    auto style = r.signal(first.value->instances[0].owner, "style", initial);
    int notifications = 0;
    r.effect<StyleDeclarations>(first.value->instances[0].owner, "notification", EffectPhase::notify,
        [style] { return style.read(); }, [&](const StyleDeclarations&) { ++notifications; });
    r.flush(); auto c = panel(); auto a = arguments(); a.styles["caption"] = style;
    const auto accepted = lower_component(r, c, a);
    check(accepted && r.deliver_effects(EffectPhase::notify).empty() && notifications == 0, "Lowering captures no effects");
    check(r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notifications == 1, "Host publishes accepted presentation explicitly");
    auto invalid = initial; invalid.opacity = -1.0f; style.write(invalid); r.flush();
    check(has(lower_component(r, c, a).diagnostics, "out_of_range", "/styles/caption/opacity") &&
          accepted.value->overrides[3] == initial && r.deliver_effects(EffectPhase::notify).empty() && notifications == 1,
          "Invalid style preserves owned accepted data and captures no notifications");
    style.write(initial); r.flush();
    const auto reorder = rows.reconcile(r, {{"other", "styled-panel"}, {"item", "styled-panel"}});
    check(reorder && reorder.value->instances[1].lifetime == first.value->instances[0].lifetime && lower_component(r, c, a),
          "Compatible reorder preserves weak style references");
    rows.reconcile(r, {{"item", "replacement"}}); r.flush();
    check(has(lower_component(r, c, a).diagnostics, "disposed_reactive_value", "/styles/caption"), "Kind replacement expires style sources");
    ReactiveRuntime foreign; a.styles["caption"] = foreign.signal(foreign.root(), "style", initial);
    check(has(lower_component(r, c, a).diagnostics, "foreign_reactive_runtime", "/styles/caption"), "Styles cannot borrow foreign runtimes");
    auto boundary = r.boundary(r.root(), "fault");
    a.styles["caption"] = r.computed<StyleDeclarations>(boundary, "style", []() -> StyleDeclarations { throw std::runtime_error("missing style"); });
    check(r.flush().size() == 1, "Style calculation failure stays within its owner boundary");
    auto failed = lower_component(r, c, a);
    check(has(failed.diagnostics, "reactive_faulted_snapshot", "/styles/caption") || has(failed.diagnostics, "reactive_unsettled", "/styles/caption"),
          "Faulted style rejects without calculation retry");
    a.styles["caption"] = initial;
    check(bool(lower_component(r, c, a)), "Host can supply an explicit literal style fallback");
    c.styles.push_back({"unused", {}}); a.styles["unused"] = style;
    check(has(lower_component(r, c, a).diagnostics, "disposed_reactive_value", "/styles/unused"), "Unmapped declared styles are read and checked");
    r.begin_batch();
    check(has(lower_component(r, panel(), arguments()).diagnostics, "reactive_open_batch", "/reactive"), "Literal styles require a settled boundary");
    r.end_batch();
}
} // namespace

int main() {
    try {
        full_tree_parity_and_alignment(); declarations_arguments_and_bounds(); weak_lifetimes_faults_and_publication();
        std::cout << "Component style checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
