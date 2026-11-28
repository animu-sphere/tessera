// Owner fixture: encodes the ABI inputs as coverage. Inside is x < number `extent` + time and
// y < size.y * point `expected`'s y; the shape vanishes unless deviceScale equals that point's x.
#include "shape.slang"

struct ProbeShape : ICustomShape {
    struct Parameters {
        ProceduralNumber extent;
        ProceduralPoint expected;
    };
    static float signedDistance(ShapeInput input, Parameters parameters) {
        float2 expected = parameters.expected.value;
        if (abs(input.deviceScale - expected.x) > 1e-6) return 1;
        return max(input.position.x - (parameters.extent.value + input.time), input.position.y - input.size.y * expected.y);
    }
};
TESSERA_CUSTOM_SHAPE(ProbeShape)
