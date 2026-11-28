#pragma once

#include <tessera/input/event.hpp>

namespace tessera::detail {

// Unmodified Escape or logical Cancel: input that cancels a pointer drag and belongs to it.
inline bool drag_escape(const InputEvent& event) {
    if (std::holds_alternative<Cancel>(event.data)) return true;
    const auto* key = std::get_if<KeyDown>(&event.data);
    return key && key->key == Key::escape && key->modifiers == Modifiers{};
}

// Keyboard, text and logical input that ends a pointer drag but continues to ordinary routing.
inline bool drag_interrupt(const InputEvent& event) {
    return std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<TextInput>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<FocusNext>(event.data) ||
           std::holds_alternative<FocusPrevious>(event.data) || std::holds_alternative<Activate>(event.data);
}

} // namespace tessera::detail
