#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/document.hpp>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace tessera {

// Neutral image reference issued by the host/backend resource system. Value 0 is invalid.
struct ImageHandle {
    std::uint64_t value = 0;
    bool operator==(const ImageHandle&) const = default;
};

// x' = a*x + c*y + tx; y' = b*x + d*y + ty.
struct Affine2D {
    float a = 1;
    float b = 0;
    float c = 0;
    float d = 1;
    float tx = 0;
    float ty = 0;
    bool operator==(const Affine2D&) const = default;
};

// Geometry is in the current transform's logical coordinates.
struct DrawRect {
    Rect rect;
    Color color;
    float corner_radius = 0;
    bool operator==(const DrawRect&) const = default;
};
// Border painted inside rect.
struct DrawBorder {
    Rect rect;
    Edges widths;
    Color color;
    float corner_radius = 0;
    bool operator==(const DrawBorder&) const = default;
};
struct DrawImage {
    Rect rect;
    ImageHandle image;
    Rect source{{0, 0}, {1, 1}}; // Normalized image coordinates.
    Color tint{1, 1, 1, 1};
    bool operator==(const DrawImage&) const = default;
};
struct DrawGlyphRun {
    Point origin; // Text box top left; glyph positions are relative to it.
    GlyphRun run;
    Color color;
    bool operator==(const DrawGlyphRun&) const = default;
};
// Intersects the current clip with an axis-aligned rectangle.
struct PushClip {
    Rect rect;
    bool operator==(const PushClip&) const = default;
};
struct PopClip {
    bool operator==(const PopClip&) const = default;
};
// Composes with the current transform: current * transform.
struct PushTransform {
    Affine2D transform;
    bool operator==(const PushTransform&) const = default;
};
struct PopTransform {
    bool operator==(const PopTransform&) const = default;
};

using DrawCommand = std::variant<DrawRect, DrawBorder, DrawImage, DrawGlyphRun,
                                 PushClip, PopClip, PushTransform, PopTransform>;

// Commands paint in order; later commands appear above earlier ones.
// Clips and transforms share one LIFO stack that must be balanced.
struct UiDrawList {
    std::vector<DrawCommand> commands;
    bool operator==(const UiDrawList&) const = default;
};

inline constexpr std::size_t max_draw_stack_depth = 64;

std::vector<Diagnostic> validate(const UiDrawList&);

} // namespace tessera
