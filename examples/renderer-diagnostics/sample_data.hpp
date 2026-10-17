#pragma once
#include "panel.hpp"

namespace diagnostics {
// Declared synthetic inputs, not sampled renderer performance.
inline Snapshot sample_a() {
    return {"Fixture scene", "Synthetic device", "Synthetic FIFO", 1280, 720, 16, 62.5, 24,
        {{"update", "Update", Statistics{4, 6, 8, 1}}, {"submit", "Submit", Statistics{0, 0, 0, 0}}},
        {{"geometry", "Geometry", Statistics{7, 9, 10, 1}}, {"shading", "Shading", Statistics{3, 4, 5, 0.5}}},
        {{"draws", "Draws", 12, "draws"}, {"triangles", "Triangles", 2400, "triangles"},
            {"frame-upload", "Frame upload", 1024, "bytes"}, {"session-upload", "Session upload", 4096, "bytes"},
            {"validation", "Validation messages", 0, "messages"}},
        {{101, 8}, {102, 16}, {103, 24}, {104, 0}}};
}
inline Snapshot sample_b() {
    auto snapshot = sample_a();
    snapshot.frame_ms = 20; snapshot.fps = 50; snapshot.p99_ms = 40;
    snapshot.cpu[0].milliseconds = Statistics{5, 7, 9, 1};
    snapshot.cpu[1].milliseconds = Statistics{1, 2, 3, 0.5};
    snapshot.gpu[0].milliseconds = Statistics{9, 11, 12, 1};
    snapshot.gpu[1].milliseconds = Statistics{4, 5, 6, 0.5};
    snapshot.counters[0].value = 18; snapshot.counters[1].value = 3600;
    snapshot.counters[2].value = 2048; snapshot.counters[3].value = 6144;
    snapshot.history = {{102, 16}, {103, 24}, {104, 0}, {105, 40}};
    return snapshot;
}
inline Snapshot sample_c() {
    auto snapshot = sample_b();
    for (auto& timing : snapshot.gpu) {
        timing.milliseconds.reset(); timing.unavailable = "GPU timestamps unavailable";
    }
    snapshot.history.clear();
    return snapshot;
}
} // namespace diagnostics
