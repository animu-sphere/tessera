// Owner fixture: returns its color parameter so coverage/blending can be compared with a solid DrawRect.
#include "procedural.slang"

struct SolidFill : IProceduralFill {
    struct Parameters {
        ProceduralColor color;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) { return parameters.color.value; }
};
TESSERA_PROCEDURAL_FILL(SolidFill)
