#pragma once

#include <tessera/ui/authoring.hpp>
#include <array>

namespace diagnostics {
// Owned display data. Sampling, aggregation and renderer objects belong to the caller.
struct Statistics {
    double mean = 0, p95 = 0, maximum = 0, stddev = 0;
    bool operator==(const Statistics&) const = default;
};
struct Timing {
    std::string id, label;
    std::optional<Statistics> milliseconds;
    std::string unavailable;
    bool operator==(const Timing&) const = default;
};
struct Counter {
    std::string id, label;
    std::uint64_t value = 0;
    std::string unit;
    bool operator==(const Counter&) const = default;
};
struct Sample {
    std::uint64_t frame = 0;
    double milliseconds = 0;
    bool operator==(const Sample&) const = default;
};
struct Snapshot {
    std::string scene, device, presentation;
    std::uint32_t width = 0, height = 0;
    double frame_ms = 0, fps = 0, p99_ms = 0;
    std::vector<Timing> cpu, gpu;
    std::vector<Counter> counters;
    std::vector<Sample> history; // Oldest first; at most 32 samples.
    bool operator==(const Snapshot&) const = default;
};
struct Disclosure {
    std::array<bool, 4> expanded{true, true, true, true}; // CPU, GPU, counters, controls.
    bool operator==(const Disclosure&) const = default;
};
inline constexpr tessera::Size viewport{720, 160};
inline constexpr std::array<const char*, 4> groups{"cpu", "gpu", "counters", "controls"};
inline constexpr std::array<const char*, 4> actions{"diagnostics.cpu", "diagnostics.gpu", "diagnostics.counters", "diagnostics.controls"};
inline constexpr std::array<unsigned, 4> sample_counts{1, 2, 4, 8};
inline constexpr std::array<const char*, 4> msaa_actions{"msaa.samples1", "msaa.samples2", "msaa.samples4", "msaa.samples8"};
inline constexpr std::array<const char*, 3> material_views{"shaded", "normals", "base-color"};
inline constexpr std::array<const char*, 3> material_view_actions{"material_view.shaded", "material_view.normals", "material_view.base-color"};
inline constexpr std::array<const char*, 3> material_view_commands{"material_view.shaded", "material_view.normals", "material_view.base_color"};
inline const tessera::ValidationContext context{{actions[0], actions[1], actions[2], actions[3],
    "direct.edit", "direct.decrease", "direct.increase", "direct.apply", "direct.cancel",
    "ambient.edit", "ambient.decrease", "ambient.increase", "ambient.apply", "ambient.cancel",
    "key_x.edit", "key_x.decrease", "key_x.increase", "key_x.apply", "key_x.cancel",
    "key_y.edit", "key_y.decrease", "key_y.increase", "key_y.apply", "key_y.cancel",
    "key_z.edit", "key_z.decrease", "key_z.increase", "key_z.apply", "key_z.cancel",
    msaa_actions[0], msaa_actions[1], msaa_actions[2], msaa_actions[3], "outlines.toggle", "scene_lights.toggle",
    material_view_actions[0], material_view_actions[1], material_view_actions[2]}};
struct MaterialView {
    std::string accepted = "shaded";
    bool enabled = true;
    bool operator==(const MaterialView&) const = default;
};
struct Msaa {
    unsigned accepted = 1;
    std::vector<unsigned> supported{1, 2, 4};
    bool enabled = true;
    bool operator==(const Msaa&) const = default;
};
struct Outlines {
    bool accepted = false;
    bool enabled = true;
    bool operator==(const Outlines&) const = default;
};
struct SceneLights {
    bool accepted = true;
    bool enabled = true;
    bool operator==(const SceneLights&) const = default;
};
struct Numeric {
    double accepted = 1;
    tessera::TextEditState draft;
    bool enabled = true;
    bool operator==(const Numeric&) const = default;
};
// Key direction components are independent numeric fields; the host never normalizes the vector.
struct KeyDirection {
    Numeric x{0}, y{0}, z{-1};
    bool operator==(const KeyDirection&) const = default;
};
enum class NumericField { direct, ambient, key_x, key_y, key_z };
// Strength fields; key direction components are listed separately.
inline constexpr std::array<NumericField, 2> numeric_fields{NumericField::direct, NumericField::ambient};
inline constexpr std::array<NumericField, 3> key_fields{NumericField::key_x, NumericField::key_y, NumericField::key_z};
inline constexpr std::array<NumericField, 5> all_numeric_fields{NumericField::direct, NumericField::ambient,
    NumericField::key_x, NumericField::key_y, NumericField::key_z};
struct NumericBounds {
    double minimum = 0, maximum = 4;
};
// Command and typed-set field identity, such as `key_x`.
std::string field_name(NumericField);
// Author-ID prefix, such as `key-x`; command segments cannot contain hyphens.
std::string field_id(NumericField);
// Accessible TextInput label, such as `Key direction X`.
std::string field_label(NumericField);
NumericBounds field_bounds(NumericField);
// Resolves an exact command field identity or an author ID beginning with `<field_id>-`.
std::optional<NumericField> numeric_field(std::string_view name);
std::optional<NumericField> numeric_owner(std::string_view author_id);
std::string canonical(double);
tessera::Result<double> parse_numeric(std::string_view, NumericField = NumericField::direct);
// Application composition, not a new runtime widget or semantic role.
tessera::authoring::Element NumericControl(const Numeric&, NumericField = NumericField::direct);
tessera::authoring::Element KeyDirectionControl(const KeyDirection&);
tessera::authoring::Element MsaaControl(const Msaa&);
tessera::authoring::Element MaterialViewControl(const MaterialView&);

std::vector<tessera::Diagnostic> validate(const Snapshot&);
// Call after validation. Pure ordinary composition; no callbacks, sampling or retained pointers.
tessera::authoring::Element Panel(const Snapshot&, const Disclosure&, const Numeric&, const Msaa&, const Outlines&, const SceneLights&, const Numeric& ambient, const MaterialView&, const KeyDirection&);
tessera::StyleSheet panel_styles();
} // namespace diagnostics
