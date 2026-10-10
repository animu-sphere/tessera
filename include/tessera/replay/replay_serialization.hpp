#pragma once

#include <tessera/replay/replay.hpp>
#include <cstddef>
#include <string>
#include <string_view>

namespace tessera {

// Replay JSON v1/v2: versioned separately from UI JSON and font profile JSON. v2 adds declared host slots.
// Documents are embedded UI JSON v1 objects. A font profile is identified by its digest, never embedded.
inline constexpr std::size_t max_serialized_replay_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t max_replay_json_values = 4 * 1024 * 1024;

// Both check every value that needs no shaper or snapshot (version, bounds, environment, actions, documents,
// styles, viewports, events, targets) and return no partial value. Timestamp order, target resolution,
// eligibility, layout, and paint remain playback checks. Load diagnostics carry JSON pointers and UTF-8 byte
// offsets. No filesystem or resource lookup.
Result<ReplayRecording> load_replay(std::string_view json);
Result<std::string> save_replay(const ReplayRecording&);

} // namespace tessera
