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

// Overlap of two rectangles. Disjoint axes keep the larger origin and clamp size to zero.
constexpr Rect intersect(const Rect& a, const Rect& b) noexcept {
    const Point origin{std::max(a.origin.x, b.origin.x), std::max(a.origin.y, b.origin.y)};
    return {origin, {std::max(0.0f, std::min(a.origin.x + a.size.width, b.origin.x + b.size.width) - origin.x),
                     std::max(0.0f, std::min(a.origin.y + a.size.height, b.origin.y + b.size.height) - origin.y)}};
}

} // namespace tessera
