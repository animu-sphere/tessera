#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
bool has(const std::vector<tessera::Diagnostic>& diagnostics, std::string_view code, std::string_view path) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code && diagnostic.path == path && !diagnostic.message.empty() &&
            diagnostic.severity == tessera::Severity::error) return true;
    }
    return false;
}
void rejected(std::string_view source, std::string_view code, std::string_view path) {
    const auto result = tessera::load_document(source);
    check(!result, "Invalid source returned a usable document");
    check(has(result.diagnostics, code, path), std::string("Missing expected diagnostic: ") + std::string(code));
    check(result.diagnostics.front().byte_offset.has_value(), "Parsed diagnostic has no source offset");
}

void round_trip_and_handles() {
    std::ifstream file(std::string(TESSERA_FIXTURE_DIR) + "/menu.json", std::ios::binary);
    check(file.good(), "Fixture missing");
    const std::string canonical{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const tessera::ValidationContext context{{"start_game", "back"}};
    auto loaded = tessera::load_document(canonical, context);
    check(static_cast<bool>(loaded), "Fixture failed validation");
    auto saved = tessera::save_document(*loaded.value, context);
    check(static_cast<bool>(saved) && *saved.value == canonical, "Canonical fixture formatting changed");
    const auto restored = tessera::load_document(*saved.value, context);
    check(static_cast<bool>(restored) && *restored.value == *loaded.value, "Semantic round trip lost data");
    check(tessera::save_document(*restored.value, context).value == saved.value, "Repeated serialization changed");

    // Object ordering, whitespace, and omitted defaults normalize to one representation.
    const auto reordered = tessera::load_document(R"({"version":1,"root":{"type":"Box","properties":{},"events":{},"children":[],"classes":[],"extensions":{}}})");
    check(static_cast<bool>(reordered), "Explicit defaults rejected");
    check(tessera::save_document(*reordered.value).value == tessera::save_document(tessera::UiDocument{}).value,
          "Equivalent empty documents differ");

    auto first = tessera::UiTree::create(*loaded.value, context);
    auto second = tessera::UiTree::create(*loaded.value, context);
    check(static_cast<bool>(first) && static_cast<bool>(second), "Tree creation failed");
    auto& tree = **first.value;
    const auto root = tree.root();
    check(tree.size() == 3, "Tree size differs");
    const auto children = tree.children(root);
    check(children.size() == 2 && tree.get(children[0])->id == "title" && tree.get(children[1])->id == "start",
          "Child order lost");
    check(tree.find("start") == children[1] && !tree.find("missing"), "Author-ID lookup differs");
    check(tree.get({root.tree, 999}) == nullptr && tree.children({}).empty(), "Invalid handle accepted");
    check((*second.value)->get(root) == nullptr, "Foreign tree handle accepted");
    first.value->reset();
    auto replacement = tessera::UiTree::create(*loaded.value, context);
    check((*replacement.value)->get(root) == nullptr, "Expired tree handle reused");
    loaded.value->root.id = "changed";
    check((*replacement.value)->document().root.id == "menu", "Snapshot aliases mutable authoring data");
}

void semantic_errors() {
    tessera::UiDocument invalid;
    invalid.version = 2;
    invalid.root.id = "duplicate";
    invalid.root.properties["focusable"] = std::string("true");
    invalid.root.properties["labelled_by"] = tessera::NodeReference{"absent"};
    invalid.root.events["activate"] = "missing_action";
    tessera::UiNode child;
    child.id = "duplicate";
    child.kind = tessera::NodeKind::text;
    invalid.root.children.push_back(child);
    const auto errors = tessera::validate(invalid);
    check(has(errors, "unsupported_version", "/version"), "Version error missing");
    check(has(errors, "property_type", "/root/properties/focusable"), "Property error missing");
    check(has(errors, "duplicate_id", "/root/children/0/id"), "Duplicate ID error missing");
    check(has(errors, "invalid_reference", "/root/properties/labelled_by/ref"), "Reference error missing");
    check(has(errors, "unknown_action", "/root/events/activate"), "Action error missing");
    check(has(errors, "missing_property", "/root/children/0/properties/text"), "Text property error missing");
    check(!tessera::save_document(invalid) && !tessera::UiTree::create(invalid), "Invalid authoring data accepted");

    rejected(R"({"version":1,"root":{"type":"Box","properties":{"focusable":0}}})", "property_type", "/root/properties/focusable");
    const auto type_error = tessera::load_document(R"({"version":1,"root":{"type":"Box","properties":{"focusable":0}}})");
    check(type_error.diagnostics.front().byte_offset == 60, "Property source offset differs");
    rejected(R"({"version":1,"root":{"type":"Box","properties":{"text":"x"}}})", "unknown_property", "/root/properties/text");
    rejected(R"({"version":1,"root":{"type":"Text","properties":{"text":"x"},"children":[{"type":"Box"}]}})", "invalid_children", "/root/children");
    rejected(R"({"version":1,"root":{"type":"Box","properties":{"labelled_by":{"ref":"absent"}}}})", "invalid_reference", "/root/properties/labelled_by/ref");
    rejected(R"({"version":1,"root":{"type":"Box","children":[{"type":"Box","id":"x"},{"type":"Box","id":"x"}]}})", "duplicate_id", "/root/children/1/id");
    rejected(R"({"version":1,"root":{"type":"Box","events":{"click":"x"}}})", "unknown_event", "/root/events/click");
    rejected(R"({"version":1,"root":{"type":"Box","extensions":{"editor":{}}}})", "invalid_extension", "/root/extensions/editor");
    rejected(R"({"version":1,"root":{"type":"Box","id":""}})", "invalid_identifier", "/root/id");
    rejected(R"({"version":1,"root":{"type":"Box","classes":["x","x"]}})", "duplicate_class", "/root/classes/1");
    rejected(R"({"version":1,"root":{"type":"Box","properties":{"bad/name~":true}}})", "unknown_property", "/root/properties/bad~1name~0");
}

void syntax_and_schema() {
    rejected("", "json_syntax", "");
    rejected("[]", "schema_type", "");
    rejected("{}", "missing_field", "/version");
    rejected(R"({"version":1})", "missing_field", "/root");
    rejected(R"({"version":true,"root":{"type":"Box"}})", "schema_type", "/version");
    rejected(R"({"version":1.5,"root":{"type":"Box"}})", "schema_type", "/version");
    rejected(R"({"version":4294967296,"root":{"type":"Box"}})", "schema_type", "/version");
    rejected(R"({"version":2,"root":{"type":"Box"}})", "unsupported_version", "/version");
    rejected(R"({"version":1,"root":{"type":"Button"}})", "unknown_node_kind", "/root/type");
    rejected(R"({"version":1,"root":{"type":"Box","typo":1}})", "unknown_field", "/root/typo");
    rejected(R"({"version":1,"root":{"type":"Box","children":{}}})", "schema_type", "/root/children");
    rejected(R"({"version":1,"root":{"type":"Box","properties":{"focusable":null}}})", "property_type", "/root/properties/focusable");
    rejected(R"({"version":1,"root":{"type":"Box","properties":{"labelled_by":{"ref":3}}}})", "schema_type", "/root/properties/labelled_by/ref");
    rejected(R"({"version":1,"version":1})", "duplicate_member", "/version");
    rejected(R"({"version":1,"\u0076ersion":1})", "duplicate_member", "/version");
    rejected(R"({"version":01})", "json_syntax", "");
    rejected(R"({"version":1.})", "json_syntax", "/version");
    rejected(R"({"version":1e+})", "json_syntax", "/version");
    rejected(R"({"version":1e999})", "invalid_number", "/version");
    rejected(R"({"version":NaN})", "json_syntax", "/version");
    rejected(R"({"version":1,})", "json_syntax", "");
    rejected(R"({"version":1} trailing)", "json_syntax", "");
    rejected(R"({"version":1,"root":{"type":"Box","id":"\q"}})", "json_syntax", "/root/id");
    rejected(R"({"version":1,"root":{"type":"Box","id":"\ud800"}})", "invalid_unicode", "/root/id");
    rejected(R"({"version":1,"root":{"type":"Box","id":"\udc00"}})", "invalid_unicode", "/root/id");
    rejected(std::string("{\"bad\":\"") + '\xff' + "\"}", "invalid_utf8", "");
    rejected(std::string("{\"bad\":\"") + "\xc0\xaf" + "\"}", "invalid_utf8", "");
    rejected(std::string("{\"bad\":\"") + "\xed\xa0\x80" + "\"}", "invalid_utf8", "");
    rejected(std::string("{\"bad\":\"") + "\xf4\x90\x80\x80" + "\"}", "invalid_utf8", "");
}

void metadata_and_limits() {
    const auto unicode = tessera::load_document(R"({"version":1,"root":{"type":"Text","properties":{"text":"\u65e5\u672c\ud83d\ude00\u0000\n\t\"\\/"}}})");
    check(static_cast<bool>(unicode), "Unicode escapes rejected");
    const auto& text = std::get<std::string>(unicode.value->root.properties.at("text"));
    check(text.substr(0, 10) == "日本😀" && text[10] == '\0', "Unicode decoding differs");
    const auto saved_unicode = tessera::save_document(*unicode.value);
    check(tessera::load_document(*saved_unicode.value).value == unicode.value, "Control/Unicode round trip differs");

    tessera::UiDocument numbers;
    numbers.extensions["test:values"] = tessera::JsonValue{tessera::JsonValue::Array{
        tessera::JsonValue{-0.0}, tessera::JsonValue{0.1},
        tessera::JsonValue{std::numeric_limits<double>::max()},
        tessera::JsonValue{std::numeric_limits<double>::denorm_min()}}};
    auto saved = tessera::save_document(numbers);
    check(static_cast<bool>(saved) && tessera::load_document(*saved.value).value == numbers, "Binary64 values lost precision");
    numbers.extensions["test:bad"] = tessera::JsonValue{std::numeric_limits<double>::infinity()};
    check(!tessera::save_document(numbers), "Non-finite metadata accepted");
    numbers.extensions.erase("test:bad");
    numbers.root.id = std::string("\xff");
    check(!tessera::UiTree::create(numbers), "Invalid builder UTF-8 accepted");

    tessera::UiDocument metadata_depth;
    tessera::JsonValue nested{false};
    // Root-node extension value is JSON depth 3; deepest scalar is depth 256.
    for (std::size_t i = 3; i < tessera::max_json_depth; ++i)
        nested = tessera::JsonValue{tessera::JsonValue::Array{std::move(nested)}};
    metadata_depth.root.extensions["test:deep"] = nested;
    saved = tessera::save_document(metadata_depth);
    check(static_cast<bool>(saved) && static_cast<bool>(tessera::load_document(*saved.value)), "Allowed metadata depth rejected");
    metadata_depth.root.extensions["test:deep"] = tessera::JsonValue{tessera::JsonValue::Array{std::move(nested)}};
    check(!tessera::save_document(metadata_depth), "Excessive metadata depth accepted");

    tessera::UiDocument deep;
    auto* node = &deep.root;
    for (std::size_t i = 1; i < tessera::max_document_depth; ++i) {
        node->children.emplace_back(); node = &node->children.back();
    }
    saved = tessera::save_document(deep);
    check(static_cast<bool>(saved) && static_cast<bool>(tessera::load_document(*saved.value)), "Allowed depth rejected");
    node->children.emplace_back();
    check(!tessera::save_document(deep) && !tessera::UiTree::create(deep), "Excessive node depth accepted");
    std::string deep_source = R"({"version":1,"root":)";
    for (std::size_t i = 0; i < tessera::max_document_depth; ++i) deep_source += R"({"type":"Box","children":[)";
    deep_source += R"({"type":"Box"})";
    for (std::size_t i = 0; i < tessera::max_document_depth; ++i) deep_source += "]}";
    deep_source += '}';
    check(!tessera::load_document(deep_source), "Excessive serialized node depth accepted");
    auto json_depth = tessera::load_document(std::string(300, '[') + "0" + std::string(300, ']'));
    check(!json_depth && json_depth.diagnostics.front().code == "depth_limit", "JSON nesting limit missing");

    tessera::UiDocument wide;
    wide.root.children.resize(tessera::max_document_nodes - 1);
    check(tessera::validate(wide).empty(), "Allowed node count rejected");
    wide.root.children.emplace_back();
    check(has(tessera::validate(wide), "node_limit", "/root"), "Node limit missing");
    wide.root.children.clear();
    tessera::UiNode huge;
    huge.kind = tessera::NodeKind::text;
    huge.properties["text"] = std::string(tessera::max_serialized_bytes, 'x');
    wide.root.children.push_back(std::move(huge));
    check(!tessera::save_document(wide), "Output byte limit missing");
    rejected(std::string(tessera::max_serialized_bytes + 1, ' '), "size_limit", "");
}
}

int main() {
    try {
        round_trip_and_handles();
        semantic_errors();
        syntax_and_schema();
        metadata_and_limits();
        std::cout << "Document, serialization, diagnostics, Unicode, limits and handle checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
