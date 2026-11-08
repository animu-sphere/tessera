#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>

namespace tessera {

// Finite distance-based outer shadow, not a Gaussian blur.
struct BoxShadow {
    Point offset;
    float softness = 0;
    Color color;
    bool operator==(const BoxShadow&) const = default;
};

} // namespace tessera
