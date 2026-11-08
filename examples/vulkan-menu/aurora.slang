// Menu backdrop: a vertical two-color wash with one soft wave-shaped glow band.
// Parameters: 0 top color, 1 bottom color, 2 glow color (alpha scales intensity),
// 3 number wave cycles across the width, 4 point (band center, band half-thickness) as height fractions.
// The smoke host evaluates the same formula independently; keep them in sync.
#include "procedural.slang"

struct AuroraFill : IProceduralFill {
    static float4 shade(ProceduralInput input) {
        float2 uv = input.position / input.size;
        float4 top = input.parameter(0), bottom = input.parameter(1), glow = input.parameter(2);
        float cycles = input.parameter(3).x;
        float2 band = input.parameter(4).xy;
        float center = band.x + band.y * 0.5 * sin(6.283185307 * (uv.x * cycles) + input.time);
        float d = (uv.y - center) / band.y;
        float intensity = exp(-d * d) * glow.a;
        float3 color = lerp(top.rgb, bottom.rgb, uv.y) + glow.rgb * intensity;
        return float4(color, 1);
    }
};
TESSERA_PROCEDURAL_FILL(AuroraFill)
