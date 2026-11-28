// Owner fixture: encodes the ABI inputs. Red/green are the normalized local position, blue is
// frac(time) and alpha is the number parameter; the point parameter offsets the position in logical units.
#include "procedural.slang"

struct ProbeFill : IProceduralFill {
    struct Parameters {
        ProceduralPoint offset;
        ProceduralNumber alpha;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) {
        float2 uv = (input.position + parameters.offset.value) / input.size;
        return float4(uv, frac(input.time), parameters.alpha.value);
    }
};
TESSERA_PROCEDURAL_FILL(ProbeFill)
