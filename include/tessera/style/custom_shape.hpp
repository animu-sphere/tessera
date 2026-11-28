#pragma once

#include <tessera/style/procedural_fill.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace tessera {

inline constexpr std::size_t max_custom_shape_parameters = 16;

// Requests a backend-registered signed-distance shape (profile sdf-v1) in place of the built-in
// background shape. `shader` is a logical identity with the same rules as ProceduralFill::shader;
// backends own compilation and pipelines. The shader returns a signed distance in local logical
// units: zero or negative is inside. Coverage stays within the fill box and uses its hard or
// opt-in 4x4 sampling, painted with the fill's color or gradient. A backend without the shader
// draws the built-in `shape` and `corner_radius` instead, as the declared fallback. Parameters
// use ProceduralFill's typed slots; `time` and `time_rate` follow ProceduralFill's host-time rules.
struct CustomShape {
    std::string shader;
    std::vector<ProceduralValue> parameters;
    float time = 0;
    float time_rate = 0;
    bool operator==(const CustomShape&) const = default;
};

} // namespace tessera
