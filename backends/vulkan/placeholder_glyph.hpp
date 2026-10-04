#pragma once

#include <array>
#include <cstdint>

namespace tessera::detail {

// Original 5x7 test marks. Bit 4 is the leftmost column; rows run top to bottom.
// Lowercase deliberately uses uppercase marks. Unsupported scalars use a box.
inline std::array<std::uint8_t, 7> placeholder_glyph(std::uint32_t scalar) {
    using Rows = std::array<std::uint8_t, 7>;
    static constexpr Rows letters[]{
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, // A B
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30}, // C D
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, // E F
        {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17}, // G H
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},       // I J
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31}, // K L
        {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17}, // M N
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16}, // O P
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, // Q R
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},       // S T
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, // U V
        {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17}, // W X
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},       // Y Z
    };
    static constexpr Rows digits[]{
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };
    if (scalar >= 'a' && scalar <= 'z') scalar -= 'a' - 'A';
    if (scalar >= 'A' && scalar <= 'Z') return letters[scalar - 'A'];
    if (scalar >= '0' && scalar <= '9') return digits[scalar - '0'];
    switch (scalar) {
    case ' ': return {};
    case '.': return {0,0,0,0,0,4,4};
    case ',': return {0,0,0,0,4,4,8};
    case ':': return {0,4,4,0,4,4,0};
    case '-': return {0,0,0,31,0,0,0};
    case '_': return {0,0,0,0,0,0,31};
    case '+': return {0,4,4,31,4,4,0};
    case '/': return {1,2,2,4,8,8,16};
    case '!': return {4,4,4,4,4,0,4};
    case '?': return {14,17,1,2,4,0,4};
    case '(': return {2,4,8,8,8,4,2};
    case ')': return {8,4,2,2,2,4,8};
    default: return {31,17,17,17,17,17,31};
    }
}
} // namespace tessera::detail
