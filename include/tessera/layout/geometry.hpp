#pragma once

#include <algorithm>

namespace tessera {

// Logical units: origin at the top left, +X right, +Y down. Renderers apply device scale.
struct Point {
    float x = 0;
    float y = 0;
    bool operator==(const Point&) const = default;
};

struct Size {
    float width = 0;
    float height = 0;
    bool operator==(const Size&) const = default;
};

struct Rect {
    Point origin;
    Size size;
    bool operator==(const Rect&) const = default;
};

struct Edges {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
    bool operator==(const Edges&) const = default;
};

// Moves each side inward; an overconstrained axis keeps its origin offset and clamps size to zero.
constexpr Rect inset(const Rect& rect, const Edges& edges) noexcept {
    return {{rect.origin.x + edges.left, rect.origin.y + edges.top},
            {std::max(0.0f, rect.size.width - edges.left - edges.right),
             std::max(0.0f, rect.size.height - edges.top - edges.bottom)}};
}

} // namespace tessera
