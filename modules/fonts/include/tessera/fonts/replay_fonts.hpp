#pragma once

#include <tessera/fonts/font_profile.hpp>
#include <tessera/replay/replay.hpp>
#include <string>

namespace tessera {

// Font profile identity for Replay v1 environments: lowercase hex SHA-256 of the profile's canonical font
// profile JSON v1 (save_font_profile output, final LF included). Fails as save_font_profile fails.
Result<std::string> font_profile_sha256(const FontProfile&);

// Checks that the recording declares this profile: a font_profile text service whose sha256 is the profile's.
// The declared id is a host lookup name and is not compared. Mismatches are font_profile_mismatch at
// /environment/text/kind or /environment/text/sha256; an invalid profile fails as save_font_profile fails.
std::vector<Diagnostic> verify_replay_fonts(const ReplayRecording&, const FontProfile&);

} // namespace tessera
