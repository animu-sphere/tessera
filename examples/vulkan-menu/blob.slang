// Menu badge: a round blob whose rim ripples with host time. `lobes` counts lobes around the rim and
// `amplitude` is the rim amplitude as a fraction of the base radius; the base radius keeps the
// widest lobe inside the box. The result is a distance estimate, exact only where the rim is
// circular; sdf-v1 coverage reads only its sign. The smoke host evaluates the same formula
// independently; keep them in sync.
#include "shape.slang"

struct BlobShape : ICustomShape {
    struct Parameters {
        ProceduralNumber lobes;
        ProceduralNumber amplitude;
    };
    static float signedDistance(ShapeInput input, Parameters parameters) {
        float2 offset = input.position - input.size * 0.5;
        float lobes = parameters.lobes.value, amplitude = parameters.amplitude.value;
        float base = min(input.size.x, input.size.y) * 0.5 / (1 + amplitude);
        float rim = base * (1 + amplitude * sin(lobes * atan2(offset.y, offset.x) + input.time));
        return length(offset) - rim;
    }
};
TESSERA_CUSTOM_SHAPE(BlobShape)
