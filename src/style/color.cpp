#include <tessera/style/color.hpp>
#include <algorithm>
#include <cmath>

namespace tessera {

bool valid(const HsvColor& color) noexcept {
    return std::isfinite(color.hue) && std::isfinite(color.saturation) && std::isfinite(color.value) &&
           color.hue >= 0 && color.hue <= 360 && color.saturation >= 0 && color.saturation <= 1 && color.value >= 0 &&
           color.value <= 1;
}

Color to_color(const HsvColor& color) noexcept {
    if (!valid(color)) return {0, 0, 0, 1};
    const double sector = color.hue == 360 ? 0 : color.hue / 60;
    const double chroma = color.value * color.saturation;
    const double second = chroma * (1 - std::abs(std::fmod(sector, 2) - 1));
    const double low = color.value - chroma;
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(sector)) {
        case 0:
            r = chroma, g = second;
            break;
        case 1:
            r = second, g = chroma;
            break;
        case 2:
            g = chroma, b = second;
            break;
        case 3:
            g = second, b = chroma;
            break;
        case 4:
            r = second, b = chroma;
            break;
        default:
            r = chroma, b = second;
            break;
    }
    return {static_cast<float>(r + low), static_cast<float>(g + low), static_cast<float>(b + low), 1};
}

std::string format_hex_color(const Color& color) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (const float channel : {color.r, color.g, color.b}) {
        const double clamped = std::isfinite(channel) ? std::clamp(static_cast<double>(channel), 0., 1.) : 0.;
        const auto byte = static_cast<int>(std::floor(clamped * 255 + 0.5));
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

} // namespace tessera
