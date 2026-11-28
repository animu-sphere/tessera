// Tessera custom shape ABI, profile sdf-v1: one signed-distance background shape, one pass, typed
// uniforms, no texture or storage-image access. A shape module includes this file, implements
// ICustomShape with its `Parameters` struct (see parameters.slang) and expands TESSERA_CUSTOM_SHAPE
// once to define the fragment entry point `shapeMain`.
//
// Coordinates: `position` is a sample point in the fill box's local logical units, before the
// current transform. signedDistance returns a distance in the same units; zero or negative is
// inside, and NaN is outside. Hard fills test the pixel center, antialiased fills the fixed 4x4
// grid; samples outside the half-open box [0, size) are never covered. Coverage reads only the sign,
// so an inexact distance (a bound or estimate) changes nothing as long as the sign is right.
// Paint: the command's solid color or gradient at the pixel center, as for built-in fills; coverage
// scales alpha once before the ordinary source-over blend.
#include "parameters.slang"

struct ShapeInput {
    float2 position;
    float2 size;
    float deviceScale; // Physical pixels per logical unit.
    float time;        // Host-supplied seconds from CustomShape::time; no clock is read.
};

interface ICustomShape {
    associatedtype Parameters; // Fields of ProceduralNumber, ProceduralPoint or ProceduralColor.
    static float signedDistance(ShapeInput input, Parameters parameters);
};

bool shapeCovers<S : ICustomShape>(inout ShapeInput input, S.Parameters parameters, float2 local) {
    if (any(local < 0) || any(local >= input.size)) return false;
    input.position = local;
    return S.signedDistance(input, parameters) <= 0;
}

float4 shapeFragment<S : ICustomShape>(VertexOutput vertex) {
    // Slot widths.z holds (time, parameter count); the parameters follow it.
    uint base = uint(vertex.widths.z);
    float4 header = tesseraFillData[base];
    ShapeInput input;
    input.position = float2(0);
    input.size = vertex.rect.zw;
    input.deviceScale = vertex.transform0.w;
    input.time = header.x;
    let parameters = tesseraLoadParameters<S.Parameters>(base + 1, uint(header.y));
    float coverage = 0;
    if ((uint(vertex.transform1.w) & 2u) != 0) {
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                coverage += shapeCovers<S>(input, parameters, pixelLocal(vertex, (float2(x, y) + .5) / 4 - .5)) ? 1 : 0;
        coverage /= 16;
    } else {
        coverage = shapeCovers<S>(input, parameters, pixelLocal(vertex, float2(0, 0))) ? 1 : 0;
    }
    if (coverage == 0) discard;
    // params.x: 0 solid color, 1 linear or 2 radial gradient stops at widths.xy.
    float4 color = vertex.color;
    if (vertex.params.x != 0) {
        float t = gradientParameter(pixelLocal(vertex, float2(0, 0)) / vertex.rect.zw, vertex.source,
                                    vertex.params.x == 2);
        float4 premultiplied = gradientStops(uint(vertex.widths.x), uint(vertex.widths.y), t);
        color = float4(premultiplied.a > 0 ? premultiplied.rgb / premultiplied.a : float3(0, 0, 0), premultiplied.a);
    }
    return float4(color.rgb, color.a * coverage);
}

#define TESSERA_CUSTOM_SHAPE(SHAPE)                                                                    \
    TESSERA_PARAMETER_DECLARATION(SHAPE.Parameters)                                                    \
    [shader("fragment")] float4 shapeMain(VertexOutput input) : SV_Target {                            \
        return shapeFragment<SHAPE>(input);                                                            \
    }
