// Menu backdrop: a vertical two-color wash with one soft wave-shaped glow band. `glow` alpha scales
// its intensity, `cycles` counts waves across the width and `band` is (center, half-thickness) as
// height fractions. The smoke host evaluates the same formula independently; keep them in sync.
#include "procedural.slang"

struct AuroraFill : IProceduralFill {
    struct Parameters {
        ProceduralColor top;
        ProceduralColor bottom;
        ProceduralColor glow;
        ProceduralNumber cycles;
        ProceduralPoint band;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) {
        float2 uv = input.position / input.size;
        float4 top = parameters.top.value, bottom = parameters.bottom.value, glow = parameters.glow.value;
        float2 band = parameters.band.value;
        float center = band.x + band.y * 0.5 * sin(6.283185307 * (uv.x * parameters.cycles.value) + input.time);
        float d = (uv.y - center) / band.y;
        float intensity = exp(-d * d) * glow.a;
        float3 color = lerp(top.rgb, bottom.rgb, uv.y) + glow.rgb * intensity;
        return float4(color, 1);
    }
};
TESSERA_PROCEDURAL_FILL(AuroraFill)
