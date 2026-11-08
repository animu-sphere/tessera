// Owner fixture: returns color parameter 0 so coverage/blending can be compared with a solid DrawRect.
#include "procedural.slang"

struct SolidFill : IProceduralFill {
    static float4 shade(ProceduralInput input) { return input.parameter(0); }
};
TESSERA_PROCEDURAL_FILL(SolidFill)
