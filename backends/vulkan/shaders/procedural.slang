// Tessera procedural fill ABI, profile fill-v1: one background fill, one pass, typed uniforms, no
// texture or storage-image access. A fill module includes this file, implements IProceduralFill and
// expands TESSERA_PROCEDURAL_FILL once to define the fragment entry point `proceduralMain`.
//
// Coordinates: `position` is the pixel center in the fill box's local logical units, [0, size),
// before the current transform; ellipse/antialiased fills sample with the built-in 4x4 coverage.
// Colors: return linear-light RGB with straight alpha; results are clamped to [0, 1], then alpha is
// multiplied by coverage and ProceduralFill::opacity and blended source-over like solid fills.
#include "primitive_common.slang"

// Parameters share the frame's fill-data buffer; each typed value occupies one float4 slot.
[[vk::binding(0, 1)]] StructuredBuffer<float4> tesseraProceduralParameters;

struct ProceduralInput {
    float2 position;
    float2 size;
    float deviceScale; // Physical pixels per logical unit.
    float time;        // Host-supplied seconds from ProceduralFill::time; no clock is read.
    uint parameterBase;
    uint parameterCount;
    // Number: (v, 0, 0, 0). Point: (x, y, 0, 0). Color: linear RGB, straight alpha. Out of range: 0.
    float4 parameter(uint index) {
        return index < parameterCount ? tesseraProceduralParameters[parameterBase + index] : float4(0);
    }
};

interface IProceduralFill {
    static float4 shade(ProceduralInput input);
};

float4 proceduralFragment<F : IProceduralFill>(VertexOutput vertex) {
    float coverage = fillCoverage(vertex);
    ProceduralInput input;
    input.position = vertex.local;
    input.size = vertex.rect.zw;
    input.deviceScale = vertex.transform0.w;
    input.time = vertex.widths.z;
    input.parameterBase = uint(vertex.widths.x);
    input.parameterCount = uint(vertex.widths.y);
    float4 color = saturate(F.shade(input));
    return float4(color.rgb, color.a * coverage * vertex.widths.w);
}

#define TESSERA_PROCEDURAL_FILL(FILL)                                                                  \
    [shader("fragment")] float4 proceduralMain(VertexOutput input) : SV_Target {                       \
        return proceduralFragment<FILL>(input);                                                        \
    }
