#pragma once

#include <tessera/render/draw_list.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace tessera::detail {

inline std::optional<Rect> expanded_stroke_bounds(Rect rect, double x, double y) {
    const double left = double(rect.origin.x) - x, top = double(rect.origin.y) - y;
    const double width = double(rect.size.width) + 2 * x, height = double(rect.size.height) + 2 * y;
    for (double value : {left, top, width, height, left + width, top + height})
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return std::nullopt;
    return Rect{{float(left), float(top)}, {float(width), float(height)}};
}

inline bool stroke_contains(double x, double y, double w, double h, double radius, double width, double dash,
                            double gap, double phase) {
    if (width == 0 || w == 0 || h == 0 || x < 0 || y < 0 || x >= w || y >= h) return false;
    const double r = std::min(radius, std::min(w, h) * .5);
    const auto rounded = [](double x, double y, double w, double h, double r) {
        const double qx = std::abs(x - w * .5) - (w * .5 - r);
        const double qy = std::abs(y - h * .5) - (h * .5 - r);
        return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) <= r;
    };
    if (!rounded(x, y, w, h, r)) return false;
    width = std::min(width, std::min(w, h) * .5);
    const double iw = w - 2 * width, ih = h - 2 * width;
    if (iw > 0 && ih > 0 && x >= width && y >= width && x < w - width && y < h - width &&
        rounded(x - width, y - width, iw, ih, std::max(0.0, r - width)))
        return false;
    if (gap == 0) return true;

    // Project onto the centerline; arclength continues across all four corners.
    x -= width * .5;
    y -= width * .5;
    w -= width;
    h -= width;
    const double cr = std::max(0.0, r - width * .5), a = w - 2 * cr, b = h - 2 * cr;
    constexpr double half_pi = 1.5707963267948966;
    const double arc = cr * half_pi, perimeter = 2 * a + 2 * b + 4 * arc;
    double position;
    if (x < cr && y < cr)
        position = perimeter - arc + cr * std::atan2(cr - y, cr - x);
    else if (x > w - cr && y < cr)
        position = a + cr * std::atan2(x - (w - cr), cr - y);
    else if (x > w - cr && y > h - cr)
        position = a + arc + b + cr * std::atan2(y - (h - cr), x - (w - cr));
    else if (x < cr && y > h - cr)
        position = 2 * a + 2 * arc + b + cr * std::atan2(cr - x, y - (h - cr));
    else {
        const double top = std::abs(y), right = std::abs(w - x), bottom = std::abs(h - y), left = std::abs(x);
        const double nearest = std::min({top, right, bottom, left});
        if (nearest == top)
            position = std::clamp(x - cr, 0.0, a);
        else if (nearest == right)
            position = a + arc + std::clamp(y - cr, 0.0, b);
        else if (nearest == bottom)
            position = a + 2 * arc + b + std::clamp(w - cr - x, 0.0, a);
        else
            position = 2 * a + 3 * arc + b + std::clamp(h - cr - y, 0.0, b);
    }
    const double period = dash + gap;
    double at = std::fmod(std::fmod(position, perimeter), period) + std::fmod(phase, period);
    at = std::fmod(at, period);
    if (at < 0) at += period;
    return at < dash;
}

} // namespace tessera::detail
