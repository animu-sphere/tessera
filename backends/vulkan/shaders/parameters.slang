// Tessera shader parameters, shared by profiles fill-v1 and sdf-v1. A fill or shape declares an
// associated `Parameters` struct whose fields, in slot order, each have one of the three slot types
// below; the ABI loads it from the frame's fill data and passes it to the shader. At build time
// tessera_add_procedural_fill/tessera_add_custom_shape reflect the struct's field names and types
// into a parameter manifest that the host registers with the artifact, so the declaration has
// one source. Fields of other types, arrays, nested structs or more than 16 fields fail the build.
#include "primitive_common.slang"

// Each slot type is one float4 slot of the request's typed values.
struct ProceduralNumber {
    float value;
    float3 reserved; // Zero.
};
struct ProceduralPoint {
    float2 value; // Logical units for positions, or any two numbers.
    float2 reserved; // Zero.
};
struct ProceduralColor {
    float4 value; // Linear-light RGB, straight alpha.
};

// Registration and submission guarantee `count` equals the declared field count; other slots read zero.
P tesseraLoadParameters<P>(uint base, uint count) {
    float4 slots[16];
    for (uint i = 0; i < 16; ++i)
        slots[i] = i < count ? tesseraFillData[base + i] : float4(0);
    return reinterpret<P>(slots);
}

// Reflection anchor read only by the build-time manifest step. Nothing references it, so the
// compiler removes it and the artifact binds no resource for it.
#define TESSERA_PARAMETER_DECLARATION(PARAMETERS) uniform PARAMETERS tesseraParameterDeclaration;
