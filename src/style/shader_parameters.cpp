#include <tessera/style/shader_parameters.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;
constexpr std::array<std::string_view, 2> profiles{"fill-v1", "sdf-v1"};
constexpr std::array<std::string_view, 3> kinds{"number", "point", "color"};
struct Failure {
    std::vector<Diagnostic> diagnostics;
};

[[noreturn]] void fail(std::string code, std::string path, std::string message) {
    throw Failure{{{std::move(code), Severity::error, std::move(path), std::move(message), {}}}};
}
template<class T> const T& as(const JsonValue& value, const std::string& path) {
    if (const auto* found = std::get_if<T>(&value.value)) return *found;
    fail("schema_type", path, "Use the type declared by shader parameter manifest JSON v1.");
}
const Object& object(const JsonValue& value, const std::string& path, std::initializer_list<std::string_view> fields) {
    const auto& result = as<Object>(value, path);
    for (const auto& [key, child] : result) {
        (void)child;
        if (std::find(fields.begin(), fields.end(), key) == fields.end())
            fail("unknown_field", path + "/" + detail::pointer_token(key), "Remove this unknown manifest field.");
    }
    for (const auto field : fields)
        if (!result.contains(field)) fail("missing_field", path + "/" + std::string(field), "Supply this field.");
    return result;
}
template<class E, std::size_t N>
E choice(const JsonValue& value, const std::string& path, const std::array<std::string_view, N>& names) {
    const auto& text = as<std::string>(value, path);
    for (std::size_t i = 0; i < names.size(); ++i)
        if (text == names[i]) return static_cast<E>(i);
    fail("unknown_value", path, "Use a declared manifest enumeration name.");
}
bool identifier(std::string_view name) {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    return !name.empty() && name.size() <= max_shader_parameter_name_bytes && letter(name.front()) &&
           std::all_of(name.begin(), name.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

ShaderParameters read(const JsonValue& json) {
    const auto& root = object(json, "", {"format", "version", "profile", "parameters"});
    if (as<std::string>(root.at("format"), "/format") != "tessera-shader-parameters")
        fail("unknown_format", "/format", "Use format tessera-shader-parameters.");
    if (as<double>(root.at("version"), "/version") != 1)
        fail("unsupported_version", "/version", "Use shader parameter manifest version 1.");
    ShaderParameters result;
    result.profile = choice<ShaderProfile>(root.at("profile"), "/profile", profiles);
    const auto& parameters = as<Array>(root.at("parameters"), "/parameters");
    if (parameters.size() > max_procedural_parameters)
        fail("parameter_limit", "/parameters", "Declare at most 16 shader parameters.");
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto at = "/parameters/" + std::to_string(i);
        const auto& entry = object(parameters[i], at, {"name", "kind"});
        result.parameters.push_back({as<std::string>(entry.at("name"), at + "/name"),
                                     choice<ProceduralValueKind>(entry.at("kind"), at + "/kind", kinds)});
    }
    return result;
}

} // namespace

std::vector<Diagnostic> validate(const ShaderParameters& declaration) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    check.enumeration(declaration.profile, ShaderProfile::sdf_v1, "/profile");
    if (declaration.parameters.size() > max_procedural_parameters)
        check.error("parameter_limit", "/parameters", "Declare at most 16 shader parameters.");
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
        const auto& parameter = declaration.parameters[i];
        const auto at = "/parameters/" + std::to_string(i);
        if (!identifier(parameter.name))
            check.error("invalid_identifier", at + "/name",
                        "Use an ASCII identifier of 1-64 bytes: a letter or '_', then letters, digits or '_'.");
        else if (!names.insert(parameter.name).second)
            check.error("duplicate_parameter", at + "/name", "Declare each parameter name once.");
        check.enumeration(parameter.kind, ProceduralValueKind::color, at + "/kind");
    }
    return errors;
}

Result<ShaderParameters> load_shader_parameters(std::string_view source) {
    auto json = detail::parse_json(source, max_serialized_shader_parameters_bytes, 1024);
    if (!json) return {std::nullopt, std::move(json.diagnostics)};
    try {
        auto declaration = read(json.value->root);
        auto errors = validate(declaration);
        if (!errors.empty()) {
            detail::annotate_json(errors, *json.value);
            return {std::nullopt, std::move(errors)};
        }
        return {std::move(declaration), {}};
    } catch (Failure& failure) {
        detail::annotate_json(failure.diagnostics, *json.value);
        return {std::nullopt, std::move(failure.diagnostics)};
    }
}

Result<std::string> save_shader_parameters(const ShaderParameters& declaration) {
    auto errors = validate(declaration);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    Array parameters;
    for (const auto& parameter : declaration.parameters)
        parameters.push_back(JsonValue{Object{{"name", JsonValue{parameter.name}},
                                              {"kind", JsonValue{std::string(kinds[std::size_t(parameter.kind)])}}}});
    const JsonValue root{Object{{"format", JsonValue{std::string("tessera-shader-parameters")}},
                                {"version", JsonValue{1.0}},
                                {"profile", JsonValue{std::string(profiles[std::size_t(declaration.profile)])}},
                                {"parameters", JsonValue{std::move(parameters)}}}};
    return {detail::write_json_value(root) + '\n', {}};
}

std::optional<std::size_t> find_parameter(const ShaderParameters& declaration, std::string_view name) {
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i)
        if (declaration.parameters[i].name == name) return i;
    return std::nullopt;
}

} // namespace tessera
