#pragma once

namespace tessera {

// sRGB-encoded components in [0, 1] with straight (non-premultiplied) alpha.
// Backends convert to their target format and blending convention.
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
    bool operator==(const Color&) const = default;
};

} // namespace tessera
