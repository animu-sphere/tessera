#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

static_assert(std::is_constructible_v<ComponentValue, Signal<std::string>>);
static_assert(std::is_constructible_v<ComponentValue, Computed<bool>>);
static_assert(!std::is_constructible_v<ComponentValue, Signal<UiNode>>);

namespace {
ComponentDescription panel() {
    ComponentDescription c;
    c.kind = "tool-panel";
    c.document.root.id = "panel";
    c.document.extensions["host:data"].value = std::string("preserved");
    UiNode label;
    label.kind = NodeKind::text; label.id = "label"; label.properties["text"] = std::string{};
    UiNode content; content.id = "content";
    c.document.root.children = {label, content};
    c.props = {{"title", PropertyType::string, {}}, {"enabled", PropertyType::boolean, Property{true}}};
    c.properties = {{"title", "label", "text"}, {"enabled", "panel", "focusable"}};
    c.slots = {{"body", "content", NodeKinds::text, 1, 4}};
    return c;
}
ComponentArguments arguments() {
    ComponentArguments a;
    a.props["title"] = Property{std::string("Tools")};
    UiNode row;
    row.kind = NodeKind::text; row.id = "row";
    row.classes = {"item"}; row.properties["text"] = std::string("Map");
    row.properties["labelled_by"] = NodeReference{"label"}; // Crosses the slot boundary.
    row.extensions["host:item"].value = std::string("map-id");
    a.slots["body"] = {row};
    return a;
}

void composition_and_reference_parity() {
    ReactiveRuntime r;
    auto title = r.signal(r.root(), "title", std::string("Inventory"));
    int evaluations = 0;
    const auto caption = r.computed<std::string>(r.root(), "caption", [&] { ++evaluations; return title.read() + " tools"; });
    const auto c = panel();
    auto a = arguments(); a.props["title"] = caption;
    check(has(lower_component(r, c, a).diagnostics, "reactive_unsettled", "/reactive/root/values/caption") && evaluations == 0,
          "Lowering cannot demand-evaluate an unsettled prop");
    r.batch([&] { title.write("Old"); title.write("New"); }); r.flush();
    auto lowered = lower_component(r, c, a);
    auto expected = c.document;
    expected.root.properties["focusable"] = true;
    expected.root.children[0].properties["text"] = std::string("New tools");
    expected.root.children[1].children = a.slots.at("body");
    check(lowered && lowered.value->document == expected && evaluations == 1 && c.document == panel().document,
          "Defaults, settled props and ordered slots lower without changing template or calculating again");
    check(lowered.value->stages == (PropertyStages::style | PropertyStages::layout | PropertyStages::paint |
        PropertyStages::input | PropertyStages::semantics), "Inserted slot content conservatively affects every stage");
    auto encoded = save_document(lowered.value->document);
    const auto decoded = encoded ? load_document(*encoded.value) : Result<UiDocument>{};
    check(decoded && *decoded.value == expected, "Ordinary serialization preserves template and slot metadata and references");
    auto actual_tree = UiTree::create(lowered.value->document), reference_tree = UiTree::create(expected);
    check(actual_tree && reference_tree, "Both full-tree presentation paths validate");
    const StyleSheet sheet;
    auto actual_styles = resolve_styles({actual_tree.value->get(), &sheet});
    auto reference_styles = resolve_styles({reference_tree.value->get(), &sheet});
    check(actual_styles && reference_styles && *actual_styles.value == *reference_styles.value, "Styles agree");
    PlaceholderTextShaper text;
    auto actual_layout = compute_layout({actual_tree.value->get(), *actual_styles.value, {240, 120}, &text});
    auto reference_layout = compute_layout({reference_tree.value->get(), *reference_styles.value, {240, 120}, &text});
    check(actual_layout && reference_layout, "Layouts compute");
    for (std::size_t i = 0; i < actual_layout.value->boxes.size(); ++i) {
        auto box = actual_layout.value->boxes[i]; box.node = reference_layout.value->boxes[i].node;
        check(box == reference_layout.value->boxes[i], "Geometry agrees with the authored reference");
    }
    auto ap = build_paint_list({actual_tree.value->get(), *actual_styles.value, &*actual_layout.value, &text});
    auto rp = build_paint_list({reference_tree.value->get(), *reference_styles.value, &*reference_layout.value, &text});
    auto as = capture_semantics({actual_tree.value->get(), *actual_styles.value, &*actual_layout.value}, 1);
    auto rs = capture_semantics({reference_tree.value->get(), *reference_styles.value, &*reference_layout.value}, 1);
    check(ap && rp && *ap.value == *rp.value && as && rs && *as.value == *rs.value, "Paint and semantics agree");
    auto same = c; same.document.root.children[0].properties["text"] = std::string("New tools");
    same.document.root.properties["focusable"] = true; same.slots[0].minimum = 0; a.slots.clear();
    auto unchanged = lower_component(r, same, a);
    check(unchanged && unchanged.value->stages == PropertyStages::none, "Stages compare against the template, with absent optional slots empty");
}

void schema_and_argument_rejection() {
    ReactiveRuntime r;
    const auto c = panel(); auto a = arguments();
    r.begin_batch(); // Schema errors take precedence over runtime entry.
    a.props.erase("title"); a.props["enabled"] = Property{std::string("true")}; a.props["typo/~"] = Property{true};
    a.slots["unknown"] = {};
    auto invalid = lower_component(r, c, a);
    check(!invalid && has(invalid.diagnostics, "component_missing_prop", "/props/title") &&
        has(invalid.diagnostics, "component_prop_type", "/props/enabled") &&
        has(invalid.diagnostics, "component_unknown_prop", "/props/typo~1~0") &&
        has(invalid.diagnostics, "component_unknown_slot", "/slots/unknown"), "Arguments have exact types/names and escaped locations");
    auto bad = c; bad.props.push_back(bad.props[0]); bad.properties.push_back(bad.properties[0]);
    bad.props[1].default_value = Property{std::string("true")}; bad.slots.push_back(bad.slots[0]);
    bad.slots[0].maximum = 0;
    invalid = lower_component(r, bad, arguments());
    check(!invalid && has(invalid.diagnostics, "component_duplicate_prop", "/component/props/2/name") &&
        has(invalid.diagnostics, "component_prop_type", "/component/props/1/default") &&
        has(invalid.diagnostics, "component_duplicate_property", "/component/properties/2/property") &&
        has(invalid.diagnostics, "component_duplicate_slot", "/component/slots/1/name") &&
        has(invalid.diagnostics, "component_slot_bounds", "/component/slots/0"), "Invalid declarations reject before runtime entry");
    bad = c; bad.properties = {{"missing", "absent", "text"}, {"enabled", "label", "text"}};
    bad.slots[0].target = "label";
    invalid = lower_component(r, bad, arguments());
    check(has(invalid.diagnostics, "component_unknown_prop", "/component/properties/0/prop") &&
        has(invalid.diagnostics, "component_target", "/component/properties/0/target") &&
        has(invalid.diagnostics, "component_prop_type", "/component/properties/1/prop") &&
        has(invalid.diagnostics, "component_slot_target", "/component/slots/0/target"), "Mappings use typed template nodes and slots use empty Boxes");
    r.end_batch();
    a = arguments(); a.slots.clear();
    check(has(lower_component(r, c, a).diagnostics, "component_slot_count", "/slots/body"), "Required slot cardinality is enforced");
    a = arguments(); a.slots["body"][0].kind = NodeKind::box;
    check(has(lower_component(r, c, a).diagnostics, "component_slot_kind", "/slots/body/0/type"), "Slot root kinds are constrained");
    a = arguments(); a.slots["body"][0].events["activate"] = "inspect";
    check(has(lower_component(r, c, a).diagnostics, "unknown_action", "/slots/body/0/events/activate"), "Slot actions use host validation");
    check(static_cast<bool>(lower_component(r, c, a, {{"inspect"}})), "Registered actions lower normally");
    a = arguments(); a.slots["body"][0].id = "label";
    check(has(lower_component(r, c, a).diagnostics, "duplicate_id", "/root/children/1/children/0/id"), "Slot IDs share the document namespace");
    a = arguments(); a.slots["body"][0].properties["labelled_by"] = NodeReference{"missing"};
    check(has(lower_component(r, c, a).diagnostics, "invalid_reference", "/root/children/1/children/0/properties/labelled_by/ref"),
          "Cross-slot references resolve only in the complete document");
    bad = c; bad.props.push_back({"budget", PropertyType::number, Property{std::numeric_limits<double>::infinity()}});
    check(has(lower_component(r, bad, arguments()).diagnostics, "invalid_number", "/component/props/2/default"), "Even unused defaults must be finite");
    bad = c; bad.kind = std::string("\xff", 1);
    check(has(lower_component(r, bad, arguments()).diagnostics, "component_name", "/component/kind"), "Component identities require valid UTF-8");
}

void bounds_before_copy() {
    ReactiveRuntime r; auto c = panel(); auto a = arguments();
    c.props.resize(max_component_props + 1);
    check(has(lower_component(r, c, a).diagnostics, "component_limit", "/component"), "Declarations have a fixed bound");
    c = panel(); c.slots[0].accepted = NodeKinds::all;
    auto& deep = a.slots["body"][0]; deep.kind = NodeKind::box;
    deep.properties.clear(); auto* cursor = &deep;
    for (std::size_t i = 1; i < max_document_depth; ++i) { cursor->children.emplace_back(); cursor = &cursor->children.back(); }
    check(has(lower_component(r, c, a).diagnostics, "component_document_limit", "/slots/body/0"),
          "A valid subtree exceeding composed depth rejects before deep copying");
    a = arguments(); a.slots["body"][0].properties["text"] = std::string("\xff", 1);
    check(has(lower_component(r, c, a).diagnostics, "invalid_utf8", "/slots/body/0/properties/text"), "Slot contents validate before copying");
    a = arguments(); c.slots[0].maximum = max_document_nodes; c.slots[0].accepted = NodeKinds::all;
    auto& wide = a.slots["body"][0]; wide = UiNode{}; wide.children.resize(max_document_nodes - 1);
    check(has(lower_component(r, c, a).diagnostics, "component_document_limit", "/slots/body/0"), "Combined node budgets include the template");
}

void ownership_faults_and_publication() {
    ReactiveRuntime r; KeyedCollection rows(r.owner(r.root(), "rows"));
    const auto first = rows.reconcile(r, {{"item", "tool-panel"}});
    check(static_cast<bool>(first), "Keyed component owner created");
    auto title = r.signal(first.value->instances[0].owner, "title", std::string("Accepted"));
    int notified = 0;
    r.effect<std::string>(first.value->instances[0].owner, "notification", EffectPhase::notify, [title] { return title.read(); },
                         [&](const std::string&) { ++notified; });
    r.flush();
    auto a = arguments(); a.props["title"] = title;
    auto accepted = lower_component(r, panel(), a);
    check(accepted && r.deliver_effects(EffectPhase::notify).empty() && notified == 0, "Composition captures no effects");
    check(r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notified == 1, "Host publishes after accepting presentation");
    title.write(std::string("\xff", 1)); r.flush();
    check(has(lower_component(r, panel(), a).diagnostics, "invalid_utf8", "/props/title") &&
          r.deliver_effects(EffectPhase::notify).empty() && notified == 1,
          "Rejected prop content captures no pending notification");
    title.write("Recovered"); r.flush();
    check(lower_component(r, panel(), a) && r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notified == 2,
          "Corrected props can be accepted and published");
    auto reordered = rows.reconcile(r, {{"other", "tool-panel"}, {"item", "tool-panel"}});
    check(reordered && reordered.value->instances[1].lifetime == first.value->instances[0].lifetime &&
          lower_component(r, panel(), a), "Reorder preserves weak prop references and instance lifetime");
    rows.reconcile(r, {{"item", "replacement"}}); r.flush();
    check(has(lower_component(r, panel(), a).diagnostics, "disposed_reactive_value", "/props/title") &&
          accepted.value->document.root.children[0].properties.at("text") == Property{std::string("Accepted")},
          "Kind replacement expires old props without changing the host's accepted candidate");
    ReactiveRuntime foreign; a.props["title"] = foreign.signal(foreign.root(), "title", std::string("Foreign"));
    check(has(lower_component(r, panel(), a).diagnostics, "foreign_reactive_runtime", "/props/title"), "Props cannot borrow another runtime");
    auto boundary = r.boundary(r.root(), "fault");
    a.props["title"] = r.computed<std::string>(boundary, "title", []() -> std::string { throw std::runtime_error("missing record"); });
    check(r.flush().size() == 1, "Subtree failure is contained by its existing owner boundary");
    auto failed = lower_component(r, panel(), a);
    check(has(failed.diagnostics, "reactive_unsettled", "/props/title") ||
          has(failed.diagnostics, "reactive_faulted_snapshot", "/props/title"), "A faulted prop rejects presentation without retry");
    a.props["title"] = Property{std::string("Unavailable")};
    check(static_cast<bool>(lower_component(r, panel(), a)), "Host can explicitly compose fallback props");
    r.begin_batch();
    check(has(lower_component(r, panel(), a).diagnostics, "reactive_open_batch", "/reactive"), "Even literal composition requires a settled update boundary");
    r.end_batch();
}
} // namespace

int main() {
    try {
        composition_and_reference_parity(); schema_and_argument_rejection(); bounds_before_copy(); ownership_faults_and_publication();
        std::cout << "Component composition checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
