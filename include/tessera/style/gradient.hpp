#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>

namespace tessera {

// Two stops at 0 and 1, clamped outside the segment. Endpoints are fractions of the border box.
// Interpolation uses premultiplied linear-light RGB and alpha.
struct LinearGradient {
    Point start{0, 0};
    Point end{0, 1};
    Color start_color;
    Color end_color;
    bool operator==(const LinearGradient&) const = default;
};

} // namespace tessera
