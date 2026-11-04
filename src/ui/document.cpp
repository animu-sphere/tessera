#include <tessera/ui/document.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "json_detail.hpp"
#include "../detail/collection.hpp"
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
        case PropertyType::boolean:
            return "a boolean";
        case PropertyType::number:
            return "a finite number";
        case PropertyType::string:
            return "a UTF-8 string";
        case PropertyType::reference:
            return "an author-ID reference {\"ref\":\"id\"}";
    }
    return "a supported value";
}

} // namespace

std::vector<Diagnostic> detail::validate_document_parts(std::uint32_t version, const UiNode& root,
                                                        const JsonValue::Object& document_extensions,
                                                        const ValidationContext& context) {
    std::vector<Diagnostic> errors;
    auto error = [&](std::string code, std::string path, std::string message) {
        errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
    };
    auto check_string = [&](std::string_view value, const std::string& path, bool identifier) {
        if (!detail::valid_utf8(value)) error("invalid_utf8", path, "Use valid UTF-8 text.");
        const bool controls =
            std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
        if (identifier && (value.empty() || controls))
            error("invalid_identifier", path, "Identifiers must be nonempty and contain no control characters.");
    };
    std::function<void(const JsonValue&, const std::string&, std::size_t)> metadata;
    metadata = [&](const JsonValue& value, const std::string& path, std::size_t depth) {
        if (depth > max_json_depth) {
            error("depth_limit", path, "Extension metadata exceeds the JSON nesting limit (256).");
            return;
        }
        std::visit(
            [&](const auto& data) {
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
            },
            value.value);
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
    struct TreeEntry {
        const UiNode* node;
        const UiNode* owner;
        std::string path;
    };
    std::vector<TreeEntry> tree_entries;
    std::map<const UiNode*, std::size_t> selected_counts;
    std::size_t count = 0;
    std::function<void(const UiNode&, const std::string&, std::size_t, const UiNode*)> visit;
    visit = [&](const UiNode& node, const std::string& path, std::size_t depth, const UiNode* parent) {
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
                error("property_type", location,
                      "Expected " + expected(descriptor->type) + "; values are not coerced.");
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
                error("popup_gap", path + "/properties/popup_gap",
                      "Use a finite, nonnegative logical gap within float range.");
        }
        const bool point_x = node.properties.contains(property_names::popup_x);
        const bool point_y = node.properties.contains(property_names::popup_y);
        if ((point_x || point_y) && (!point_x || !point_y || !node.properties.contains(property_names::popup_anchor)))
            error("popup_position", path + "/properties", "Supply popup_x and popup_y together on an anchored popup.");
        for (const auto coordinate : {property_names::popup_x, property_names::popup_y}) {
            const auto* value = effective_property(node, coordinate);
            const auto* number = value ? std::get_if<double>(value) : nullptr;
            if (number && (!std::isfinite(*number) || std::abs(*number) > std::numeric_limits<float>::max()))
                error("popup_position", path + "/properties/" + std::string(coordinate),
                      "Use a finite root-logical coordinate within float range.");
        }
        const auto marked = [&](std::string_view name) {
            const auto* value = effective_property(node, name);
            const auto* flag = value ? std::get_if<bool>(value) : nullptr;
            return flag && *flag;
        };
        const auto role = detail::collection_role(node);
        if (node.properties.contains(property_names::collection_role) && role != "list" && role != "tree" &&
            role != "menu" && !detail::collection_item(role))
            error("collection_role", path + "/properties/collection_role",
                  "Choose list, tree, menu, list_item, tree_item, or menu_item.");
        const auto parent_role = parent ? detail::collection_role(*parent) : std::string_view{};
        if ((role == "list_item" && parent_role != "list") || (role == "tree_item" && parent_role != "tree") ||
            (role == "menu_item" && parent_role != "menu"))
            error("collection_parent", path, "Place collection items directly under their matching collection.");
        if (role == "menu" && (!node.id || !node.properties.contains(property_names::popup_anchor)))
            error("menu_popup", path, "Give the root-sibling Menu a stable ID and popup_anchor.");
        if (((parent_role == "menu" && role != "menu_item") || parent_role == "menu_item") &&
            (node.kind != NodeKind::text || !node.events.empty() || marked(property_names::focusable)))
            error("menu_child", path,
                  "Menus contain command items and noninteractive Text, not nested controls or menus.");
        if (role == "tree_item" || parent_role == "tree") tree_entries.push_back({&node, parent, path});
        if (node.properties.contains(property_names::tree_parent) && role != "tree_item")
            error("collection_state", path + "/properties/tree_parent", "Only tree items have a logical tree parent.");
        if (node.properties.contains(property_names::selected) && role != "list_item" && role != "tree_item")
            error("collection_state", path + "/properties/selected", "Only list/tree items have selection state.");
        if (node.properties.contains(property_names::expanded) && role != "tree_item")
            error("collection_state", path + "/properties/expanded", "Only tree items have expansion state.");
        if (const auto* selected = effective_property(node, property_names::selected);
            selected && std::get_if<bool>(selected) && std::get<bool>(*selected) && detail::collection_item(role) &&
            ++selected_counts[parent] > 1)
            error("collection_selection", path + "/properties/selected", "Select at most one item in each collection.");
        if ((role == "tree" || role == "list" || role == "menu") && node.events.contains("activate"))
            error("collection_action", path + "/events/activate", "Put actions on collection items.");
        if (role == "menu_item" && !node.events.contains("activate"))
            error("menu_action", path + "/events/activate", "Give each menu item a registered command action.");
        if (marked(property_names::scrollbar) &&
            (marked(property_names::modal) || marked(property_names::separator) || !role.empty()))
            error("scrollbar_role", path + "/properties/scrollbar",
                  "Do not combine a scrollbar with another semantic role marker.");
        if (!role.empty() && (marked(property_names::modal) || marked(property_names::separator)))
            error("collection_role", path + "/properties/collection_role",
                  "Do not combine a collection role with modal or separator markers.");
        for (const auto& [event, action] : node.events) {
            const auto location = path + "/events/" + detail::pointer_token(event);
            if (event != "activate" && event != "cancel" && event != "expand" && event != "collapse")
                error("unknown_event", location, "Supported bindings are activate, cancel, expand, and collapse.");
            if ((event == "expand" || event == "collapse") &&
                (role != "tree_item" || !node.properties.contains(property_names::expanded)))
                error("collection_event", location, "Expansion bindings require a tree item with expanded state.");
            check_string(action, location, true);
            if (!context.actions.contains(action))
                error("unknown_action", location,
                      "Register host action '" + action + "' in ValidationContext before loading.");
        }
        extensions(node.extensions, path + "/extensions", depth * 2);
        for (std::size_t i = 0; i < node.children.size() && count <= max_document_nodes; ++i)
            visit(node.children[i], path + "/children/" + std::to_string(i), depth + 1, &node);
    };
    extensions(document_extensions, "/extensions", 1);
    visit(root, "/root", 1, nullptr);
    std::vector<const UiNode*> tree_ancestors;
    const UiNode* tree_owner = nullptr;
    for (const auto& entry : tree_entries) {
        if (entry.owner != tree_owner) tree_ancestors.clear();
        tree_owner = entry.owner;
        if (detail::collection_role(*entry.node) != "tree_item") {
            tree_ancestors.clear();
            continue;
        }
        const auto* value = effective_property(*entry.node, property_names::tree_parent);
        const auto* parent = value ? std::get_if<NodeReference>(value) : nullptr;
        if (!parent)
            tree_ancestors.clear();
        else {
            while (!tree_ancestors.empty() && tree_ancestors.back()->id != parent->id) tree_ancestors.pop_back();
            if (tree_ancestors.empty())
                error(
                    "tree_parent", entry.path + "/properties/tree_parent",
                    "Reference an earlier ancestor item in the same tree; keep logical subtrees contiguous in preorder.");
            else if (!tree_ancestors.back()->properties.contains(property_names::expanded))
                error("tree_parent", entry.path + "/properties/tree_parent",
                      "A logical parent must declare expanded state.");
        }
        tree_ancestors.push_back(entry.node);
        if (tree_ancestors.size() > max_document_depth)
            error("depth_limit", entry.path + "/properties/tree_parent", "Keep logical tree depth within 64 levels.");
    }
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
            error("modal_order", "/root/children/" + std::to_string(i),
                  "Place the Modal after all content and popups.");
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
