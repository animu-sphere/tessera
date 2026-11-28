// Four ProceduralTexture fields exceed fill-texture-v1's three texture bindings.
#include "procedural.slang"

struct ManyTextures : IProceduralFill {
    struct Parameters {
        ProceduralTexture a;
        ProceduralTexture b;
        ProceduralTexture c;
        ProceduralTexture d;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) {
        return parameters.a.sample(input.position) + parameters.d.sample(input.position);
    }
};
TESSERA_PROCEDURAL_FILL(ManyTextures)
