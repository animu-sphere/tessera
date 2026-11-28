// Corner emblem, profile fill-texture-v1: square tesserae, one per texel of `tiles` sampled at its center,
// separated by `grout` joints `joint` cells wide. A `glint` (alpha is its strength) sweeps diagonally across
// the tile centers as time advances. The smoke host evaluates the same formula independently; keep them in sync.
#include "procedural.slang"

struct MosaicFill : IProceduralFill {
    struct Parameters {
        ProceduralTexture tiles;
        ProceduralColor grout;
        ProceduralNumber joint;
        ProceduralColor glint;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) {
        float2 cells = parameters.tiles.size();
        float2 cell = input.position / input.size * cells;
        float2 index = min(floor(cell), cells - 1);
        float2 within = cell - index;
        float edge = parameters.joint.value * 0.5;
        if (any(within < edge) || any(within > 1 - edge)) return parameters.grout.value;
        float2 center = (index + 0.5) / cells;
        float4 tile = parameters.tiles.sample(center);
        float sweep = pow(saturate(sin(3.14159265 * (center.x + center.y) - input.time)), 8);
        float4 glint = parameters.glint.value;
        return float4(tile.rgb + glint.rgb * glint.a * sweep, tile.a);
    }
};
TESSERA_PROCEDURAL_FILL(MosaicFill)
