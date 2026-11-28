#include <tessera/inspection/capabilities.hpp>
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::array<std::string_view, capability_operation_count> operation_names{
    "inspect_tree",  "inspect_properties",  "semantic_action", "invoke_command", "replay_input",
    "capture_frame", "evaluate_invariants", "edit_source",     "reload",         "compare_snapshots"};
constexpr std::array<std::string_view, 2> precondition_names{"generation", "target"};
constexpr std::array<std::string_view, 3> status_names{"supported", "not_declared", "unavailable"};
constexpr std::array<std::string_view, 3> availability_names{"both", "development", "production"};
constexpr std::array<std::string_view, 2> mode_names{"development", "production"};
constexpr std::array<std::string_view, 5> parameter_types{"boolean", "number", "string", "enumeration", "object_id"};
// Semantic JSON v1 role spellings, in SemanticRole order.
constexpr std::array<std::string_view, 27> role_names{
    "root",      "button",    "text",       "separator",    "dialog",       "list",        "tree",
    "list_item", "tree_item", "menu",       "menu_item",    "scrollbar",    "radio_group", "radio",
    "checkbox",  "switch",    "text_input", "number_input", "tab_list",     "tab",         "toolbar",
    "combobox",  "listbox",   "option",     "group",        "color_picker", "text_editor"};

void error(std::vector<Diagnostic>& out, std::string code, std::string path, std::string message) {
    out.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
bool identity(std::string_view value) {
    return !value.empty() && value.size() <= max_capability_text_bytes && detail::valid_utf8(value) &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
void check_identity(std::vector<Diagnostic>& out, std::string_view value, std::string path) {
    if (!identity(value))
        error(out, "invalid_identifier", std::move(path), "Use 1-256 UTF-8 bytes without ASCII control characters.");
}
bool available(CommandAvailability availability, CommandMode mode) {
    return availability == CommandAvailability::both ||
           (availability == CommandAvailability::development && mode == CommandMode::development) ||
           (availability == CommandAvailability::production && mode == CommandMode::production);
}
template<std::size_t N> bool declared(auto value, const std::array<std::string_view, N>&) {
    return static_cast<std::size_t>(value) < N;
}

std::vector<Diagnostic> validate(const CapabilityInput& input) {
    std::vector<Diagnostic> out;
    check_identity(out, input.producer, "/producer");
    if (!input.commands) error(out, "missing_input", "/commands", "Supply the generation's command snapshot.");
    if (!input.semantics) {
        error(out, "missing_input", "/semantics", "Supply the generation's semantic snapshot.");
    } else {
        for (auto diagnostic : validate(*input.semantics)) {
            diagnostic.path = "/semantics" + diagnostic.path;
            out.push_back(std::move(diagnostic));
        }
        if (input.commands && input.semantics->generation != input.commands->generation())
            error(out, "capability_mismatch", "/semantics/generation",
                  "Capture commands and semantics from the same host generation.");
    }
    std::array<bool, capability_operation_count> seen{};
    for (std::size_t i = 0; i < input.operations.size(); ++i) {
        const auto& declaration = input.operations[i];
        const auto at = "/operations/" + std::to_string(i);
        if (!declared(declaration.operation, operation_names)) {
            error(out, "unknown_value", at + "/operation", "Use a declared capability operation.");
        } else if (std::exchange(seen[static_cast<std::size_t>(declaration.operation)], true)) {
            error(out, "duplicate_operation", at + "/operation", "Declare each operation at most once.");
        }
        if (!declared(declaration.availability, availability_names))
            error(out, "unknown_value", at + "/availability", "Use a declared command availability.");
        check_identity(out, declaration.entry_point, at + "/entry_point");
        check_identity(out, declaration.request, at + "/request");
        check_identity(out, declaration.result, at + "/result");
        std::array<bool, precondition_names.size()> conditions{};
        for (std::size_t p = 0; p < declaration.preconditions.size(); ++p) {
            const auto condition = declaration.preconditions[p];
            const auto path = at + "/preconditions/" + std::to_string(p);
            if (!declared(condition, precondition_names))
                error(out, "unknown_value", path, "Use a declared capability precondition.");
            else if (std::exchange(conditions[static_cast<std::size_t>(condition)], true))
                error(out, "duplicate_precondition", path, "Declare each precondition at most once.");
        }
    }
    return out;
}

JsonValue text(std::string_view value) { return JsonValue{std::string(value)}; }
template<std::size_t N> JsonValue name(auto value, const std::array<std::string_view, N>& names) {
    return text(names[static_cast<std::size_t>(value)]);
}
JsonValue command_value(const CommandValue& value) {
    return std::visit(
        [](const auto& v) -> JsonValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, CommandObjectId>)
                return text(v.value);
            else
                return JsonValue{v};
        },
        value);
}
JsonValue parameters(const std::vector<CommandParameter>& fields) {
    Array out;
    for (const auto& field : fields) {
        Object o{{"name", text(field.name)},
                 {"type", name(field.type, parameter_types)},
                 {"required", JsonValue{field.required}}};
        if (field.default_value) o["default"] = command_value(*field.default_value);
        if (field.type == CommandParameterType::number) {
            if (field.minimum) o["minimum"] = JsonValue{*field.minimum};
            if (field.maximum) o["maximum"] = JsonValue{*field.maximum};
        }
        if (field.type == CommandParameterType::string || field.type == CommandParameterType::object_id)
            o["max_bytes"] = JsonValue{static_cast<double>(field.max_bytes)};
        if (field.type == CommandParameterType::enumeration) {
            Array choices;
            for (const auto& choice : field.choices) choices.push_back(text(choice));
            o["choices"] = JsonValue{std::move(choices)};
        }
        out.push_back(JsonValue{std::move(o)});
    }
    return JsonValue{std::move(out)};
}
JsonValue write(const CapabilityManifest& manifest) {
    Array operations;
    for (const auto& record : manifest.operations()) {
        Object o{{"operation", name(record.operation, operation_names)}, {"status", name(record.status, status_names)}};
        if (record.status == CapabilityStatus::supported) {
            Array conditions;
            for (const auto condition : record.preconditions) conditions.push_back(name(condition, precondition_names));
            o["entry_point"] = text(record.entry_point);
            o["request"] = text(record.request);
            o["result"] = text(record.result);
            o["preconditions"] = JsonValue{std::move(conditions)};
        }
        operations.push_back(JsonValue{std::move(o)});
    }
    Array commands;
    for (const auto& [descriptor, state] : manifest.commands())
        commands.push_back(JsonValue{Object{{"id", text(descriptor.id)},
                                            {"label", text(descriptor.label)},
                                            {"description", text(descriptor.description)},
                                            {"category", text(descriptor.category)},
                                            {"availability", name(descriptor.availability, availability_names)},
                                            {"enabled", JsonValue{state.enabled}},
                                            {"checked", JsonValue{state.checked}},
                                            {"disabled_reason", text(state.disabled_reason)},
                                            {"parameters", parameters(descriptor.parameters)},
                                            {"results", parameters(descriptor.results)}}});
    Array objects;
    for (const auto& id : manifest.objects()) objects.push_back(text(id));
    Array targets;
    for (const auto& target : manifest.targets()) {
        Array actions;
        for (const auto& action : target.actions)
            actions.push_back(JsonValue{Object{{"binding", text(action.binding)},
                                               {"action", text(action.action)},
                                               {"command", action.command ? text(*action.command) : JsonValue{}}}});
        targets.push_back(JsonValue{Object{{"node", JsonValue{static_cast<double>(target.node)}},
                                           {"id", target.id ? text(*target.id) : JsonValue{}},
                                           {"role", name(target.role, role_names)},
                                           {"name", text(target.name)},
                                           {"focusable", JsonValue{target.focusable}},
                                           {"actions", JsonValue{std::move(actions)}}}});
    }
    return JsonValue{Object{{"format", text("tessera-capabilities")},
                            {"version", JsonValue{static_cast<double>(manifest.version())}},
                            {"producer", text(manifest.producer())},
                            {"mode", name(manifest.mode(), mode_names)},
                            {"generation", text(std::to_string(manifest.generation()))},
                            {"operations", JsonValue{std::move(operations)}},
                            {"commands", JsonValue{std::move(commands)}},
                            {"objects", JsonValue{std::move(objects)}},
                            {"targets", JsonValue{std::move(targets)}}}};
}
} // namespace

Result<CapabilityManifest> capture_capabilities(const CapabilityInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto& commands = *input.commands;
    CapabilityManifest manifest;
    manifest.producer_ = std::string(input.producer);
    manifest.mode_ = commands.mode();
    manifest.generation_ = commands.generation();
    for (std::size_t i = 0; i < capability_operation_count; ++i)
        manifest.operations_.push_back({static_cast<CapabilityOperation>(i)});
    for (const auto& declaration : input.operations) {
        auto& record = manifest.operations_[static_cast<std::size_t>(declaration.operation)];
        if (!available(declaration.availability, manifest.mode_)) {
            record.status = CapabilityStatus::unavailable;
            continue;
        }
        record.status = CapabilityStatus::supported;
        record.entry_point = declaration.entry_point;
        record.request = declaration.request;
        record.result = declaration.result;
        record.preconditions = declaration.preconditions;
        std::sort(record.preconditions.begin(), record.preconditions.end());
    }
    manifest.commands_ = commands.discover();
    manifest.objects_ = commands.permitted_objects();
    for (const auto& entry : input.semantics->nodes) {
        if (!entry.focusable && entry.actions.empty()) continue;
        CapabilityTarget target{entry.node, entry.id, entry.role, entry.name, entry.focusable, {}};
        for (const auto& action : entry.actions) {
            const auto mapped = commands.lookup_action(action.action);
            target.actions.push_back(
                {action.binding, action.action, mapped ? std::optional(mapped.value->descriptor.id) : std::nullopt});
        }
        manifest.targets_.push_back(std::move(target));
    }
    return {std::move(manifest), {}};
}

Result<std::string> save_capabilities(const CapabilityManifest& manifest) {
    auto result = detail::write_json_value(write(manifest)) + '\n';
    if (result.size() > max_serialized_capability_bytes)
        return {std::nullopt,
                {{"size_limit", Severity::error, "", "The canonical capability manifest exceeds 16 MiB.", {}}}};
    return {std::move(result), {}};
}
} // namespace tessera
