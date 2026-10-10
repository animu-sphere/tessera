#pragma once

#include <tessera/replay/replay.hpp>

namespace tessera::detail {

// Version, step-count, and environment checks shared by playback and Replay JSON v1.
std::vector<Diagnostic> check_recording(const ReplayRecording&);

} // namespace tessera::detail
