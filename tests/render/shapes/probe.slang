// Owner fixture: encodes the ABI inputs as coverage. Inside is x < number parameter 0 + time and
// y < size.y * point parameter 1's y; the shape vanishes unless deviceScale equals that point's x.
#include "shape.slang"

struct ProbeShape : ICustomShape {
    static float signedDistance(ShapeInput input) {
        float4 expected = input.parameter(1);
        if (abs(input.deviceScale - expected.x) > 1e-6) return 1;
        return max(input.position.x - (input.parameter(0).x + input.time), input.position.y - input.size.y * expected.y);
    }
};
TESSERA_CUSTOM_SHAPE(ProbeShape)
