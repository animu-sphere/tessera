#pragma once

#include <cstdint>

namespace tessera {

// Neutral image reference issued by the host/backend resource system. Value 0 is invalid. Draw
// commands and procedural fill texture parameters carry it; no GPU object crosses the core boundary.
struct ImageHandle {
    std::uint64_t value = 0;
    bool operator==(const ImageHandle&) const = default;
};

} // namespace tessera
