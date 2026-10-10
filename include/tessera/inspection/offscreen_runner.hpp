#pragma once

#include <tessera/render/capture.hpp>
#include <tessera/replay/replay.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tessera {

// Prototype deterministic offscreen runner; not a CLI, snapshot bundle, or stable API.

// Total captured pixel bytes one run may hold.
inline constexpr std::size_t max_capture_bytes = std::size_t(256) << 20;

// Borrowed for one run. A capture backend is declared explicitly together with its adapter; without a
// declaration the run is core-only and produces no images. No backend is chosen or substituted.
struct OffscreenInput {
    const ReplayRecording* recording = nullptr;
    TextShaper* text = nullptr;               // The service the recording's environment declares.
    std::optional<CaptureBackend> capture;    // Declared completed-frame backend configuration.
    FrameCapture* frames = nullptr;           // Required exactly when capture is declared.
};

// Run conditions a comparison must match before judging differences.
struct OffscreenManifest {
    std::uint32_t replay_version = replay_version;
    ReplayEnvironment environment;         // Scale, locale, and text service identity from the recording.
    std::optional<CaptureBackend> capture; // Absent for a core-only run.
    bool operator==(const OffscreenManifest&) const = default;
};

enum class FrameStatus : std::uint8_t {
    not_declared, // Core-only run: no capture backend was declared.
    captured,
};

// Completed frame of one replay generation.
struct OffscreenFrame {
    std::size_t generation = 0;
    Size logical_size;   // The generation's viewport.
    float device_scale = 1;
    CaptureExtent extent;
    FrameStatus status = FrameStatus::not_declared;
    std::optional<CapturedImage> image; // Present exactly when captured.
    bool operator==(const OffscreenFrame&) const = default;
};

struct OffscreenRun {
    OffscreenManifest manifest;
    ReplayOutput output;
    std::vector<OffscreenFrame> frames; // One per output generation, in order.
    bool operator==(const OffscreenRun&) const = default;
};

std::vector<Diagnostic> validate(const OffscreenInput&);

// Plays the recording as play_replay does, then captures each generation's owned paint list through the
// declared adapter at the generation's viewport and the recorded device scale. Replay diagnostics are located
// under /recording; capture diagnostics under /generations/<g>/capture. Failure returns no partial run.
Result<OffscreenRun> run_offscreen(const OffscreenInput&);

} // namespace tessera
