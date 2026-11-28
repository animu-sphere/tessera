// Owner fixture: exact rounded-box distance with number parameter 0 as the corner radius, clamped
// like the built-in rounded rectangle so antialiased coverage can be compared with it.
#include "shape.slang"

struct RoundedShape : ICustomShape {
    static float signedDistance(ShapeInput input) {
        float r = min(input.parameter(0).x, min(input.size.x, input.size.y) * 0.5);
        float2 q = abs(input.position - input.size * 0.5) - (input.size * 0.5 - r);
        float2 positive = max(q, 0);
        float m = max(positive.x, positive.y);
        return (m > 0 ? m * length(positive / m) : 0) + min(max(q.x, q.y), 0) - r;
    }
};
TESSERA_CUSTOM_SHAPE(RoundedShape)
