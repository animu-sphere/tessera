// Owner fixture: encodes the ABI inputs. Red/green are the normalized local position, blue is
// frac(time) and alpha is number parameter 1; point parameter 0 offsets the position in logical units.
#include "procedural.slang"

struct ProbeFill : IProceduralFill {
    static float4 shade(ProceduralInput input) {
        float2 uv = (input.position + input.parameter(0).xy) / input.size;
        return float4(uv, frac(input.time), input.parameter(1).x);
    }
};
TESSERA_PROCEDURAL_FILL(ProbeFill)
