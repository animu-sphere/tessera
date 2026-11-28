// Owner fixture: the built-in ellipse equation as a distance-like inside/outside function, so its
// coverage can be compared with FillShape::ellipse.
#include "shape.slang"

struct EllipseShape : ICustomShape {
    static float signedDistance(ShapeInput input) { return length(2 * (input.position / input.size) - 1) - 1; }
};
TESSERA_CUSTOM_SHAPE(EllipseShape)
