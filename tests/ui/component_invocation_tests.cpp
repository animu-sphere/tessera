#include <tessera/ui/component.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {
ComponentInvocation box(std::string id) {
    ComponentInvocation v;
    v.component.kind = "container"; v.component.document.root.id = id;
    v.component.slots = {{"body", id, NodeKinds::all, 0, max_document_nodes}};
    return v;
}
std::vector<ComponentInvocation> fixture() {
    ComponentInvocation leaf;
    leaf.component.kind = "caption";
    auto& node = leaf.component.document.root;
    node.kind = NodeKind::text; node.id = "caption"; node.properties["text"] = std::string{};
    node.properties["focusable"] = true;
    node.extensions["host:item"].value = std::string("map");
    leaf.component.props = {{"label", PropertyType::string, {}}};
    leaf.component.properties = {{"label", "caption", "text"}};
    leaf.arguments.props["label"] = Property{std::string("Map")};
    leaf.component.actions = {{"select", std::string("select-item")}};
    leaf.component.events = {{"select", "caption", "activate"}};
    StyleDeclarations style; style.height = Dimension::points(24); style.background = Color{0, 0, 1, 1};
    leaf.component.styles = {{"row", style}};
    leaf.component.style_bindings = {{"row", "caption"}};
    auto middle = box("middle"); middle.invocation_slots["body"] = {0};
    auto outer = box("outer"); outer.invocation_slots["body"] = {1};
    StyleDeclarations inherited; inherited.text.size = 18.0f; inherited.color = Color{1, 0, 0, 1};
    outer.component.styles = {{"inherited", inherited}};
    outer.component.style_bindings = {{"inherited", "outer"}};
    outer.component.document.extensions["host:document"].value = true;
    return {leaf, middle, outer}; // Root and children need not follow array order.
}
const ValidationContext context{{"select-item"}};

void presentation_and_updates() {
    ReactiveRuntime r;
    auto label = r.signal(r.root(), "label", std::string("Map"));
    int evaluations = 0, notifications = 0;
    auto caption = r.computed<std::string>(r.root(), "caption", [&] { ++evaluations; return label.read(); });
    r.effect<std::string>(r.root(), "notification", EffectPhase::notify, [caption] { return caption.read(); },
        [&](const std::string&) { ++notifications; });
    auto v = fixture(); v[0].arguments.props["label"] = caption;
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "reactive_unsettled", "/reactive/root/values/caption") && evaluations == 0,
          "Invocation lowering cannot demand evaluation");
    r.flush();
    const auto lowered = lower_component_tree(r, v, 2, context);
    check(lowered && evaluations == 1 && notifications == 0, "Tree lowering reads settled values without publishing effects");
    auto authored = v[2].component.document;
    auto middle = v[1].component.document.root;
    auto leaf = v[0].component.document.root;
    leaf.properties["text"] = std::string("Map"); leaf.events["activate"] = "select-item";
    middle.children.push_back(leaf); authored.root.children.push_back(middle);
    std::vector<StyleDeclarations> overrides(3);
    overrides[0] = *v[2].component.styles[0].default_value;
    overrides[2] = *v[0].component.styles[0].default_value;
    check(lowered.value->document == authored && lowered.value->overrides == overrides,
          "Nested descriptions preserve document metadata, actions and aligned styles");
    auto tree = UiTree::create(authored, context); check(bool(tree), "Authored reference creates a tree");
    const StyleSheet sheet;
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, lowered.value->overrides});
    auto expected_styles = resolve_styles({tree.value->get(), &sheet, {}, overrides});
    check(styles && expected_styles && *styles.value == *expected_styles.value && styles.value->at(2).color == Color{1, 0, 0, 1},
          "Inherited text and ordinary override precedence cross invocation boundaries");
    PlaceholderTextShaper text;
    auto layout = compute_layout({tree.value->get(), *styles.value, {200, 120}, &text});
    auto expected_layout = compute_layout({tree.value->get(), *expected_styles.value, {200, 120}, &text});
    check(layout && expected_layout && layout.value->boxes == expected_layout.value->boxes, "Nested geometry matches authored geometry");
    auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    auto expected_paint = build_paint_list({tree.value->get(), *expected_styles.value, &*expected_layout.value, &text});
    auto semantics = capture_semantics({tree.value->get(), *styles.value, &*layout.value}, 1);
    auto expected_semantics = capture_semantics({tree.value->get(), *expected_styles.value, &*expected_layout.value}, 1);
    check(paint && expected_paint && *paint.value == *expected_paint.value && semantics && expected_semantics && *semantics.value == *expected_semantics.value,
          "Full-tree paint and semantics match the authored presentation");
    const HitTestInput hit{tree.value->get(), *styles.value, &*layout.value};
    const auto target = *(*tree.value)->find("caption");
    check(hit_test(hit, {10, 10}).value->target == target, "Nested hit testing uses the flattened geometry");
    PointerDispatcher pointer;
    check(bool(pointer.dispatch(hit, {1us, PointerDown{{1}, {10, 10}}})), "Nested pointer press dispatches");
    auto release = pointer.dispatch(hit, {2us, PointerUp{{1}, {10, 10}}});
    FocusDispatcher focus; check(bool(focus.focus(hit, target)), "Nested child is focusable");
    auto activate = focus.dispatch(hit, {3us, Activate{}});
    auto semantic = request_semantic_action({hit.tree, hit.styles, hit.layout}, target, "activate");
    check(release && activate && semantic && release.value->actions.size() == 1 && activate.value->actions.size() == 1 &&
          release.value->actions[0] == activate.value->actions[0] && release.value->actions[0] == *semantic.value,
          "Nested events reach the same pointer, logical keyboard/gamepad and semantic action boundary");
    const auto encoded = save_document(lowered.value->document, context);
    check(encoded && load_document(*encoded.value, context).value == std::optional<UiDocument>{authored}, "Invocation output uses ordinary document serialization");
    check(r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notifications == 1, "Only explicit publication captures effects");
    r.batch([&] { label.write("Rope"); label.write("Coiled rope"); }); r.flush();
    auto updated = lower_component_tree(r, v, 2, context);
    check(updated && std::get<std::string>(updated.value->document.root.children[0].children[0].properties.at("text")) == "Coiled rope" &&
          lowered.value->document == authored && evaluations == 2, "A later tree call refreshes weak arguments and leaves prior candidates owned");
    v[0].component.document.root.id = "outer"; v[0].component.properties[0].target = "outer";
    v[0].component.events[0].target = "outer"; v[0].component.style_bindings[0].target = "outer";
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "duplicate_id", "/invocations/2/root/children/0/children/0/id") &&
          r.deliver_effects(EffectPhase::notify).empty() && notifications == 1, "Final ID collision rejects the candidate without capturing effects");
}

void topology_and_locations() {
    ReactiveRuntime r; r.begin_batch();
    auto v = fixture();
    check(has(lower_component_tree(r, {}, 0).diagnostics, "component_invocation_limit", "/invocations"), "Empty trees reject");
    check(has(lower_component_tree(r, v, 9, context).diagnostics, "component_invocation_index", "/root"), "Root indices validate");
    v[1].invocation_slots["body"] = {9};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_invocation_index", "/invocations/1/slots/body/0"), "Child indices validate before graph entry");
    v = fixture(); v[1].invocation_slots["body"] = {2};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_invocation_cycle", "/invocations/1/slots/body/0"), "Cycles reject");
    v = fixture(); v[2].invocation_slots["body"] = {1, 1};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_invocation_reuse", "/invocations/2/slots/body/1"), "Shared instances reject");
    v = fixture(); v[1].invocation_slots["body"] = {};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_invocation_unreachable", "/invocations/0"), "Unconnected records reject");
    v = fixture(); v[1].arguments.slots["body"] = {};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_duplicate_slot_argument", "/invocations/1/slots/body"), "Empty raw and invocation writers conflict");
    v[1].arguments.slots.clear(); v[1].arguments.composed_slots["body"] = {};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_duplicate_slot_argument", "/invocations/1/slots/body"), "Empty composed and invocation writers conflict");
    v = fixture(); v[1].invocation_slots["unknown/~"] = {};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_unknown_slot", "/invocations/1/slots/unknown~1~0"), "Slot names are exact and escaped");
    v = fixture(); v[1].component.slots[0].accepted = NodeKinds::box;
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_slot_kind", "/invocations/1/slots/body/0/type"), "Nested root kinds validate");
    v = fixture(); v[1].component.slots[0].minimum = 2;
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_slot_count", "/invocations/1/slots/body"), "Nested cardinality validates");
    v = fixture();
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "reactive_open_batch", "/reactive"), "Whole-graph failures retain graph locations");
    r.end_batch();
    v[0].arguments.props.clear();
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_missing_prop", "/invocations/0/props/label"), "Component schema diagnostics identify the invocation");
    v = fixture(); v[0].arguments.actions["select"] = std::string("missing");
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "unknown_action", "/invocations/0/actions/select"), "Nested host actions use ordinary registration");
    v = fixture(); v[0].component.document.root.properties["labelled_by"] = NodeReference{"outer"};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "invalid_reference", "/invocations/0/component/document/root/properties/labelled_by/ref"),
          "Each invocation template obeys ordinary standalone validation");
}

void geometry_bounds_and_mixed_slots() {
    ReactiveRuntime r;
    std::vector<ComponentInvocation> v{box("outer"), box("inner")};
    v[0].invocation_slots["body"] = {1};
    // Two template roots plus 9998 raw nodes make the exact final node bound.
    v[1].arguments.slots["body"].resize(max_document_nodes - 2);
    auto exact = lower_component_tree(r, v);
    check(exact && exact.value->document.root.children[0].children.size() == max_document_nodes - 2, "Aggregate node boundary is inclusive");
    r.begin_batch();
    v[1].arguments.slots["body"].push_back({});
    check(has(lower_component_tree(r, v).diagnostics, "component_document_limit", "/invocations/0/component/document/root"), "Aggregate bound rejects before graph entry or copying");
    v = {box("outer"), box("inner")}; v[0].invocation_slots["body"] = {1};
    auto* node = &v[1].component.document.root;
    v[1].component.slots.clear();
    for (std::size_t i = 1; i < max_document_depth; ++i) { node->children.resize(1); node = &node->children[0]; }
    check(has(lower_component_tree(r, v).diagnostics, "component_document_limit", "/invocations/0/component/document/root"), "Projected depth covers template and invocation levels");
    node = &v[1].component.document.root;
    for (std::size_t i = 1; i < max_document_depth - 1; ++i) node = &node->children[0];
    node->children.clear(); r.end_batch();
    check(bool(lower_component_tree(r, v)), "Exact composed depth limit succeeds");
    v.resize(max_component_invocations + 1);
    check(has(lower_component_tree(r, v).diagnostics, "component_invocation_limit", "/invocations"), "Invocation array has its own bound");
    v = fixture();
    UiNode raw; raw.kind = NodeKind::text; raw.properties["text"] = std::string("Footer");
    UiNode raw_target; raw_target.id = "raw-slot";
    UiNode composed_target; composed_target.id = "composed-slot";
    v[2].component.document.root.children = {raw_target, composed_target};
    v[2].component.slots[0].target = "raw-slot";
    v[2].component.slots.push_back({"footer", "composed-slot", NodeKinds::text});
    BoundComponent child; child.document.root = raw; child.overrides.resize(1); child.overrides[0].opacity = 0.5f;
    v[2].arguments.composed_slots["footer"] = {child};
    v[1].component.document.root.children = {raw_target}; v[1].component.document.root.children[0].id = "middle-slot";
    v[1].component.slots[0].target = "middle-slot";
    // Raw children and invocation children occupy distinct slots on this node.
    UiNode footer_target; footer_target.id = "middle-footer"; v[1].component.document.root.children.push_back(footer_target);
    v[1].component.slots.push_back({"footer", "middle-footer", NodeKinds::text}); v[1].arguments.slots["footer"] = {raw};
    auto mixed = lower_component_tree(r, v, 2, context);
    check(mixed && mixed.value->overrides.size() == 9 && mixed.value->overrides[8].opacity == 0.5f &&
          mixed.value->overrides[6] == StyleDeclarations{}, "Raw, composed and invocation slots preserve anonymous sibling alignment");
    for (std::size_t i = 0; i < max_component_slots; ++i) v[1].arguments.slots[std::to_string(i)] = {};
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "component_limit", "/invocations/1/component"), "All three slot maps share a bound");
}

void weak_lifetimes_and_faults() {
    ReactiveRuntime r; auto owner = r.owner(r.root(), "caption");
    auto label = r.signal(owner, "label", std::string("Map"));
    auto v = fixture(); v[0].arguments.props["label"] = label;
    auto accepted = lower_component_tree(r, v, 2, context); check(bool(accepted), "Live nested references lower");
    owner.dispose();
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "disposed_reactive_value", "/invocations/0/props/label") &&
          accepted.value->document.root.children[0].children[0].id == "caption", "Disposal rejects without altering accepted owned candidates");
    ReactiveRuntime foreign; v[0].arguments.props["label"] = foreign.signal(foreign.root(), "label", std::string("Map"));
    check(has(lower_component_tree(r, v, 2, context).diagnostics, "foreign_reactive_runtime", "/invocations/0/props/label"), "Foreign nested references reject");
    auto boundary = r.boundary(r.root(), "fault");
    auto failed = r.computed<std::string>(boundary, "label", []() -> std::string { throw std::runtime_error("missing item"); });
    v[0].arguments.props["label"] = failed; check(r.flush().size() == 1, "Fault is contained by the ordinary owner boundary");
    const auto rejected = lower_component_tree(r, v, 2, context);
    check(has(rejected.diagnostics, "reactive_faulted_snapshot", "/invocations/0/props/label") ||
          has(rejected.diagnostics, "reactive_unsettled", "/invocations/0/props/label"), "Faulted child rejects the entire candidate");
    v[0].arguments.props["label"] = Property{std::string("Unavailable")};
    check(bool(lower_component_tree(r, v, 2, context)), "Host can supply an explicit fallback description argument");
}
} // namespace
int main() {
    try {
        presentation_and_updates(); topology_and_locations(); geometry_bounds_and_mixed_slots(); weak_lifetimes_and_faults();
        std::cout << "Component invocation checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
