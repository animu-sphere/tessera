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
inline const tessera::ValidationContext context{{actions[0], actions[1], actions[2], actions[3],
    "direct.edit", "direct.decrease", "direct.increase", "direct.apply", "direct.cancel",
    msaa_actions[0], msaa_actions[1], msaa_actions[2], msaa_actions[3]}};
struct Msaa {
    unsigned accepted = 1;
    std::vector<unsigned> supported{1, 2, 4};
    bool enabled = true;
    bool operator==(const Msaa&) const = default;
};
struct Numeric {
    double accepted = 1;
    tessera::TextEditState draft;
    bool enabled = true;
    bool operator==(const Numeric&) const = default;
};
std::string canonical(double);
tessera::Result<double> parse_numeric(std::string_view);
// Application composition, not a new runtime widget or semantic role.
tessera::authoring::Element NumericControl(const Numeric&);
tessera::authoring::Element MsaaControl(const Msaa&);

std::vector<tessera::Diagnostic> validate(const Snapshot&);
// Call after validation. Pure ordinary composition; no callbacks, sampling or retained pointers.
tessera::authoring::Element Panel(const Snapshot&, const Disclosure&, const Numeric&, const Msaa&);
tessera::StyleSheet panel_styles();
} // namespace diagnostics
