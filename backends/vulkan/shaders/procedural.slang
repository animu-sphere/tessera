// Tessera procedural fill ABI, profiles fill-v1 and fill-texture-v1: one background fill, one pass and
// typed uniforms. A fill module includes this file, implements IProceduralFill with its `Parameters`
// struct (see parameters.slang) and expands TESSERA_PROCEDURAL_FILL once to define the fragment entry
// point `proceduralMain`. A fill whose Parameters declare no ProceduralTexture field is fill-v1 and has
// no image, sampler or storage access; one to three ProceduralTexture fields make it fill-texture-v1,
// which samples those host-bound images and nothing else.
//
// Coordinates: `position` is the pixel center in the fill box's local logical units, [0, size),
// before the current transform; ellipse/antialiased fills sample with the built-in 4x4 coverage.
// Colors: return linear-light RGB with straight alpha; results are clamped to [0, 1], then alpha is
// multiplied by coverage and ProceduralFill::opacity and blended source-over like solid fills.
#include "parameters.slang"

struct ProceduralInput {
    float2 position;
    float2 size;
    float deviceScale; // Physical pixels per logical unit.
    float time;        // Host-supplied seconds from ProceduralFill::time; no clock is read.
};

interface IProceduralFill {
    associatedtype Parameters; // Fields of ProceduralNumber, ProceduralPoint, ProceduralColor or ProceduralTexture.
    static float4 shade(ProceduralInput input, Parameters parameters);
};

// fill-texture-v1: the k-th texture parameter is bound at set 0, 2 or 3 (set 1 holds fill data). The
// renderer binds every set, repeating the first texture for absent ones, because sample() reaches all
// three. fill-v1 artifacts never call sample(), so the compiler removes these declarations.
[[vk::binding(0, 0)]] Texture2D<float4> tesseraTexture0;
[[vk::binding(1, 0)]] SamplerState tesseraSampler0;
[[vk::binding(0, 2)]] Texture2D<float4> tesseraTexture1;
[[vk::binding(1, 2)]] SamplerState tesseraSampler1;
[[vk::binding(0, 3)]] Texture2D<float4> tesseraTexture2;
[[vk::binding(1, 3)]] SamplerState tesseraSampler2;

// One host image, requested by an ImageHandle parameter. Its slot holds the texture's binding number
// (0-2, in parameter order) and 1 when the image is bound with premultiplied alpha.
struct ProceduralTexture {
    float index;
    float premultiplied;
    float2 reserved; // Zero.
    // Level 0 of the image at normalized coordinates `uv`, filtered and addressed by the host's sampler:
    // RGB as the view decodes it (linear light for an sRGB view) with straight alpha. Premultiplied
    // images are filtered premultiplied, then divided by alpha; transparent texels read zero RGB.
    float4 sample(float2 uv) {
        uint i = uint(index);
        float4 color;
        if (i == 0) color = tesseraTexture0.SampleLevel(tesseraSampler0, uv, 0);
        else if (i == 1) color = tesseraTexture1.SampleLevel(tesseraSampler1, uv, 0);
        else color = tesseraTexture2.SampleLevel(tesseraSampler2, uv, 0);
        if (premultiplied != 0) color.rgb = color.a > 0 ? color.rgb / color.a : float3(0);
        return color;
    }
    // Level-0 width and height in texels.
    float2 size() {
        uint i = uint(index), width, height;
        if (i == 0) tesseraTexture0.GetDimensions(width, height);
        else if (i == 1) tesseraTexture1.GetDimensions(width, height);
        else tesseraTexture2.GetDimensions(width, height);
        return float2(width, height);
    }
};

float4 proceduralFragment<F : IProceduralFill>(VertexOutput vertex) {
    float coverage = fillCoverage(vertex);
    ProceduralInput input;
    input.position = vertex.local;
    input.size = vertex.rect.zw;
    input.deviceScale = vertex.transform0.w;
    input.time = vertex.widths.z;
    // Slots widths.xy hold the parameter base and count.
    let parameters = tesseraLoadParameters<F.Parameters>(uint(vertex.widths.x), uint(vertex.widths.y));
    float4 color = saturate(F.shade(input, parameters));
    return float4(color.rgb, color.a * coverage * vertex.widths.w);
}

#define TESSERA_PROCEDURAL_FILL(FILL)                                                                  \
    TESSERA_PARAMETER_DECLARATION(FILL.Parameters)                                                     \
    [shader("fragment")] float4 proceduralMain(VertexOutput input) : SV_Target {                       \
        return proceduralFragment<FILL>(input);                                                        \
    }
