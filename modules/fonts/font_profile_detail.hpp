#pragma once

#include <tessera/fonts/font_profile.hpp>

namespace tessera::detail {
// Validates and registers into a fresh service; never changes an existing service.
Result<FontShaper> build_profile_shaper(const FontProfile&);
}
