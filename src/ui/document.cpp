#include <tessera/ui/document.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {

std::string expected(PropertyType type) {
    switch (type) {
    case PropertyType::boolean: return "a boolean";
    case PropertyType::number: return "a finite number";
    case PropertyType::string: return "a UTF-8 string";
    case PropertyType::reference: return "an author-ID reference {\"ref\":\"id\"}";
    }
    return "a supported value";
}

} // namespace

std::vector<Diagnostic> detail::validate_document_parts(std::uint32_t version, const UiNode& root,
    const JsonValue::Object& document_extensions, const ValidationContext& context) {
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
    if (version != 1)
        error("unsupported_version", "/version", "Only document version 1 is supported; migrate explicitly.");
    std::map<std::string, std::string, std::less<>> ids;
    std::vector<std::pair<std::string, std::string>> references;
    std::vector<std::pair<std::string, std::string>> anchors;
    std::vector<std::string> popups;
    std::size_t modal_count = 0;
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
            const auto* descriptor = find_property_descriptor(name);
            if (!descriptor || !contains(descriptor->accepted, node.kind))
                error("unknown_property", location, "This property is not supported on this node kind.");
            else if (property_type(value) != descriptor->type)
                error("property_type", location, "Expected " + expected(descriptor->type) + "; values are not coerced.");
            if (const auto* string = std::get_if<std::string>(&value)) check_string(*string, location, false);
            if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
                error("invalid_number", location, "Numbers must be finite.");
            if (const auto* ref = std::get_if<NodeReference>(&value)) {
                check_string(ref->id, location + "/ref", true);
                references.emplace_back(ref->id, location + "/ref");
            }
        }
        for (const auto& descriptor : property_descriptors()) {
            if (contains(descriptor.required, node.kind) && !node.properties.contains(descriptor.name))
                error("missing_property", path + "/properties/" + detail::pointer_token(descriptor.name),
                      std::string(node.kind == NodeKind::box ? "Box" : "Text") + " nodes require the '" +
                      std::string(descriptor.name) + "' property: " + expected(descriptor.type) + ".");
        }
        if (node.kind == NodeKind::text && !node.children.empty())
            error("invalid_children", path + "/children", "Text is a leaf; place children in a Box.");
        const auto marker = node.properties.find(property_names::modal);
        if (marker != node.properties.end() && std::get_if<bool>(&marker->second) && std::get<bool>(marker->second)) {
            ++modal_count;
            if (depth != 2)
                error("modal_parent", path, "Place the single Modal directly under the document root, last.");
            if (!node.id)
                error("modal_id", path + "/id", "Give the Modal a stable author ID for dismissal and focus recovery.");
            if (node.properties.contains(property_names::popup_anchor))
                error("modal_popup", path, "A Modal cannot also be an anchored Popup.");
            if (node.events.contains("activate"))
                error("modal_action", path + "/events/activate", "Put activate bindings on controls inside the Modal.");
            popups.push_back(path);
        }
        if (const auto found = node.properties.find(property_names::popup_anchor); found != node.properties.end()) {
            if (const auto* anchor = std::get_if<std::string>(&found->second)) {
                check_string(*anchor, path + "/properties/popup_anchor", true);
                anchors.emplace_back(*anchor, path);
            }
            popups.push_back(path);
            if (depth != 2)
                error("popup_parent", path, "Place popups directly under the document root, after ordinary content.");
        }
        if (const auto found = node.properties.find(property_names::popup_side); found != node.properties.end()) {
            if (const auto* side = std::get_if<std::string>(&found->second);
                side && *side != "bottom" && *side != "top" && *side != "left" && *side != "right")
                error("popup_side", path + "/properties/popup_side", "Choose bottom, top, left, or right.");
        }
        if (const auto found = node.properties.find(property_names::popup_gap); found != node.properties.end()) {
            if (const auto* gap = std::get_if<double>(&found->second);
                gap && (!std::isfinite(*gap) || *gap < 0 || *gap > std::numeric_limits<float>::max()))
                error("popup_gap", path + "/properties/popup_gap", "Use a finite, nonnegative logical gap within float range.");
        }
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
    extensions(document_extensions, "/extensions", 1);
    visit(root, "/root", 1);
    if (count > max_document_nodes) error("node_limit", "/root", "Document exceeds the node limit (10000).");
    for (const auto& [id, path] : references) {
        if (!ids.contains(id)) error("invalid_reference", path, "No node has author ID '" + id + "'.");
    }
    if (modal_count > 1)
        error("modal_count", "/root", "Compose at most one Modal; nested or stacked Modal lifetimes are unsupported.");
    bool popup_seen = false;
    for (std::size_t i = 0; i < root.children.size(); ++i) {
        const bool popup = root.children[i].properties.contains(property_names::popup_anchor);
        const auto marker = root.children[i].properties.find(property_names::modal);
        const bool modal = marker != root.children[i].properties.end() && std::get_if<bool>(&marker->second) &&
            std::get<bool>(marker->second);
        if (modal && i + 1 != root.children.size())
            error("modal_order", "/root/children/" + std::to_string(i), "Place the Modal after all content and popups.");
        if (popup_seen && !popup && !modal)
            error("popup_order", "/root/children/" + std::to_string(i),
                  "Place ordinary content before all root popup children so paint and hit order agree.");
        popup_seen = popup_seen || popup;
    }
    for (const auto& [anchor, path] : anchors) {
        const auto target = ids.find(anchor);
        if (target == ids.end()) continue; // An unmounted anchor suppresses its popup during layout.
        for (const auto& popup : popups) {
            if (target->second == popup || target->second.starts_with(popup + "/"))
                error("popup_anchor", path + "/properties/popup_anchor",
                      "Anchor to ordinary content, not a popup/modal layer or its descendants.");
        }
    }
    return errors;
}

std::vector<Diagnostic> validate(const UiDocument& document, const ValidationContext& context) {
    return detail::validate_document_parts(document.version, document.root, document.extensions, context);
}
} // namespace tessera
