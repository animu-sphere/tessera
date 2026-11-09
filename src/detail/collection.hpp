#pragma once

#include <tessera/ui/property_metadata.hpp>
#include <tessera/ui/tree.hpp>

namespace tessera::detail {

inline std::string_view collection_role(const UiNode& node) {
    const auto* value = effective_property(node, property_names::collection_role);
    const auto* role = value ? std::get_if<std::string>(value) : nullptr;
    return role ? std::string_view(*role) : std::string_view{};
}

inline bool collection_item(std::string_view role) {
    return role == "list_item" || role == "tree_item" || role == "menu_item" || role == "radio" || role == "tab" ||
           role == "option";
}

inline bool collection_owner_role(std::string_view role) {
    return role == "list" || role == "tree" || role == "menu" || role == "radio_group" || role == "tab_list" ||
           role == "listbox";
}

// Items whose group is one sequential focus stop and whose movement requests selection.
inline bool roving_item(std::string_view role) { return role == "radio" || role == "tab"; }

// Flat layout rows retain logical ancestry for disabled state as well as semantics.
inline bool tree_ancestor_disabled(const UiTree& tree, const UiNode& node) {
    auto parent = effective_property(node, property_names::tree_parent);
    while (parent) {
        const auto& ancestor = *tree.get(*tree.find(std::get<NodeReference>(*parent).id));
        if (std::get<bool>(*effective_property(ancestor, property_names::disabled))) return true;
        parent = effective_property(ancestor, property_names::tree_parent);
    }
    return false;
}

} // namespace tessera::detail
