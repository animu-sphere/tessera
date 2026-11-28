// Build-check fixture: a plain float field has no declared slot kind.
#include "procedural.slang"

struct PlainFill : IProceduralFill {
    struct Parameters {
        float alpha;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) { return float4(parameters.alpha); }
};
TESSERA_PROCEDURAL_FILL(PlainFill)
