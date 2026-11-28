struct Primitive {
    float4 transform0; // a, c, tx, device scale
    float4 transform1; // b, d, ty, fill flags (1 ellipse, 2 smooth)
    float4 rect;
    float4 color; // Linear RGB, straight alpha.
    float4 widths; // Border edges, bitmap rows, gradient color/stops, stroke width/dash/gap/phase, or procedural parameter base/count/time/opacity.
    float4 source; // Image crop, bitmap rows, gradient, shadow parameters, or stroke coverage fringe.
    float4 params; // radius or custom-shape paint, mode (0 fill, 1 border, 2 image, 3 placeholder, 4 premultiplied image, 5/6 linear, 7/8 radial, 9 outer/10 inset shadow, 11 stroke, 12 procedural, 13 custom shape), framebuffer width, height
};
[[vk::push_constant]] ConstantBuffer<Primitive> primitive;
// Frame fill data: each gradient stop record is two slots (premultiplied linear-light color, then
// its offset in x); procedural and custom-shape parameters occupy one slot per typed value (parameters.slang).
[[vk::binding(0, 1)]] StructuredBuffer<float4> tesseraFillData;

struct VertexOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float4 rect : TEXCOORD1;
    nointerpolation float4 color : TEXCOORD2;
    nointerpolation float4 widths : TEXCOORD3;
    nointerpolation float4 source : TEXCOORD4;
    nointerpolation float4 params : TEXCOORD5;
    nointerpolation float4 transform0 : TEXCOORD6;
    nointerpolation float4 transform1 : TEXCOORD7;
};
VertexOutput makeVertex(uint id, Primitive data) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1),
                               float2(0,1), float2(1,0), float2(1,1)};
    VertexOutput output;
    output.local = corners[id] * data.rect.zw;
    if (data.params.y == 9)
        output.local = corners[id] * (data.rect.zw + 2 * data.source.x) - data.source.x;
    if (data.params.y == 11)
        output.local = corners[id] * (data.rect.zw + 2 * data.source.xy) - data.source.xy;
    if ((uint(data.transform1.w) & 2u) != 0) {
        float det = (data.transform0.x * data.transform1.y - data.transform0.y * data.transform1.x) * data.transform0.w;
        float2 fringe = float2(abs(data.transform1.y) + abs(data.transform0.y),
                               abs(data.transform1.x) + abs(data.transform0.x)) / abs(det) * .5;
        output.local = corners[id] * (data.rect.zw + 2 * fringe) - fringe;
    }
    float2 p = output.local + data.rect.xy;
    float2 logical = float2(dot(data.transform0.xy, p) + data.transform0.z,
                            dot(data.transform1.xy, p) + data.transform1.z);
    output.position = float4(logical * data.transform0.w / data.params.zw * 2 - 1, 0, 1);
    output.rect = data.rect; output.color = data.color; output.widths = data.widths;
    output.source = data.source; output.params = data.params;
    output.transform0 = data.transform0; output.transform1 = data.transform1;
    return output;
}
bool roundedContains(float2 p, float2 size, float radius) {
    float r = min(radius, min(size.x, size.y) * 0.5);
    float2 q = abs(p - size * 0.5) - (size * 0.5 - r);
    float2 positive = max(q, 0);
    float m = max(positive.x, positive.y);
    float distance = m > 0 ? m * length(positive / m) : 0;
    return distance + min(max(q.x, q.y), 0) <= r;
}
bool fillContains(float2 p, float2 size, float radius, bool ellipse) {
    if (any(size == 0)) return false;
    return ellipse ? length(2 * (p / size) - 1) <= 1 : roundedContains(p, size, radius);
}
float2 pixelLocal(VertexOutput input, float2 offset) {
    float4 t0 = input.transform0, t1 = input.transform1;
    float det = t0.x * t1.y - t0.y * t1.x;
    float2 p = (input.position.xy + offset) / t0.w - float2(t0.z, t1.z);
    return float2(t1.y * p.x - t0.y * p.y, -t1.x * p.x + t0.x * p.y) / det - input.rect.xy;
}
// Gradient parameter at normalized box position `uv`: linear projection (source.xy, offset
// source.z) or radial distance (center source.xy, inverse radii source.zw), clamped to [0, 1].
float gradientParameter(float2 uv, float4 source, bool radial) {
    // Clamping each distance component first preserves saturated length without overflow.
    return radial ? saturate(length(clamp((uv - source.xy) * source.zw, -1.0, 1.0)))
                  : saturate(dot(uv, source.xy) + source.z);
}
// Premultiplied color of `count` stop records starting at record `first`, clamped at both ends.
float4 gradientStops(uint first, uint count, float t) {
    uint end = first + count, low = first, high = end;
    while (low < high) {
        uint middle = low + (high - low) / 2;
        if (tesseraFillData[2 * middle + 1].x <= t) low = middle + 1;
        else high = middle;
    }
    if (low == first) return tesseraFillData[2 * first];
    if (low == end) return tesseraFillData[2 * (end - 1)];
    float a = tesseraFillData[2 * (low - 1) + 1].x, b = tesseraFillData[2 * low + 1].x;
    return lerp(tesseraFillData[2 * (low - 1)], tesseraFillData[2 * low], (t - a) / (b - a));
}
// Background fill coverage shared by built-in and procedural fills: hard or 4x4 sampled
// rectangle/ellipse membership. Discards uncovered pixels and leaves input.local at the pixel center.
float fillCoverage(inout VertexOutput input) {
    float coverage = 1;
    bool ellipse = (uint(input.transform1.w) & 1u) != 0;
    if (ellipse) input.local = pixelLocal(input, float2(0,0));
    if ((uint(input.transform1.w) & 2u) != 0) {
        coverage = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                float2 local = pixelLocal(input, (float2(x,y) + .5) / 4 - .5);
                if (all(local >= 0) && all(local < input.rect.zw))
                    coverage += fillContains(local, input.rect.zw, input.params.x, ellipse) ? 1 : 0;
            }
        coverage /= 16;
        if (coverage == 0) discard;
        input.local = pixelLocal(input, float2(0,0));
    } else if (!fillContains(input.local, input.rect.zw, input.params.x, ellipse)) discard;
    return coverage;
}
