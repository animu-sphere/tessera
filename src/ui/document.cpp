#include <tessera/ui/document.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>

namespace tessera {

std::vector<Diagnostic> validate(const UiDocument& document, const ValidationContext& context) {
    std::vector<Diagnostic> errors;
    auto error = [&](std::string code, std::string path, std::string message) {
        errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
    };
    auto check_string = [&](std::string_view value, const std::string& path, bool identifier) {
        if (!detail::valid_utf8(value)) error("invalid_utf8", path, "Use valid UTF-8 text.");
        const bool controls = std::any_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 0x20 || c == 0x7f;
        });
        if (identifier && (value.empty() || controls))
            error("invalid_identifier", path, "Identifiers must be nonempty and contain no control characters.");
    };
    std::function<void(const JsonValue&, const std::string&, std::size_t)> metadata;
    metadata = [&](const JsonValue& value, const std::string& path, std::size_t depth) {
        if (depth > max_json_depth) {
            error("depth_limit", path, "Extension metadata exceeds the JSON nesting limit (256).");
            return;
        }
        std::visit([&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, double>) {
                if (!std::isfinite(data)) error("invalid_number", path, "Numbers must be finite.");
            } else if constexpr (std::is_same_v<T, std::string>) {
                check_string(data, path, false);
            } else if constexpr (std::is_same_v<T, JsonValue::Array>) {
                for (std::size_t i = 0; i < data.size(); ++i)
                    metadata(data[i], path + "/" + std::to_string(i), depth + 1);
            } else if constexpr (std::is_same_v<T, JsonValue::Object>) {
                for (const auto& [key, child] : data) {
                    check_string(key, path, false);
                    metadata(child, path + "/" + detail::pointer_token(key), depth + 1);
                }
            }
        }, value.value);
    };
    auto extensions = [&](const JsonValue::Object& values, const std::string& path, std::size_t depth) {
        for (const auto& [key, value] : values) {
            const auto location = path + "/" + detail::pointer_token(key);
            check_string(key, location, true);
            const auto colon = key.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 == key.size())
                error("invalid_extension", location, "Use a namespaced extension name, e.g. 'path-finder:editor'.");
            metadata(value, location, depth + 1);
        }
    };
    if (document.version != 1)
        error("unsupported_version", "/version", "Only document version 1 is supported; migrate explicitly.");
    std::map<std::string, std::string, std::less<>> ids;
    std::vector<std::pair<std::string, std::string>> references;
    std::size_t count = 0;
    std::function<void(const UiNode&, const std::string&, std::size_t)> visit;
    visit = [&](const UiNode& node, const std::string& path, std::size_t depth) {
        if (++count > max_document_nodes) return;
        if (depth > max_document_depth) {
            error("depth_limit", path, "Document exceeds the node nesting limit (64).");
            return;
        }
        if (node.kind != NodeKind::box && node.kind != NodeKind::text)
            error("unknown_node_kind", path + "/type", "Supported node kinds are Box and Text.");
        if (node.id) {
            check_string(*node.id, path + "/id", true);
            auto [existing, inserted] = ids.emplace(*node.id, path);
            if (!inserted) error("duplicate_id", path + "/id", "ID already defined at " + existing->second + "/id.");
        }
        std::set<std::string, std::less<>> classes;
        for (std::size_t i = 0; i < node.classes.size(); ++i) {
            const auto location = path + "/classes/" + std::to_string(i);
            check_string(node.classes[i], location, true);
            if (!classes.insert(node.classes[i]).second)
                error("duplicate_class", location, "Remove the duplicate class name.");
        }
        for (const auto& [name, value] : node.properties) {
            const auto location = path + "/properties/" + detail::pointer_token(name);
            const bool text = name == "text" && node.kind == NodeKind::text;
            const bool boolean = name == "focusable" || name == "disabled";
            const bool reference = name == "labelled_by";
            if (!text && !boolean && !reference) {
                error("unknown_property", location, "This property is not supported on this node kind.");
            } else if ((text && !std::holds_alternative<std::string>(value)) ||
                       (boolean && !std::holds_alternative<bool>(value)) ||
                       (reference && !std::holds_alternative<NodeReference>(value))) {
                error("property_type", location, text ? "Expected a UTF-8 string." :
                    boolean ? "Expected a boolean; numeric/string coercion is not supported." :
                              "Expected an author-ID reference: {\"ref\":\"id\"}.");
            }
            if (const auto* string = std::get_if<std::string>(&value)) check_string(*string, location, false);
            if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
                error("invalid_number", location, "Numbers must be finite.");
            if (const auto* ref = std::get_if<NodeReference>(&value)) {
                check_string(ref->id, location + "/ref", true);
                references.emplace_back(ref->id, location + "/ref");
            }
        }
        if (node.kind == NodeKind::text && !node.properties.contains("text"))
            error("missing_property", path + "/properties/text", "Text nodes require the 'text' string property.");
        if (node.kind == NodeKind::text && !node.children.empty())
            error("invalid_children", path + "/children", "Text is a leaf; place children in a Box.");
        for (const auto& [event, action] : node.events) {
            const auto location = path + "/events/" + detail::pointer_token(event);
            if (event != "activate" && event != "cancel")
                error("unknown_event", location, "Supported binding names are activate and cancel.");
            check_string(action, location, true);
            if (!context.actions.contains(action))
                error("unknown_action", location, "Register host action '" + action + "' in ValidationContext before loading.");
        }
        extensions(node.extensions, path + "/extensions", depth * 2);
        for (std::size_t i = 0; i < node.children.size() && count <= max_document_nodes; ++i)
            visit(node.children[i], path + "/children/" + std::to_string(i), depth + 1);
    };
    extensions(document.extensions, "/extensions", 1);
    visit(document.root, "/root", 1);
    if (count > max_document_nodes) error("node_limit", "/root", "Document exceeds the node limit (10000).");
    for (const auto& [id, path] : references) {
        if (!ids.contains(id)) error("invalid_reference", path, "No node has author ID '" + id + "'.");
    }
    return errors;
}
} // namespace tessera
