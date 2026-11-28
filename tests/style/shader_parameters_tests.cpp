#include <tessera/style/shader_parameters.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
using Kind = ProceduralValueKind;
// The canonical form the build-time manifest step writes beside each artifact.
constexpr std::string_view aurora =
    R"({"format":"tessera-shader-parameters","parameters":[{"kind":"color","name":"top"},)"
    R"({"kind":"number","name":"cycles"},{"kind":"point","name":"band"}],"profile":"fill-v1","version":1})"
    "\n";

std::string manifest(std::string_view profile, std::string_view parameters, std::string_view extra = "") {
    return std::string(R"({"format":"tessera-shader-parameters","version":1,"profile":")") + std::string(profile) +
           R"(","parameters":[)" + std::string(parameters) + "]" + std::string(extra) + "}";
}

void canonical_round_trip() {
    const auto loaded = load_shader_parameters(aurora);
    check(bool(loaded), "A canonical manifest loads");
    const ShaderParameters expected{ShaderProfile::fill_v1,
                                    {{"top", Kind::color}, {"cycles", Kind::number}, {"band", Kind::point}}};
    check(*loaded.value == expected, "Names, kinds, slot order and profile are read");
    const auto saved = save_shader_parameters(*loaded.value);
    check(saved && *saved.value == aurora, "Saving writes the canonical bytes");
    check(find_parameter(expected, "band") == 2u && !find_parameter(expected, "Band"),
          "Lookup is exact and returns the slot index");

    const auto empty = load_shader_parameters(manifest("sdf-v1", ""));
    check(empty && empty.value->profile == ShaderProfile::sdf_v1 && empty.value->parameters.empty(),
          "A shape may declare no parameters");
    std::string sixteen;
    for (int i = 0; i < 16; ++i)
        sixteen += std::string(i ? "," : "") + R"({"name":"p)" + std::to_string(i) + R"(","kind":"number"})";
    check(bool(load_shader_parameters(manifest("fill-v1", sixteen))), "Sixteen parameters are accepted");
    check(has(load_shader_parameters(manifest("fill-v1", sixteen + R"(,{"name":"q","kind":"number"})")).diagnostics,
              "parameter_limit", "/parameters"),
          "A seventeenth parameter is rejected");

    constexpr std::string_view textured =
        R"({"format":"tessera-shader-parameters","parameters":[{"kind":"texture","name":"image"},)"
        R"({"kind":"color","name":"tint"},{"kind":"texture","name":"overlay"}],"profile":"fill-texture-v1",)"
        R"("version":1})"
        "\n";
    const auto loaded_textures = load_shader_parameters(textured);
    check(loaded_textures &&
              *loaded_textures.value ==
                  ShaderParameters{ShaderProfile::fill_texture_v1,
                                   {{"image", Kind::texture}, {"tint", Kind::color}, {"overlay", Kind::texture}}},
          "Texture parameters load in fill-texture-v1");
    const auto saved_textures = save_shader_parameters(*loaded_textures.value);
    check(saved_textures && *saved_textures.value == textured, "Texture declarations save canonically");
    const auto texture = R"({"name":"t0","kind":"texture"})";
    check(bool(load_shader_parameters(
              manifest("fill-texture-v1",
                       std::string(texture) + R"(,{"name":"t1","kind":"texture"},{"name":"t2","kind":"texture"})"))),
          "Three textures are accepted");
}

void located_rejection() {
    const auto rejects = [](const std::string& json, std::string_view code, std::string_view path) {
        const auto result = load_shader_parameters(json);
        return !result && has(result.diagnostics, code, path) && result.diagnostics.front().byte_offset;
    };
    const auto one = R"({"name":"glow","kind":"color"})";
    check(rejects(R"({"format":"other","version":1,"profile":"fill-v1","parameters":[]})", "unknown_format", "/format"),
          "Another format is rejected");
    check(rejects(R"({"format":"tessera-shader-parameters","version":2,"profile":"fill-v1","parameters":[]})",
                  "unsupported_version", "/version"),
          "Another version is rejected");
    check(rejects(manifest("fill-v2", one), "unknown_value", "/profile"), "Unknown profiles are rejected");
    check(rejects(manifest("fill-v1", R"({"name":"glow","kind":"vector"})"), "unknown_value", "/parameters/0/kind"),
          "Unknown kinds are rejected");
    check(rejects(manifest("fill-v1", R"({"name":"glow"})"), "missing_field", "/parameters/0/kind"),
          "Kinds are required");
    check(rejects(manifest("fill-v1", R"({"name":"glow","kind":"color","default":1})"), "unknown_field",
                  "/parameters/0/default"),
          "Unknown parameter fields are rejected");
    check(rejects(manifest("fill-v1", one, R"(,"entry":"main")"), "unknown_field", "/entry"),
          "Unknown root fields are rejected");
    check(rejects(manifest("fill-v1", R"({"name":3,"kind":"color"})"), "schema_type", "/parameters/0/name"),
          "Names are strings");
    check(rejects(manifest("fill-v1", std::string(one) + "," + one), "duplicate_parameter", "/parameters/1/name"),
          "Names are unique");
    for (const auto* name : {"", "2d", "glow color", "gl\xc3\xb6w"})
        check(rejects(manifest("fill-v1", std::string(R"({"name":")") + name + R"(","kind":"color"})"),
                      "invalid_identifier", "/parameters/0/name"),
              "Names are ASCII identifiers");
    check(bool(load_shader_parameters(manifest("fill-v1", R"({"name":"_glow2","kind":"color"})"))),
          "Underscores and digits after the first byte are identifiers");
    const std::string long_name(max_shader_parameter_name_bytes + 1, 'a');
    check(rejects(manifest("fill-v1", R"({"name":")" + long_name + R"(","kind":"color"})"), "invalid_identifier",
                  "/parameters/0/name"),
          "Names have at most 64 bytes");
    check(!load_shader_parameters("{"), "Malformed JSON is rejected");
    const auto texture = R"({"name":"image","kind":"texture"})";
    for (const auto* profile : {"fill-v1", "sdf-v1"})
        check(rejects(manifest(profile, texture), "texture_profile", "/parameters/0/kind"),
              "Texture parameters belong to fill-texture-v1 only");
    check(rejects(manifest("fill-texture-v1", one), "texture_limit", "/parameters"),
          "fill-texture-v1 declares at least one texture");
    std::string four;
    for (int i = 0; i < 4; ++i)
        four += std::string(i ? "," : "") + R"({"name":"t)" + std::to_string(i) + R"(","kind":"texture"})";
    check(rejects(manifest("fill-texture-v1", four), "texture_limit", "/parameters"),
          "fill-texture-v1 declares at most three textures");

    ShaderParameters invalid{static_cast<ShaderProfile>(3), {{"a", static_cast<Kind>(4)}}};
    const auto errors = validate(invalid);
    check(has(errors, "unknown_value", "/profile") && has(errors, "unknown_value", "/parameters/0/kind"),
          "Validation rejects out-of-range enums");
    check(!save_shader_parameters(invalid), "Saving validates first");
}
} // namespace

int main() {
    try {
        canonical_round_trip();
        located_rejection();
        std::cout << "Shader parameter checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
