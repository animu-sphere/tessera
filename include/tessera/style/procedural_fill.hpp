#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace tessera {

// One typed shader parameter. Each value occupies one four-component slot in declared order:
// numbers as (v, 0, 0, 0), points as (x, y, 0, 0), colors as linear-light straight alpha.
using ProceduralValue = std::variant<float, Point, Color>;
enum class ProceduralValueKind : std::uint8_t { number, point, color };
// Kinds follow the variant alternative order.
static_assert(std::variant_size_v<ProceduralValue> == 3 &&
              std::is_same_v<std::variant_alternative_t<2, ProceduralValue>, Color>);
inline ProceduralValueKind kind_of(const ProceduralValue& value) {
    return static_cast<ProceduralValueKind>(value.index());
}

inline constexpr std::size_t max_procedural_parameters = 16;
inline constexpr std::size_t max_procedural_shader_bytes = 128;

// Requests a backend-registered procedural fill inside the existing background shape. `shader` is a
// logical identity (printable ASCII, no spaces); backends own compilation and pipelines. Time is in
// seconds and Tessera reads no clock. In a style, `time` is the phase at host time 0 and `time_rate`
// the shader seconds per host second (0 is static); paint evaluates `time + time_rate * host_time`
// into the draw command, whose `time` is what backends use and whose rate only marks it as moving.
// Opacity scales shader alpha.
struct ProceduralFill {
    std::string shader;
    std::vector<ProceduralValue> parameters;
    float time = 0;
    float opacity = 1;
    float time_rate = 0;
    bool operator==(const ProceduralFill&) const = default;
};

} // namespace tessera
