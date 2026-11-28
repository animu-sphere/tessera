// Owner fixture: the built-in ellipse equation as a distance-like inside/outside function, so its
// coverage can be compared with FillShape::ellipse. It declares no parameters.
#include "shape.slang"

struct EllipseShape : ICustomShape {
    struct Parameters {};
    static float signedDistance(ShapeInput input, Parameters parameters) {
        return length(2 * (input.position / input.size) - 1) - 1;
    }
};
TESSERA_CUSTOM_SHAPE(EllipseShape)
