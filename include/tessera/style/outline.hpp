#pragma once

#include <tessera/style/color.hpp>

namespace tessera {

// Outside decoration; does not participate in layout or hit testing.
struct Outline {
    float width = 1;
    float offset = 0;
    Color color;
    float dash_length = 0;
    float gap_length = 0;
    float dash_offset = 0;
    bool operator==(const Outline&) const = default;
};

} // namespace tessera
