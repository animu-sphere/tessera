struct Primitive {
    float4 transform0; // a, c, tx, device scale
    float4 transform1; // b, d, ty, unused
    float4 rect;
    float4 color; // Linear RGB, straight alpha.
    float4 widths; // top, right, bottom, left
    float4 source;
    float4 params; // radius, mode (0 fill, 1 border, 2 image), framebuffer width, height
};
[[vk::push_constant]] ConstantBuffer<Primitive> primitive;
[[vk::binding(0, 0)]] Texture2D<float4> image;
[[vk::binding(1, 0)]] SamplerState imageSampler;

struct VertexOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
};
[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID) {
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1),
                               float2(0,1), float2(1,0), float2(1,1)};
    VertexOutput output;
    output.local = corners[id] * primitive.rect.zw;
    float2 p = output.local + primitive.rect.xy;
    float2 logical = float2(dot(primitive.transform0.xy, p) + primitive.transform0.z,
                            dot(primitive.transform1.xy, p) + primitive.transform1.z);
    output.position = float4(logical * primitive.transform0.w / primitive.params.zw * 2 - 1, 0, 1);
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
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    if (!roundedContains(input.local, primitive.rect.zw, primitive.params.x)) discard;
    if (primitive.params.y == 1) {
        float2 offset = primitive.widths.wx;
        float2 inner = primitive.rect.zw - offset - primitive.widths.yz;
        float radius = max(0, primitive.params.x - max(max(primitive.widths.x, primitive.widths.y),
                                                       max(primitive.widths.z, primitive.widths.w)));
        float2 p = input.local - offset;
        if (all(inner > 0) && all(p >= 0) && all(p < inner) && roundedContains(p, inner, radius)) discard;
    }
    return primitive.color;
}
[shader("fragment")]
float4 imageMain(VertexOutput input) : SV_Target {
    float2 uv = primitive.source.xy + input.local / primitive.rect.zw * primitive.source.zw;
    return primitive.color * image.Sample(imageSampler, uv);
}
