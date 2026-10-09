#pragma once

#include <tessera/fonts/font_profile.hpp>
#include <string_view>

namespace tessera {

// Self-contained font-profile JSON v1, separate from UI and Replay versions.
// Asset bytes are lowercase hex on save; load also accepts uppercase hex.
// The asset/expanded-face bounds remain those of FontProfile; 4 MiB allows escaped declarations.
inline constexpr std::size_t max_serialized_font_profile_bytes = 2 * max_font_profile_bytes + 4 * 1024 * 1024;
inline constexpr std::size_t max_font_profile_json_values = 32768;

// Both validate all registrations against a fresh service and return no partial value.
// Load diagnostics include JSON pointers and UTF-8 byte offsets. No filesystem/provider lookup.
Result<FontProfile> load_font_profile(std::string_view json);
Result<std::string> save_font_profile(const FontProfile&);

} // namespace tessera
