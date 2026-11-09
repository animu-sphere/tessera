#pragma once

#include <tessera/input/event.hpp>
#include <optional>

namespace tessera::detail {

// Cross-axis navigation stays available to focus dispatch. Use the caller's numeric precision.
template<class Number>
std::optional<Number> axis_adjustment(const InputEvent& event, bool horizontal, Number current, float minimum,
                                      float maximum, float step) {
    if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
        if (key->key == Key::home) return minimum;
        if (key->key == Key::end) return maximum;
        if (key->key == (horizontal ? Key::left : Key::up)) return current - step;
        if (key->key == (horizontal ? Key::right : Key::down)) return current + step;
    }
    if (const auto* navigate = std::get_if<Navigate>(&event.data)) {
        if (navigate->direction == (horizontal ? Direction::left : Direction::up)) return current - step;
        if (navigate->direction == (horizontal ? Direction::right : Direction::down)) return current + step;
    }
    if (std::holds_alternative<Activate>(event.data)) return current + step;
    return {};
}

} // namespace tessera::detail
