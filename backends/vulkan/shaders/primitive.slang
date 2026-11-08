#include "primitive_common.slang"

[[vk::binding(0, 0)]] Texture2D<float4> image;
[[vk::binding(1, 0)]] SamplerState imageSampler;
struct GradientStopData {
    float4 color; // Premultiplied linear-light color.
    float4 position;
};
[[vk::binding(0, 1)]] StructuredBuffer<GradientStopData> gradientStops;

[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID) { return makeVertex(id, primitive); }
struct InstanceInput {
    [[vk::location(0)]] float4 transform0 : TEXCOORD0;
    [[vk::location(1)]] float4 transform1 : TEXCOORD1;
    [[vk::location(2)]] float4 rect : TEXCOORD2;
    [[vk::location(3)]] float4 color : TEXCOORD3;
    [[vk::location(4)]] float4 widths : TEXCOORD4;
    [[vk::location(5)]] float4 source : TEXCOORD5;
    [[vk::location(6)]] float4 params : TEXCOORD6;
};
[shader("vertex")]
VertexOutput batchMain(uint id : SV_VulkanVertexID, InstanceInput input) {
    Primitive data;
    data.transform0 = input.transform0; data.transform1 = input.transform1;
    data.rect = input.rect; data.color = input.color; data.widths = input.widths;
    data.source = input.source; data.params = input.params;
    return makeVertex(id, data);
}
bool strokeContains(float2 p, float2 size, float radius, float4 stroke) {
    if (stroke.x == 0 || any(size == 0) || any(p < 0) || any(p >= size)) return false;
    float r = min(radius, min(size.x, size.y) * .5);
    if (!roundedContains(p, size, r)) return false;
    float width = min(stroke.x, min(size.x, size.y) * .5);
    float2 inner = size - 2 * width;
    if (all(inner > 0) && all(p >= width) && all(p < size - width) &&
        roundedContains(p - width, inner, max(0, r - width))) return false;
    if (stroke.z == 0) return true;
    p -= width * .5;
    size -= width;
    float cr = max(0, r - width * .5), a = size.x - 2 * cr, b = size.y - 2 * cr;
    float arc = cr * 1.5707963267948966, perimeter = 2 * a + 2 * b + 4 * arc;
    float position;
    if (p.x < cr && p.y < cr)
        position = perimeter - arc + cr * atan2(cr - p.y, cr - p.x);
    else if (p.x > size.x - cr && p.y < cr)
        position = a + cr * atan2(p.x - (size.x - cr), cr - p.y);
    else if (p.x > size.x - cr && p.y > size.y - cr)
        position = a + arc + b + cr * atan2(p.y - (size.y - cr), p.x - (size.x - cr));
    else if (p.x < cr && p.y > size.y - cr)
        position = 2 * a + 2 * arc + b + cr * atan2(cr - p.x, p.y - (size.y - cr));
    else {
        float4 distances = abs(float4(p.y, size.x - p.x, size.y - p.y, p.x));
        float nearest = min(min(distances.x, distances.y), min(distances.z, distances.w));
        if (nearest == distances.x) position = clamp(p.x - cr, 0, a);
        else if (nearest == distances.y) position = a + arc + clamp(p.y - cr, 0, b);
        else if (nearest == distances.z) position = a + 2 * arc + b + clamp(size.x - cr - p.x, 0, a);
        else position = 2 * a + 3 * arc + b + clamp(size.y - cr - p.y, 0, b);
    }
    float period = stroke.y + stroke.z;
    float at = fmod(fmod(fmod(position, perimeter), period) + stroke.w, period);
    return at < stroke.y;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    if (input.params.y == 11) {
        // Reconstruct from pixel centers, not raster-quantized quad interpolation.
        float covered = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                float2 local = pixelLocal(input, (float2(x,y) + .5) / 4 - .5);
                covered += strokeContains(local, input.rect.zw, input.params.x, input.widths) ? 1 : 0;
            }
        return float4(input.color.rgb, input.color.a * covered / 16);
    }
    if (input.params.y == 9 || input.params.y == 10) {
        bool inset = input.params.y == 10;
        if (inset && !roundedContains(input.local, input.rect.zw, input.params.x)) discard;
        float2 local = input.local - input.source.zw;
        if (input.source.x == 0) {
            if (roundedContains(local, input.rect.zw, input.params.x) == inset) discard;
            return input.color;
        }
        float r = min(input.params.x, min(input.rect.z, input.rect.w) * 0.5);
        float2 q = abs(local - input.rect.zw * 0.5) - (input.rect.zw * 0.5 - r);
        float2 positive = max(q, 0);
        float m = max(positive.x, positive.y);
        float distance = (m > 0 ? m * length(positive / m) : 0) + min(max(q.x, q.y), 0) - r;
        float t = saturate((inset ? -distance : distance) * input.source.y);
        float coverage = 1 - t * t * (3 - 2 * t);
        return float4(input.color.rgb, input.color.a * coverage);
    }
    if (input.params.y == 3) {
        uint2 cell = min(uint2(input.local / input.rect.zw * float2(5,7)), uint2(4,6));
        uint row = cell.y < 4 ? uint(input.widths[cell.y]) : uint(input.source[cell.y - 4]);
        if ((row & (1u << (4u - cell.x))) == 0) discard;
        return input.color;
    }
    float coverage = fillCoverage(input);
    if (input.params.y >= 5 && input.params.y <= 8) {
        float2 uv = input.local / input.rect.zw;
        // Clamping each distance component first preserves saturated length without overflow.
        float t = input.params.y >= 7
            ? saturate(length(clamp((uv - input.source.xy) * input.source.zw, -1.0, 1.0)))
            : saturate(dot(uv, input.source.xy) + input.source.z);
        float4 color;
        if (input.params.y == 5 || input.params.y == 7) {
            color = lerp(input.color, input.widths, t);
        } else {
            uint first = uint(input.widths.x), end = first + uint(input.widths.y);
            uint low = first, high = end;
            while (low < high) {
                uint middle = low + (high - low) / 2;
                if (gradientStops[middle].position.x <= t) low = middle + 1;
                else high = middle;
            }
            if (low == first) color = gradientStops[first].color;
            else if (low == end) color = gradientStops[end - 1].color;
            else {
                GradientStopData a = gradientStops[low - 1], b = gradientStops[low];
                color = lerp(a.color, b.color, (t - a.position.x) / (b.position.x - a.position.x));
            }
        }
        return float4(color.a > 0 ? color.rgb / color.a : float3(0,0,0), color.a * coverage);
    }
    if (input.params.y == 1) {
        float2 offset = input.widths.wx;
        float2 inner = input.rect.zw - offset - input.widths.yz;
        float radius = max(0, input.params.x - max(max(input.widths.x, input.widths.y),
                                                       max(input.widths.z, input.widths.w)));
        float2 p = input.local - offset;
        if (all(inner > 0) && all(p >= 0) && all(p < inner) && roundedContains(p, inner, radius)) discard;
    }
    return float4(input.color.rgb, input.color.a * coverage);
}
[shader("fragment")]
float4 imageMain(VertexOutput input) : SV_Target {
    float2 uv = input.source.xy + input.local / input.rect.zw * input.source.zw;
    float4 color = input.color * image.Sample(imageSampler, uv);
    // Premultiplied samples blend with source factor one, so the straight tint's alpha scales RGB here.
    if (input.params.y == 4) color.rgb *= input.color.a;
    return color;
}
