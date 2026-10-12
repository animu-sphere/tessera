#include <tessera/ui/component.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
const Diagnostic& find(const std::vector<Diagnostic>& diagnostics, std::string_view code, std::string_view path) {
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.code == code && diagnostic.path == path) return diagnostic;
    throw std::runtime_error("Missing expected diagnostic: " + std::string(code) + " at " + std::string(path));
}
void parsed_rejections() {
    const std::string invalid = R"({"version":1,"root":{"type":"Text","properties":{"text":"日本語","focusable":"yes"}}})";
    auto plain = load_document(invalid);
    auto sourced = load_document_with_sources(invalid, "fixture:日本語.json");
    check(!plain && !sourced, "Invalid semantic input returns no candidate");
    const auto& diagnostic = find(sourced.diagnostics, "property_type", "/root/properties/focusable");
    const auto begin = invalid.find("\"yes\"");
    check(diagnostic.source == std::optional<DiagnosticSource>{{{"fixture:日本語.json", {begin, begin + 5}}, DiagnosticOrigin::value}},
          "Semantic error identifies the exact UTF-8 value bytes after multibyte text");
    for (auto& error : sourced.diagnostics) error.source.reset();
    check(sourced.diagnostics == plain.diagnostics, "Source enrichment preserves every existing diagnostic field and order");

    const std::string missing = R"({"version":1,"root":{}})";
    auto absent = load_document_with_sources(missing, "missing.json");
    const auto& required = find(absent.diagnostics, "missing_field", "/root/type");
    check(required.source && required.source->origin == DiagnosticOrigin::value &&
          missing.substr(required.source->location.span.begin, required.source->location.span.end - required.source->location.span.begin) == "{}",
          "Missing field uses its parsed containing value without inventing a field span");
    const std::string syntax = R"({"version":1,"root":)";
    auto broken = load_document_with_sources(syntax, "syntax.json");
    check(!broken && broken.diagnostics[0].source && broken.diagnostics[0].source->origin == DiagnosticOrigin::point &&
          broken.diagnostics[0].source->location.span == SourceSpan{syntax.size(), syntax.size()},
          "Truncated syntax has an explicit zero-width failure point at EOF");
    auto utf8 = load_document_with_sources(std::string(1, char(0xff)), "bytes.json");
    check(!utf8 && utf8.diagnostics[0].source->origin == DiagnosticOrigin::point, "Invalid UTF-8 preserves the loader's failure point");
    auto bad_file = load_document_with_sources(syntax, std::string(max_source_file_bytes + 1, 'x'));
    check(!bad_file && has(bad_file.diagnostics, "source_map_file", "/sources/file") && !bad_file.diagnostics[0].source,
          "Invalid file metadata rejects before parsing and is never copied as a trusted origin");
}

void document_locator() {
    auto loaded = load_document_with_sources(R"({"version":1,"root":{"type":"Box","children":[{"type":"Text","properties":{"text":"x"}}]}})", "template.json");
    check(bool(loaded), "Locator fixture loads");
    auto& map = loaded.value->sources;
    auto& node = map.nodes[1];
    node.file = "child.json";
    node.invocation = SourceLocation{"call.cpp", {100, 110}};
    std::vector<Diagnostic> diagnostics{
        {"property_type", Severity::error, "/root/children/0/properties/text", "Invalid text", {}},
        {"duplicate_id", Severity::error, "/root/children/0/id", "Duplicate", {}},
        {"unknown", Severity::warning, "/root/children/10/id", "Missing child", {}},
        {"unsupported_version", Severity::error, "/version", "Version", {}}};
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty(), "Locator accepts aligned metadata");
    check(diagnostics[0].source->location.file == "child.json" && diagnostics[0].source->origin == DiagnosticOrigin::value &&
          diagnostics[1].source->origin == DiagnosticOrigin::node && !diagnostics[2].source && !diagnostics[3].source,
          "Exact property and node ranges use the child file; missing children and document fields stay unavailable");
    node.properties.erase("text"); node.unmapped_properties.insert("text");
    node.property_arguments["text"] = {"value.cpp", {3, 12}};
    node.event_arguments["activate"] = {"action.cpp", {2, 7}};
    node.style_argument = SourceLocation{"style.cpp", {0, 20}};
    loaded.value->document.root.children[0].events["activate"] = "go";
    diagnostics.push_back({"unknown_action", Severity::error, "/root/children/0/events/activate", "Action", {}});
    diagnostics.push_back({"invalid_style", Severity::error, "/root/children/0/style/width", "Width", {}});
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty() &&
          diagnostics[0].source->location.file == "value.cpp" && diagnostics[0].source->origin == DiagnosticOrigin::argument &&
          diagnostics[4].source->location.file == "action.cpp" && diagnostics[5].source->location.file == "style.cpp",
          "Independent expression origins take priority for properties, events and complete styles");
    node.property_arguments.clear();
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty() && diagnostics[0].source->origin == DiagnosticOrigin::invocation,
          "Rebound properties cannot inherit a placeholder value's origin");
    node.invocation.reset();
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty() && diagnostics[0].source->origin == DiagnosticOrigin::node,
          "Node fallback is explicitly distinguished from an argument value");
    node.mapped = false; node.file.reset(); node.span = {};
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty() && !diagnostics[0].source,
          "Generated nodes with no relevant provenance clear obsolete diagnostic origins");

    const auto accepted = diagnostics;
    node.pointer = "/wrong";
    check(has(locate_diagnostics(loaded.value->document, map, diagnostics), "source_map_mismatch", "/sources/nodes/1") && diagnostics == accepted,
          "Malformed metadata leaves the entire diagnostic array unchanged");
    node.pointer = "/root/children/0";
    diagnostics.resize(max_source_diagnostics);
    check(locate_diagnostics(loaded.value->document, map, diagnostics).empty(), "Exact diagnostic count bound is accepted");
    diagnostics.push_back({});
    check(has(locate_diagnostics(loaded.value->document, map, diagnostics), "diagnostic_limit", "/diagnostics"), "Excess diagnostic count is bounded");
    diagnostics.resize(1); diagnostics[0].path.assign(max_serialized_bytes + 1, '/');
    check(has(locate_diagnostics(loaded.value->document, map, diagnostics), "diagnostic_limit", "/diagnostics"), "Shared path bytes are bounded before matching");

    UiDocument escaped; escaped.root.properties["x/y~z"] = true;
    DocumentSourceMap escaped_map{"escape.json", {{"/root", {0, 30}, {{"x/y~z", {10, 20}}}}}};
    std::vector<Diagnostic> escaped_errors{{"unknown_property", Severity::error, "/root/properties/x~1y~0z", "Unknown", {}}};
    check(locate_diagnostics(escaped, escaped_map, escaped_errors).empty() && escaped_errors[0].source->location.span == SourceSpan{10, 20},
          "Escaped JSON-pointer tokens retain exact property provenance on an invalid document");
}

ComponentDescription component_fixture() {
    auto loaded = load_document_with_sources(R"({"version":1,"root":{"type":"Text","id":"caption","properties":{"text":"x","labelled_by":{"ref":"caption"}}}})", "caption.json");
    check(bool(loaded), "Component fixture loads");
    ComponentDescription component;
    component.kind = "caption"; component.document = loaded.value->document; component.sources = loaded.value->sources;
    component.props = {{"caption", PropertyType::string, {}}, {"label", PropertyType::reference, Property{NodeReference{"caption"}}}};
    component.properties = {{"caption", "caption", "text"}, {"label", "caption", "labelled_by"}};
    component.actions = {{"activate", {}}}; component.events = {{"activate", "caption", "activate"}};
    component.styles = {{"appearance", StyleDeclarations{}}}; component.style_bindings = {{"appearance", "caption"}};
    return component;
}
void component_rejections() {
    ReactiveRuntime runtime;
    auto component = component_fixture();
    ComponentArguments arguments;
    arguments.props["caption"] = Property{std::string("Accepted")}; arguments.actions["activate"] = std::string("go");
    const SourceLocation call{"call.cpp", {0, 30}}, caption{"caption.cpp", {10, 20}}, action{"action.cpp", {2, 8}}, style{"style.cpp", {40, 50}};
    arguments.sources = ComponentArgumentSources{call, {{"caption", caption}}, {{"activate", action}}, {}};
    ValidationContext context{{"go"}};
    auto accepted = lower_component(runtime, component, arguments, context);
    check(bool(accepted), "Accepted component candidate lowers");
    const auto prior = *save_document(accepted.value->document, context).value;
    arguments.props["caption"] = Property{true};
    auto wrong = lower_component(runtime, component, arguments, context);
    check(!wrong && find(wrong.diagnostics, "component_prop_type", "/props/caption").source ==
          std::optional<DiagnosticSource>{{caption, DiagnosticOrigin::argument}}, "Pre-read type rejection uses the argument expression");
    arguments.props.erase("caption"); arguments.sources->props.erase("caption");
    auto missing = lower_component(runtime, component, arguments, context);
    check(find(missing.diagnostics, "component_missing_prop", "/props/caption").source ==
          std::optional<DiagnosticSource>{{call, DiagnosticOrigin::invocation}}, "Missing required argument uses the invocation, not a default declaration");
    auto owner = runtime.owner(runtime.root(), "row");
    auto signal = runtime.signal(owner, "caption", std::string("x")); owner.dispose();
    arguments.props["caption"] = signal; arguments.sources->props["caption"] = caption;
    auto disposed = lower_component(runtime, component, arguments, context);
    check(!disposed && disposed.diagnostics[0].path == "/props/caption" && disposed.diagnostics[0].source->location == caption,
          "Disposed weak arguments retain the located reactive cause and expression source");
    arguments.props["caption"] = Property{std::string("x")};
    arguments.actions["activate"] = std::string("absent");
    check(find(lower_component(runtime, component, arguments, context).diagnostics, "unknown_action", "/actions/activate").source->location == action,
          "Unknown host actions use their supplied origins");
    arguments.actions["activate"] = std::string("go");
    StyleDeclarations invalid_style; invalid_style.width = Dimension::points(-1);
    arguments.styles["appearance"] = invalid_style; arguments.sources->styles["appearance"] = style;
    auto styling = lower_component(runtime, component, arguments, context);
    check(!styling && styling.diagnostics[0].source && styling.diagnostics[0].source->location == style,
          "A complete style origin covers its field diagnostics");
    arguments.styles.clear(); arguments.sources->styles.clear();
    arguments.props["label"] = Property{NodeReference{"missing"}};
    arguments.sources->props["label"] = SourceLocation{"label.cpp", {1, 10}};
    auto reference = lower_component(runtime, component, arguments, context);
    check(find(reference.diagnostics, "invalid_reference", "/root/properties/labelled_by/ref").source->location.file == "label.cpp",
          "Final composed-document validation locates the instance expression after lowering");
    arguments.props.erase("label"); arguments.sources->props.erase("label");
    component.document.root.properties["text"] = true;
    auto bad_template = lower_component(runtime, component, arguments, context);
    check(find(bad_template.diagnostics, "property_type", "/component/document/root/properties/text").source->location.file == "caption.json",
          "Invalid authored templates use validated template ranges before path relocation");
    check(*save_document(accepted.value->document, context).value == prior, "All rejections preserve the host's accepted owned candidate");
}

void nested_and_composed_origins() {
    ReactiveRuntime runtime;
    auto child = component_fixture();
    ComponentArguments arguments; arguments.props["caption"] = Property{std::string("x")}; arguments.actions["activate"] = std::string("go");
    arguments.sources = ComponentArgumentSources{SourceLocation{"child-call.cpp", {0, 5}}, {}, {}, {}};
    ComponentDescription parent; parent.kind = "parent"; parent.document.root.id = "parent"; parent.slots = {{"body", "parent"}};
    std::vector<ComponentInvocation> tree{{parent, {}, {{"body", {1}}}}, {child, arguments, {}}};
    tree[1].arguments.props["caption"] = Property{false};
    tree[1].arguments.sources->props["caption"] = {"nested.cpp", {3, 8}};
    auto rejected = lower_component_tree(runtime, tree, 0, {{"go"}});
    check(find(rejected.diagnostics, "component_prop_type", "/invocations/1/props/caption").source->location.file == "nested.cpp",
          "Nested rejection retains the child expression file and invocation-local path");
    auto bound = lower_component(runtime, child, arguments, {{"go"}}); check(bool(bound), "Independent child lowers");
    parent.document.root.id = "caption"; parent.slots[0].target = "caption";
    ComponentArguments composed; composed.composed_slots["body"] = {*bound.value};
    composed.sources = ComponentArgumentSources{SourceLocation{"parent.cpp", {20, 25}}, {}, {}, {}};
    auto duplicate = lower_component(runtime, parent, composed, {{"go"}});
    const auto& error = find(duplicate.diagnostics, "duplicate_id", "/root/children/0/id");
    check(error.source && error.source->location.file == "caption.json" && error.source->origin == DiagnosticOrigin::node,
          "Cross-document duplicate identity keeps the child's declaration through composition");
    auto damaged = *bound.value; damaged.document.root.properties["text"] = false;
    composed.composed_slots["body"] = {damaged};
    auto child_error = lower_component(runtime, parent, composed, {{"go"}});
    check(find(child_error.diagnostics, "property_type", "/slots/body/0/properties/text").source->location.file == "child-call.cpp",
          "Pre-copy child validation uses the child's provenance, never the parent invocation");
    composed.composed_slots["body"][0].sources->nodes[0].invocation->span = {5, 0};
    auto untrusted = lower_component(runtime, parent, composed, {{"go"}});
    check(!find(untrusted.diagnostics, "property_type", "/slots/body/0/properties/text").source,
          "Invalid child metadata cannot provide a trusted origin or inherit a parent origin");
}
} // namespace
int main() {
    try {
        parsed_rejections(); document_locator(); component_rejections(); nested_and_composed_origins();
        std::cout << "Source diagnostic checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
