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
    std::array<bool, 3> expanded{true, true, true}; // CPU, GPU, counters.
    bool operator==(const Disclosure&) const = default;
};
inline constexpr tessera::Size viewport{720, 160};
inline constexpr std::array<const char*, 3> groups{"cpu", "gpu", "counters"};
inline constexpr std::array<const char*, 3> actions{"diagnostics.cpu", "diagnostics.gpu", "diagnostics.counters"};
inline const tessera::ValidationContext context{{actions[0], actions[1], actions[2]}};

std::vector<tessera::Diagnostic> validate(const Snapshot&);
// Call after validation. Pure ordinary composition; no callbacks, sampling or retained pointers.
tessera::authoring::Element Panel(const Snapshot&, const Disclosure&);
tessera::StyleSheet panel_styles();
} // namespace diagnostics
