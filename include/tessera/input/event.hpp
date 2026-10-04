#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/ui/tree.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tessera {

// Host-assigned identity, stable from PointerDown until PointerUp/PointerCancel.
struct PointerId {
    std::uint32_t value = 0;
    bool operator==(const PointerId&) const = default;
};

enum class PointerButton : std::uint8_t { primary, secondary, middle };

struct Modifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
    bool operator==(const Modifiers&) const = default;
};

// Logical (layout-mapped) keys for control and shortcuts. Characters arrive as TextInput.
enum class Key : std::uint8_t {
    tab, enter, escape, space, backspace, delete_key, insert, home, end, page_up, page_down,
    left, right, up, down,
    a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
    digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
};

enum class Direction : std::uint8_t { up, down, left, right };

struct PointerMove {
    PointerId pointer;
    Point position;
    Modifiers modifiers;
    bool operator==(const PointerMove&) const = default;
};
struct PointerDown {
    PointerId pointer;
    Point position;
    PointerButton button = PointerButton::primary;
    Modifiers modifiers;
    bool operator==(const PointerDown&) const = default;
};
struct PointerUp {
    PointerId pointer;
    Point position;
    PointerButton button = PointerButton::primary;
    Modifiers modifiers;
    bool operator==(const PointerUp&) const = default;
};
// The host abandoned the pointer (lost window focus, capture loss, device removal).
struct PointerCancel {
    PointerId pointer;
    bool operator==(const PointerCancel&) const = default;
};
// Delta in logical units; positive values reveal content further right/down.
struct Scroll {
    Point position;
    Point delta;
    Modifiers modifiers;
    bool operator==(const Scroll&) const = default;
};
struct KeyDown {
    Key key = Key::enter;
    Modifiers modifiers;
    bool repeat = false;
    bool operator==(const KeyDown&) const = default;
};
struct KeyUp {
    Key key = Key::enter;
    Modifiers modifiers;
    bool operator==(const KeyUp&) const = default;
};
// Committed UTF-8 text. No composition/IME state.
struct TextInput {
    std::string text;
    bool operator==(const TextInput&) const = default;
};
// Logical navigation after the host applied dead zones and repeat policy.
struct Navigate {
    Direction direction = Direction::down;
    bool repeat = false;
    bool operator==(const Navigate&) const = default;
};
struct FocusNext {
    bool operator==(const FocusNext&) const = default;
};
struct FocusPrevious {
    bool operator==(const FocusPrevious&) const = default;
};
struct Activate {
    bool operator==(const Activate&) const = default;
};
struct Cancel {
    bool operator==(const Cancel&) const = default;
};

struct InputEvent {
    std::chrono::microseconds timestamp{0}; // Host monotonic clock; non-decreasing per stream.
    std::variant<PointerMove, PointerDown, PointerUp, PointerCancel, Scroll, KeyDown, KeyUp,
                 TextInput, Navigate, FocusNext, FocusPrevious, Activate, Cancel> data;
    bool operator==(const InputEvent&) const = default;
};

std::vector<Diagnostic> validate(const InputEvent&);

// Dispatch output. Tessera stores no host callbacks: the host runs each requested action
// after dispatch returns, and applies resulting mutations at its next update point.
struct ActionRequest {
    std::string binding; // "activate" or "cancel".
    std::string action;  // A name accepted through ValidationContext::actions.
    NodeHandle target;   // Valid only while the producing tree snapshot is alive.
    bool operator==(const ActionRequest&) const = default;
};

} // namespace tessera
