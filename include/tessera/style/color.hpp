#pragma once

#include <string>

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

// Hue in degrees [0, 360] (360 names the same color as 0) and saturation/value in [0, 1]
// of the sRGB-encoded components, so the model matches hexadecimal sRGB colors. Keeping hue
// separate from the color retains it while saturation or value is zero.
struct HsvColor {
    double hue = 0;
    double saturation = 0;
    double value = 0;
    bool operator==(const HsvColor&) const = default;
};

// True for finite components within the ranges above.
bool valid(const HsvColor&) noexcept;
// The opaque sRGB color of a valid HSV value; an invalid one yields opaque black.
Color to_color(const HsvColor&) noexcept;
// "#RRGGBB" from each sRGB component clamped to [0, 1], rounded to the nearest byte with
// halves upward; alpha is ignored and nonfinite components read as zero.
std::string format_hex_color(const Color&);

} // namespace tessera
