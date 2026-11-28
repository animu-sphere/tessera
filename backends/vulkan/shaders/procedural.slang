// Tessera procedural fill ABI, profile fill-v1: one background fill, one pass, typed uniforms, no
// texture or storage-image access. A fill module includes this file, implements IProceduralFill with
// its `Parameters` struct (see parameters.slang) and expands TESSERA_PROCEDURAL_FILL once to define
// the fragment entry point `proceduralMain`.
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
    associatedtype Parameters; // Fields of ProceduralNumber, ProceduralPoint or ProceduralColor.
    static float4 shade(ProceduralInput input, Parameters parameters);
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
