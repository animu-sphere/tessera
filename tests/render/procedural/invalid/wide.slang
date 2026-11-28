// Build-check fixture: seventeen parameters exceed the 16-slot bound.
#include "procedural.slang"

struct WideFill : IProceduralFill {
    struct Parameters {
        ProceduralNumber p0;
        ProceduralNumber p1;
        ProceduralNumber p2;
        ProceduralNumber p3;
        ProceduralNumber p4;
        ProceduralNumber p5;
        ProceduralNumber p6;
        ProceduralNumber p7;
        ProceduralNumber p8;
        ProceduralNumber p9;
        ProceduralNumber p10;
        ProceduralNumber p11;
        ProceduralNumber p12;
        ProceduralNumber p13;
        ProceduralNumber p14;
        ProceduralNumber p15;
        ProceduralNumber p16;
    };
    static float4 shade(ProceduralInput input, Parameters parameters) { return float4(parameters.p16.value); }
};
TESSERA_PROCEDURAL_FILL(WideFill)
