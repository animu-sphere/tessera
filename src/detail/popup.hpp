#pragma once

#include "layout_snapshot.hpp"
#include <tessera/ui/property_metadata.hpp>
#include <algorithm>

namespace tessera::detail {

inline const std::string* popup_anchor(const UiNode& node) {
    const auto found = node.properties.find(property_names::popup_anchor);
    return found == node.properties.end() ? nullptr : std::get_if<std::string>(&found->second);
}

inline const LayoutBox* popup_anchor_box(const UiTree& tree, const LayoutResult& layout, const UiNode& popup) {
    const auto* id = popup_anchor(popup);
    const auto anchor = id ? tree.find(*id) : std::nullopt;
    if (!anchor) return nullptr;
    const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
        [&](const LayoutBox& box) { return box.node == *anchor; });
    if (found == layout.boxes.end() || !found->visible) return nullptr;
    const auto visible = found->clip ? intersect(found->border_box, *found->clip) : found->border_box;
    return visible.size.width > 0 && visible.size.height > 0 ? &*found : nullptr;
}

} // namespace tessera::detail
