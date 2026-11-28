#pragma once

#include <tessera/style/procedural_fill.hpp>
#include <tessera/ui/document.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tessera {

// Shader ABI profiles whose shaders declare typed parameters: procedural fills and custom shapes.
enum class ShaderProfile : std::uint8_t { fill_v1, sdf_v1 };

// One declared parameter: its shader field name and the kind of value its slot takes.
struct ShaderParameter {
    std::string name; // An ASCII identifier: a letter or '_', then letters, digits or '_'.
    ProceduralValueKind kind = ProceduralValueKind::number;
    bool operator==(const ShaderParameter&) const = default;
};

// A shader's parameters in slot order, reflected from its source at build time (backends own
// compilation and reflection). Requests naming the shader supply exactly these values, positionally.
struct ShaderParameters {
    ShaderProfile profile = ShaderProfile::fill_v1;
    std::vector<ShaderParameter> parameters;
    bool operator==(const ShaderParameters&) const = default;
};

inline constexpr std::size_t max_shader_parameter_name_bytes = 64;
inline constexpr std::size_t max_serialized_shader_parameters_bytes = std::size_t(16) << 10;

// Checks the profile and kind enums, at most 16 parameters and unique identifier names.
std::vector<Diagnostic> validate(const ShaderParameters&);
// Shader parameter manifest JSON v1: {"format": "tessera-shader-parameters", "version": 1,
// "profile": "fill-v1" | "sdf-v1", "parameters": [{"name": ..., "kind": "number" | "point" | "color"}]}.
Result<ShaderParameters> load_shader_parameters(std::string_view json);
// Canonical manifest: sorted keys, no whitespace and a trailing newline.
Result<std::string> save_shader_parameters(const ShaderParameters&);
// The parameter's slot index, or no value when the declaration has no parameter of that name.
std::optional<std::size_t> find_parameter(const ShaderParameters&, std::string_view name);

} // namespace tessera
