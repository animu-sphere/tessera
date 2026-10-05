struct Primitive {
    float4 transform0; // a, c, tx, device scale
    float4 transform1; // b, d, ty, unused
    float4 rect;
    float4 color; // Linear RGB, straight alpha.
    float4 widths; // Border edges, or placeholder bitmap rows 0-3.
    float4 source; // Image source rectangle, or placeholder bitmap rows 4-6.
    float4 params; // radius, mode (0 fill, 1 border, 2 image, 3 placeholder), framebuffer width, height
};
[[vk::push_constant]] ConstantBuffer<Primitive> primitive;
[[vk::binding(0, 0)]] Texture2D<float4> image;
[[vk::binding(1, 0)]] SamplerState imageSampler;

struct VertexOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float4 rect : TEXCOORD1;
    nointerpolation float4 color : TEXCOORD2;
    nointerpolation float4 widths : TEXCOORD3;
    nointerpolation float4 source : TEXCOORD4;
    nointerpolation float4 params : TEXCOORD5;
};
VertexOutput makeVertex(uint id, Primitive data) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1),
                               float2(0,1), float2(1,0), float2(1,1)};
    VertexOutput output;
    output.local = corners[id] * data.rect.zw;
    float2 p = output.local + data.rect.xy;
    float2 logical = float2(dot(data.transform0.xy, p) + data.transform0.z,
                            dot(data.transform1.xy, p) + data.transform1.z);
    output.position = float4(logical * data.transform0.w / data.params.zw * 2 - 1, 0, 1);
    output.rect = data.rect; output.color = data.color; output.widths = data.widths;
    output.source = data.source; output.params = data.params;
    return output;
}
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
bool roundedContains(float2 p, float2 size, float radius) {
    float r = min(radius, min(size.x, size.y) * 0.5);
    float2 q = abs(p - size * 0.5) - (size * 0.5 - r);
    float2 positive = max(q, 0);
    float m = max(positive.x, positive.y);
    float distance = m > 0 ? m * length(positive / m) : 0;
    return distance + min(max(q.x, q.y), 0) <= r;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    if (input.params.y == 3) {
        uint2 cell = min(uint2(input.local / input.rect.zw * float2(5,7)), uint2(4,6));
        uint row = cell.y < 4 ? uint(input.widths[cell.y]) : uint(input.source[cell.y - 4]);
        if ((row & (1u << (4u - cell.x))) == 0) discard;
        return input.color;
    }
    if (!roundedContains(input.local, input.rect.zw, input.params.x)) discard;
    if (input.params.y == 1) {
        float2 offset = input.widths.wx;
        float2 inner = input.rect.zw - offset - input.widths.yz;
        float radius = max(0, input.params.x - max(max(input.widths.x, input.widths.y),
                                                       max(input.widths.z, input.widths.w)));
        float2 p = input.local - offset;
        if (all(inner > 0) && all(p >= 0) && all(p < inner) && roundedContains(p, inner, radius)) discard;
    }
    return input.color;
}
[shader("fragment")]
float4 imageMain(VertexOutput input) : SV_Target {
    float2 uv = input.source.xy + input.local / input.rect.zw * input.source.zw;
    return input.color * image.Sample(imageSampler, uv);
}
