#pragma once

#include <cstdint>

namespace tessera {

// Paint-only resolved fields a transition may interpolate. None of them affects layout, input
// targeting or semantics, so presenting an intermediate value needs only a new paint list.
struct TransitionProperties {
    bool opacity = false;
    bool background = false;
    bool border_color = false;
    bool color = false;
    bool corner_radius = false;
    bool box_shadow = false;
    bool outline = false;
    bool any() const noexcept {
        return opacity || background || border_color || color || corner_radius || box_shadow || outline;
    }
    static constexpr TransitionProperties all() noexcept { return {true, true, true, true, true, true, true}; }
    bool operator==(const TransitionProperties&) const = default;
};

// Fixed timing curves. The cubic presets use the CSS control points: ease (.25, .1, .25, 1),
// ease_in (.42, 0, 1, 1), ease_out (0, 0, .58, 1) and ease_in_out (.42, 0, .58, 1).
enum class Easing : std::uint8_t { linear, ease, ease_in, ease_out, ease_in_out };

// How a node's named fields move to a new target: over `duration` seconds of host time along
// `easing`, starting when the host accepts the generation that changed them. The target style's
// transition applies, so a rule can choose different timing for entering and leaving a state.
struct Transition {
    TransitionProperties properties;
    float duration = 0; // Finite seconds >= 0; zero presents the target immediately.
    Easing easing = Easing::ease;
    bool operator==(const Transition&) const = default;
};

// Eased progress for linear progress `t`, clamped to [0, 1]; returns exactly 0 at 0 and 1 at 1.
// Deterministic: a fixed-iteration solve with no platform math beyond IEEE arithmetic.
double ease(Easing, double t) noexcept;

} // namespace tessera
