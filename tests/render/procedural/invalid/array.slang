// Build-check fixture: an array field is not one parameter slot.
#include "procedural.slang"

struct ArrayFill : IProceduralFill {
    struct Parameters {
        ProceduralColor colors[2];
    };
    static float4 shade(ProceduralInput input, Parameters parameters) { return parameters.colors[1].value; }
};
TESSERA_PROCEDURAL_FILL(ArrayFill)
