#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <vector>

namespace tessera {

struct GradientStop {
    float offset = 0;
    Color color;
    bool operator==(const GradientStop&) const = default;
};

// Endpoints are fractions of the border box; colors interpolate in premultiplied linear light.
struct LinearGradient {
    Point start{0, 0};
    Point end{0, 1};
    Color start_color;
    Color end_color;
    // Empty preserves the two-color shorthand. Otherwise, use at least two nondecreasing offsets in [0, 1].
    std::vector<GradientStop> stops;
    std::size_t stop_count() const { return stops.empty() ? 2 : stops.size(); }
    GradientStop stop(std::size_t index) const {
        return stops.empty() ? (index == 0 ? GradientStop{0, start_color} : GradientStop{1, end_color}) : stops[index];
    }
    bool operator==(const LinearGradient&) const = default;
};

} // namespace tessera
