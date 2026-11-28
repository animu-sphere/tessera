// Owner fixture, profile fill-texture-v1: samples `image` at the fill's normalized position scaled by
// `repeat` (texture coordinates past 1 exercise the host sampler's addressing), multiplies the sample
// by `tint`, and adds `overlay` sampled at the same coordinates times its own weight. With `probe`
// nonzero it instead encodes size(): red/green are the first texture's width/height divided by 8.
#include "procedural.slang"

struct TextureFill : IProceduralFill {
    struct Parameters {
        ProceduralTexture image;
        ProceduralColor tint;
        ProceduralTexture overlay;
        ProceduralNumber weight;
        ProceduralPoint repeat;
        ProceduralNumber probe;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) {
        if (parameters.probe.value != 0) return float4(parameters.image.size() / 8, parameters.overlay.size().x / 8, 1);
        float2 uv = input.position / input.size * parameters.repeat.value;
        float4 base = parameters.image.sample(uv) * parameters.tint.value;
        float4 over = parameters.overlay.sample(uv) * parameters.weight.value;
        return float4(base.rgb + over.rgb * over.a, base.a);
    }
};
TESSERA_PROCEDURAL_FILL(TextureFill)
