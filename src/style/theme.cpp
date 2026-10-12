#include "theme_detail.hpp"
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace tessera {
namespace {

std::string at(std::string_view path, std::size_t index) {
    return std::string(path) + "/" + std::to_string(index);
}
void append(std::vector<Diagnostic>& to, std::vector<Diagnostic> from, std::string_view prefix = {}) {
    for (auto& d : from) {
        d.path.insert(0, prefix);
        to.push_back(std::move(d));
    }
}
void name_check(std::string_view name, const std::string& path, detail::Checker& check) {
    if (name.size() > max_theme_name_bytes)
        check.error("theme_name_bound", path, "Use at most 128 UTF-8 bytes for a theme variable name.");
    else if (!detail::valid_utf8(name)) check.error("invalid_utf8", path, "Use valid UTF-8 text.");
    else if (name.empty() || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        check.error("invalid_identifier", path, "Theme variable names must be nonempty and contain no control characters.");
}

enum class Assignment { ok, unknown, wrong_type };
// This table routes into the existing declaration contract rather than defining
// another numeric/unit policy. A null value performs only property lookup.
Assignment assign(StyleDeclarations& d, std::string_view property, const ThemeValue* value) {
#define FIELD(name, field) \
    if (property == name) { \
        if (!value) return Assignment::ok; \
        using T = typename decltype(d.field)::value_type; \
        if (const auto* typed = std::get_if<T>(value)) { d.field = *typed; return Assignment::ok; } \
        return Assignment::wrong_type; \
    }
    FIELD("display", display)
    FIELD("direction", direction)
    FIELD("justify", justify)
    FIELD("align", align)
    FIELD("overflow", overflow)
    FIELD("width", width)
    FIELD("height", height)
    FIELD("min_width", min_width)
    FIELD("min_height", min_height)
    FIELD("max_width", max_width)
    FIELD("max_height", max_height)
    FIELD("margin", margin)
    FIELD("border", border)
    FIELD("padding", padding)
    FIELD("gap", gap)
    FIELD("grow", grow)
    FIELD("shrink", shrink)
    FIELD("visibility", visibility)
    FIELD("opacity", opacity)
    FIELD("background", background)
    FIELD("border_color", border_color)
    FIELD("corner_radius", corner_radius)
    FIELD("text/font", text.font)
    FIELD("text/size", text.size)
    FIELD("text/line_height", text.line_height)
    FIELD("text/weight", text.weight)
    FIELD("color", color)
#undef FIELD
    return Assignment::unknown;
}
void value_check(const ThemeValue& value, const std::string& path, std::vector<Diagnostic>& errors) {
    // Float tokens permit zero and +infinity, with the destination's stricter
    // constraints checked on binding. Other types use their ordinary rules.
    constexpr std::array<std::string_view, 13> representative{
        "max_width", "background", "width", "padding", "text/font", "text/line_height",
        "text/weight", "display", "direction", "justify", "align", "overflow", "visibility"};
    StyleDeclarations d;
    const auto field = representative[value.index()];
    assign(d, field, &value);
    for (auto diagnostic : validate(d)) {
        diagnostic.path = path + diagnostic.path.substr(field.size() + 1);
        errors.push_back(std::move(diagnostic));
    }
}
void reference_check(const ThemeReference& r, const std::string& path, std::vector<Diagnostic>& errors) {
    detail::Checker check(errors);
    name_check(r.name, path + "/name", check);
    if (r.fallback) value_check(*r.fallback, path + "/fallback", errors);
}
bool binding_bound(const StyleSheet& sheet, std::size_t& remaining) {
    for (const auto& rule : sheet.rules) {
        if (rule.variables.size() > remaining) return false;
        remaining -= rule.variables.size();
    }
    return true;
}

class Variables {
public:
    Variables(const Theme* theme, std::vector<Diagnostic>& errors) : theme_(theme), errors_(errors) {
        if (theme) {
            values_.resize(theme->variables.size()); states_.resize(theme->variables.size());
            for (std::size_t i = 0; i < theme->variables.size(); ++i) indices_.emplace(theme->variables[i].name, i);
        }
    }
    void settle() {
        for (std::size_t i = 0; i < states_.size(); ++i) variable(i);
    }
    std::optional<ThemeValue> reference(const ThemeReference& r, const std::string& path) {
        const auto found = indices_.find(r.name);
        if (found == indices_.end()) {
            if (r.fallback) return r.fallback;
            detail::Checker(errors_).error("theme_missing", path + "/name",
                "Declare theme variable '" + r.name + "' or supply a literal fallback.");
            return {};
        }
        if (states_[found->second] == 1) {
            detail::Checker(errors_).error("theme_cycle", path + "/name",
                "Remove the alias cycle through theme variable '" + r.name + "'; fallback cannot hide a cycle.");
            return {};
        }
        auto value = variable(found->second);
        if (!value) detail::Checker(errors_).error("theme_unavailable", path + "/name",
            "Fix the invalid alias dependency of theme variable '" + r.name + "'.");
        return value;
    }
private:
    std::optional<ThemeValue> variable(std::size_t index) {
        if (states_[index] == 2) return values_[index];
        states_[index] = 1;
        const auto& input = theme_->variables[index].value;
        if (const auto* literal = std::get_if<ThemeValue>(&input)) values_[index] = *literal;
        else values_[index] = reference(std::get<ThemeReference>(input), at("/theme/variables", index) + "/value");
        states_[index] = 2;
        return values_[index];
    }
    const Theme* theme_;
    std::vector<Diagnostic>& errors_;
    std::map<std::string, std::size_t, std::less<>> indices_;
    std::vector<unsigned char> states_;
    std::vector<std::optional<ThemeValue>> values_;
};

std::vector<StyleDeclarations> materialize(const StyleSheet& sheet, Variables& variables,
                                         std::string_view base, std::vector<Diagnostic>& errors) {
    std::vector<StyleDeclarations> result;
    result.reserve(sheet.rules.size());
    for (std::size_t i = 0; i < sheet.rules.size(); ++i) {
        const auto& rule = sheet.rules[i];
        auto declarations = rule.declarations;
        const auto path = at(std::string(base) + "/rules", i);
        for (std::size_t j = 0; j < rule.variables.size(); ++j) {
            const auto& binding = rule.variables[j];
            const auto location = at(path + "/variables", j);
            auto value = variables.reference(binding.variable, location + "/variable");
            if (value && assign(declarations, binding.property, &*value) == Assignment::wrong_type)
                detail::Checker(errors).error("theme_type", location + "/variable",
                    "Use the exact declared style type for '" + binding.property + "'; values are not converted.");
        }
        append(errors, validate(declarations, path + "/declarations"));
        result.push_back(std::move(declarations));
    }
    return result;
}
} // namespace

namespace detail {
std::vector<Diagnostic> validate_theme_bindings(const StyleSheet& sheet) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    std::size_t remaining = max_theme_bindings;
    if (!binding_bound(sheet, remaining)) {
        check.error("theme_binding_bound", "/rules", "Use at most 4096 variable bindings across the candidate.");
        return errors;
    }
    for (std::size_t i = 0; i < sheet.rules.size(); ++i) {
        std::set<std::string, std::less<>> properties;
        const auto& bindings = sheet.rules[i].variables;
        for (std::size_t j = 0; j < bindings.size(); ++j) {
            const auto path = at(at("/rules", i) + "/variables", j);
            const auto& binding = bindings[j];
            StyleDeclarations d;
            if (assign(d, binding.property, nullptr) == Assignment::unknown)
                check.error("theme_property", path + "/property", "Use a ResolvedStyle field name; text fields use text/<field>.");
            else if (!properties.insert(binding.property).second)
                check.error("theme_duplicate_binding", path + "/property", "Bind each property at most once per rule.");
            name_check(binding.variable.name, path + "/variable/name", check);
            if (binding.variable.fallback) {
                const auto assignment = assign(d, binding.property, &*binding.variable.fallback);
                if (assignment == Assignment::wrong_type)
                    check.error("theme_type", path + "/variable/fallback", "Use the property's exact style type for its fallback.");
                else if (assignment == Assignment::ok) {
                    // Destination checks also apply to fallbacks that are not used.
                    for (auto diagnostic : validate(d)) {
                        diagnostic.path = path + "/variable/fallback" + diagnostic.path;
                        errors.push_back(std::move(diagnostic));
                    }
                }
                else value_check(*binding.variable.fallback, path + "/variable/fallback", errors);
            }
        }
    }
    return errors;
}

Result<PreparedStyles> prepare_theme(const Theme* theme, const StyleSheet& sheet) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    std::size_t remaining = max_theme_bindings;
    if (!binding_bound(sheet, remaining) || (theme && !binding_bound(theme->defaults, remaining)))
        check.error("theme_binding_bound", "/theme/bindings", "Use at most 4096 variable bindings shared by the theme and sheet.");
    if (theme && theme->variables.size() > max_theme_variables)
        check.error("theme_variable_bound", "/theme/variables", "Use at most 256 theme variables.");
    if (theme && theme->defaults.rules.size() > max_theme_rules)
        check.error("theme_rule_bound", "/theme/defaults/rules", "Use at most 1024 theme default rules.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (theme) {
        std::set<std::string, std::less<>> names;
        for (std::size_t i = 0; i < theme->variables.size(); ++i) {
            const auto& v = theme->variables[i];
            const auto path = at("/theme/variables", i);
            name_check(v.name, path + "/name", check);
            if (!names.insert(v.name).second)
                check.error("theme_duplicate_variable", path + "/name", "Declare each theme variable name once.");
            if (const auto* literal = std::get_if<ThemeValue>(&v.value)) value_check(*literal, path + "/value", errors);
            else reference_check(std::get<ThemeReference>(v.value), path + "/value", errors);
        }
        append(errors, validate(theme->defaults), "/theme/defaults");
    }
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    Variables variables(theme, errors);
    variables.settle();
    PreparedStyles result;
    if (theme) result.theme = materialize(theme->defaults, variables, "/theme/defaults", errors);
    result.sheet = materialize(sheet, variables, "/sheet", errors);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {std::move(result), {}};
}
} // namespace detail

std::vector<Diagnostic> validate(const Theme& theme) {
    return detail::prepare_theme(&theme, {}).diagnostics;
}
} // namespace tessera
