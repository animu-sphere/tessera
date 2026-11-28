// Menu badge: a round blob whose rim ripples with host time. Parameters: 0 number of lobes around
// the rim, 1 number rim amplitude as a fraction of the base radius; the base radius keeps the
// widest lobe inside the box. The result is a distance estimate, exact only where the rim is
// circular; sdf-v1 coverage reads only its sign. The smoke host evaluates the same formula
// independently; keep them in sync.
#include "shape.slang"

struct BlobShape : ICustomShape {
    static float signedDistance(ShapeInput input) {
        float2 offset = input.position - input.size * 0.5;
        float lobes = input.parameter(0).x, amplitude = input.parameter(1).x;
        float base = min(input.size.x, input.size.y) * 0.5 / (1 + amplitude);
        float rim = base * (1 + amplitude * sin(lobes * atan2(offset.y, offset.x) + input.time));
        return length(offset) - rim;
    }
};
TESSERA_CUSTOM_SHAPE(BlobShape)
