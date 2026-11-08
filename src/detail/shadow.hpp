#pragma once

#include <tessera/layout/geometry.hpp>
#include <cmath>
#include <limits>
#include <optional>

namespace tessera::detail {

inline std::optional<Rect> shadow_bounds(Rect rect, float softness, bool inset = false) {
    if (inset) softness = 0;
    const double x = double(rect.origin.x) - softness, y = double(rect.origin.y) - softness;
    const double w = double(rect.size.width) + 2 * double(softness),
                 h = double(rect.size.height) + 2 * double(softness);
    const auto safe = [](double value) {
        return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
    };
    if (!safe(x) || !safe(y) || !safe(w) || !safe(h) || !safe(x + w) || !safe(y + h)) return {};
    return Rect{{float(x), float(y)}, {float(w), float(h)}};
}

} // namespace tessera::detail
