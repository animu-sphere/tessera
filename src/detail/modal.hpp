#pragma once

#include "popup.hpp"

namespace tessera::detail {

inline bool modal(const UiNode& node) {
    const auto* value = effective_property(node, property_names::modal);
    return value && std::get_if<bool>(value) && std::get<bool>(*value);
}

inline bool root_layer(const UiNode& node) {
    return popup_anchor(node) || modal(node);
}

} // namespace tessera::detail
