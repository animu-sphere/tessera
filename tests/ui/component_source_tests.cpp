#include <tessera/ui/component.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
ComponentDescription sourced(std::string_view json, std::string file, std::string kind) {
    auto loaded = load_document_with_sources(json, std::move(file));
    check(bool(loaded), "Source fixture loads");
    ComponentDescription component;
    component.kind = std::move(kind);
    component.document = std::move(loaded.value->document);
    component.sources = std::move(loaded.value->sources);
    return component;
}
ComponentDescription container(std::string id) {
    ComponentDescription component;
    component.kind = "container"; component.document.root.id = id;
    component.slots = {{"body", id, NodeKinds::all}};
    return component;
}
constexpr std::string_view leaf_json = R"({"version":1,"root":{"type":"Text","id":"caption","properties":{"text":"Placeholder","disabled":false}}})";

void nested_presentation_and_provenance() {
    ReactiveRuntime runtime;
    auto label = runtime.signal(runtime.root(), "label", std::string("Placeholder"));
    int evaluations = 0, notifications = 0;
    auto caption = runtime.computed<std::string>(runtime.root(), "caption", [&] { ++evaluations; return label.read(); });
    runtime.effect<std::string>(runtime.root(), "notify", EffectPhase::notify, [caption] { return caption.read(); },
                               [&](const std::string&) { ++notifications; });
    ComponentInvocation leaf;
    leaf.component = sourced(leaf_json, "caption.json", "caption");
    leaf.component.props = {{"caption", PropertyType::string, {}}, {"focus", PropertyType::boolean, Property{true}}};
    leaf.component.properties = {{"caption", "caption", "text"}, {"focus", "caption", "focusable"}};
    leaf.arguments.props["caption"] = caption;
    ComponentInvocation root;
    root.component = sourced(R"({"version":1,"root":{"type":"Box","id":"panel","children":[{"type":"Box","id":"body"},{"type":"Box","id":"raw"}]}})",
                             "panel.json", "panel");
    root.component.slots = {{"body", "body", NodeKinds::text}, {"footer", "raw", NodeKinds::text}};
    root.invocation_slots["body"] = {0};
    UiNode raw; raw.kind = NodeKind::text; raw.properties["text"] = std::string("Footer");
    root.arguments.slots["footer"] = {raw};
    std::vector<ComponentInvocation> invocations{leaf, root};
    check(has(lower_component_tree(runtime, invocations, 1).diagnostics, "reactive_unsettled", "/reactive/root/values/caption") && evaluations == 0,
          "Sources cannot demand reactive evaluation");
    runtime.flush();
    auto result = lower_component_tree(runtime, invocations, 1);
    check(result && result.value->sources && evaluations == 1 && notifications == 0, "Lowering owns maps without publication");
    const auto& map = *result.value->sources;
    check(map.file == "panel.json" && map.nodes.size() == 5 && validate_sources(result.value->document, map).empty(),
          "Composed map matches final preorder and property shape");
    const auto& source = map.nodes[2];
    check(source.file == "caption.json" && source.pointer == "/root/children/0/children/0" &&
          source.span == leaf.component.sources->nodes[0].span && source.properties.size() == 1 &&
          source.properties.at("disabled") == leaf.component.sources->nodes[0].properties.at("disabled") &&
          source.unmapped_properties == std::set<std::string, std::less<>>{"focusable", "text"},
          "Template node and untouched property origins survive; equal and inserted instance properties are explicitly unmapped");
    check(!map.nodes[4].mapped && map.nodes[4].pointer == "/root/children/1/children/0" &&
          map.nodes[4].unmapped_properties.contains("text") && !map.nodes[4].file && map.nodes[4].span == SourceSpan{},
          "Anonymous raw children have no invented location");

    auto unsourced = invocations;
    for (auto& invocation : unsourced) invocation.component.sources.reset();
    auto reference = lower_component_tree(runtime, unsourced, 1);
    check(reference && !reference.value->sources && reference.value->document == result.value->document &&
          reference.value->stages == result.value->stages && reference.value->overrides == result.value->overrides &&
          save_document(reference.value->document).value == save_document(result.value->document).value,
          "Source metadata changes neither document encoding nor stages or styles");
    auto tree = UiTree::create(result.value->document); check(bool(tree), "Candidate creates an ordinary tree");
    std::vector<ResolvedStyle> styles((*tree.value)->size());
    PlaceholderTextShaper text;
    auto layout = compute_layout({tree.value->get(), styles, {240, 120}, &text}); check(bool(layout), "Full-tree layout succeeds");
    auto snapshot = capture_inspection({tree.value->get(), styles, &*layout.value, &map});
    auto expected = capture_inspection({tree.value->get(), styles, &*layout.value});
    check(snapshot && expected && snapshot.value->semantics == expected.value->semantics &&
          snapshot.value->elements[2].geometry == expected.value->elements[2].geometry &&
          snapshot.value->elements[2].source == std::optional<NodeSource>{source} && !snapshot.value->elements[4].source,
          "Inspection captures resolved per-node files and explicit missing origins without changing presentation");
    std::vector<UiInvariant> assertions{{"caption", SemanticNameInvariant{AuthorIdTarget{"caption"}, "Placeholder"}}};
    auto invariants = evaluate_invariants({&*snapshot.value, assertions, Size{240, 120}});
    check(invariants && invariants.value->passed() && invariants.value->results[0].observations[0].source->file == "caption.json",
          "Invariant observations retain the actual component file");
    runtime.batch([&] { label.write("Updated"); }); runtime.flush();
    auto updated = lower_component_tree(runtime, invocations, 1);
    check(updated && updated.value->sources == result.value->sources && snapshot.value->elements[2].source == source &&
          result.value->document != updated.value->document && notifications == 0, "Later values cannot alter accepted owned source observations");
}

void repeated_composition_and_validation_order() {
    ReactiveRuntime runtime;
    auto child_component = sourced(leaf_json, "child.json", "caption");
    auto child = lower_component(runtime, child_component, {}); check(bool(child), "Child lowers independently");
    auto middle = container("middle");
    ComponentArguments arguments; arguments.composed_slots["body"] = {*child.value};
    auto composed = lower_component(runtime, middle, arguments); check(bool(composed), "Unsourced parent accepts sourced child");
    check(composed.value->sources && composed.value->sources->file.empty() && !composed.value->sources->nodes[0].mapped &&
          composed.value->sources->nodes[1].file == "child.json", "Only the child supplies an origin");
    auto outer = sourced(R"({"version":1,"root":{"type":"Box","id":"outer"}})", "outer.json", "outer");
    outer.slots = {{"body", "outer"}};
    arguments.composed_slots["body"] = {*composed.value};
    auto repeated = lower_component(runtime, outer, arguments);
    check(repeated && repeated.value->sources->nodes[2].file == "child.json" &&
          repeated.value->sources->nodes[2].pointer == "/root/children/0/children/0" &&
          !repeated.value->sources->nodes[1].mapped && validate_sources(repeated.value->document, *repeated.value->sources).empty(),
          "Repeated composition rebases pointers and cannot relabel child files as the outer file");
    // An absent file override and an explicitly empty authored identity differ.
    arguments.composed_slots["body"][0].sources->nodes[1].file = std::string{};
    auto anonymous_file = lower_component(runtime, outer, arguments);
    check(anonymous_file && anonymous_file.value->sources->nodes[2].file == std::optional<std::string>{""},
          "An unknown child file identity cannot inherit an enclosing authored file");

    runtime.begin_batch();
    arguments.composed_slots["body"][0].sources->nodes[1].pointer = "/wrong";
    check(has(lower_component(runtime, outer, arguments).diagnostics, "source_map_mismatch", "/slots/body/0/sources/nodes/1"),
          "Child map validation precedes parent settled reads or subtree copying");
    arguments.composed_slots.clear(); outer.sources->nodes[0].span = {20, 10};
    check(has(lower_component(runtime, outer, arguments).diagnostics, "source_map_span", "/component/sources/nodes/0/span"),
          "Template map validation precedes graph settlement");
    std::vector<ComponentInvocation> invocations(1); invocations[0].component = outer;
    check(has(lower_component_tree(runtime, invocations).diagnostics, "source_map_span", "/invocations/0/component/sources/nodes/0/span"),
          "Nested map diagnostics identify their invocation");
    runtime.end_batch();
}

void malformed_maps_and_bounds() {
    auto component = sourced(leaf_json, "leaf.json", "caption");
    const auto original = *component.sources;
    auto map = original;
    map.nodes[0].properties.at("text").end = map.nodes[0].span.end + 1;
    check(has(validate_sources(component.document, map), "source_map_span", "/sources/nodes/0/properties/text"), "Property ranges stay within their node");
    map = original; map.nodes[0].unmapped_properties.insert("text");
    check(has(validate_sources(component.document, map), "source_map_mismatch", "/sources/nodes/0"), "A property cannot be both mapped and unmapped");
    map = original; map.nodes[0].properties.erase("text");
    check(has(validate_sources(component.document, map), "source_map_mismatch", "/sources/nodes/0"), "Missing property origins require explicit absence");
    map.nodes[0].unmapped_properties.insert("text");
    check(validate_sources(component.document, map).empty(), "Partial property sources have explicit unavailable names");
    map = original; map.nodes[0].mapped = false;
    check(has(validate_sources(component.document, map), "source_map_span", "/sources/nodes/0"), "Unmapped nodes cannot carry invented spans");
    map = original; map.nodes[0].file = std::string(1, static_cast<char>(0xff));
    check(has(validate_sources(component.document, map), "source_map_file", "/sources/nodes/0/file"), "Node file overrides are UTF-8");
    map = original; map.file.resize(max_source_file_bytes, 'x');
    check(validate_sources(component.document, map).empty(), "File byte bound is inclusive");
    map.file.push_back('x');
    check(has(validate_sources(component.document, map), "source_map_file", "/sources/file") &&
          has(load_document_with_sources(leaf_json, map.file).diagnostics, "source_map_file", "/sources/file"), "Loaders and consumers reject oversized identities");
    map = original; map.nodes.resize(max_document_nodes + 1);
    check(has(validate_sources(component.document, map), "source_map_mismatch", "/sources/nodes"), "Node records share document bounds");

    UiDocument wide;
    wide.root.children.resize(max_document_nodes - 1);
    DocumentSourceMap exact; exact.nodes.resize(max_document_nodes);
    for (std::size_t i = 0; i < exact.nodes.size(); ++i) {
        exact.nodes[i].mapped = false;
        exact.nodes[i].pointer = i == 0 ? "/root" : "/root/children/" + std::to_string(i - 1);
    }
    check(validate_sources(wide, exact).empty(), "Exact document node bound has aligned explicit unmapped records");
    UiDocument deep; auto* node = &deep.root;
    DocumentSourceMap deep_map;
    std::string pointer = "/root";
    for (std::size_t i = 0; i < max_document_depth; ++i) {
        NodeSource source; source.pointer = pointer; source.mapped = false; deep_map.nodes.push_back(source);
        if (i + 1 < max_document_depth) { node->children.resize(1); node = &node->children[0]; pointer += "/children/0"; }
    }
    check(validate_sources(deep, deep_map).empty(), "Exact depth bound validates");
    node->children.resize(1); NodeSource extra; extra.pointer = pointer + "/children/0"; extra.mapped = false; deep_map.nodes.push_back(extra);
    check(has(validate_sources(deep, deep_map), "source_map_mismatch", "/sources/nodes"), "Excessive source traversal depth rejects without copying");
}

void warning_availability_and_inspection_rejection() {
    ReactiveRuntime runtime;
    auto component = sourced(R"({"version":1,"root":{"type":"Box","id":"panel"}})", "panel.json", "panel");
    component.document.root.events["activate"] = "go";
    component.slots = {{"body", "panel"}};
    ComponentArguments arguments;
    UiNode raw; raw.events["activate"] = "go"; arguments.slots["body"] = {raw};
    const ValidationContext actions{{"go"}};
    auto bound = lower_component(runtime, component, arguments, actions); check(bool(bound), "Partial warning fixture lowers");
    auto tree = UiTree::create(bound.value->document, actions); check(bool(tree), "Warning fixture creates a tree");
    std::vector<ResolvedStyle> styles((*tree.value)->size()); PlaceholderTextShaper text;
    auto layout = compute_layout({tree.value->get(), styles, {200, 100}, &text}); check(bool(layout), "Warning fixture lays out");
    auto map = *bound.value->sources;
    auto snapshot = capture_inspection({tree.value->get(), styles, &*layout.value, &map});
    check(snapshot && snapshot.value->diagnostics.size() == 2 && snapshot.value->diagnostics[0].byte_offset == map.nodes[0].span.begin &&
          !snapshot.value->diagnostics[1].byte_offset && !snapshot.value->elements[1].source,
          "Only mapped warning nodes acquire source offsets");
    map.nodes[1].pointer = "/wrong";
    check(has(capture_inspection({tree.value->get(), styles, &*layout.value, &map}).diagnostics,
              "source_map_mismatch", "/sources/nodes/1"), "Inspection rejects malformed composed map pointers");
}
} // namespace

int main() {
    try {
        nested_presentation_and_provenance(); repeated_composition_and_validation_order(); malformed_maps_and_bounds();
        warning_availability_and_inspection_rejection();
        std::cout << "Component source checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
