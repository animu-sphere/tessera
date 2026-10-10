#include <tessera/commands/commands.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tessera {
namespace detail {
struct CommandRegistryData {
    std::uint64_t revision;
    std::map<std::string, CommandDescriptor, std::less<>> descriptors;
    CommandBindings bindings;
};
struct CommandSnapshotData {
    std::shared_ptr<const CommandRegistryData> registry;
    std::uint64_t generation;
    CommandMode mode;
    CommandStates states;
    CommandObjectIds objects;
};
} // namespace detail
namespace {
using detail::Checker;
bool segment(std::string_view s) {
    if (s.empty() || s.front() < 'a' || s.front() > 'z') return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}
bool command_id(std::string_view s) {
    bool dot = false;
    while (true) {
        const auto end = s.find('.');
        if (!segment(s.substr(0, end))) return false;
        if (end == s.npos) return dot;
        dot = true;
        s.remove_prefix(end + 1);
    }
}
void text(Checker& check, const std::string& s, const std::string& path, bool nonempty = false,
          std::size_t bound = max_command_string_bytes) {
    if (nonempty && s.empty()) check.error("empty_text", path, "Supply a nonempty UTF-8 value.");
    if (s.size() > bound) check.error("out_of_range", path, "Reduce the value to the declared byte bound.");
    if (!detail::valid_utf8(s)) check.error("invalid_utf8", path, "Supply valid UTF-8.");
}
void value(Checker& check, const CommandParameter& p, const CommandValue& v, const std::string& path,
           const CommandObjectIds* objects) {
    bool matches = false;
    switch (p.type) {
    case CommandParameterType::boolean: matches = std::holds_alternative<bool>(v); break;
    case CommandParameterType::number: matches = std::holds_alternative<double>(v); break;
    case CommandParameterType::string:
    case CommandParameterType::enumeration: matches = std::holds_alternative<std::string>(v); break;
    case CommandParameterType::object_id: matches = std::holds_alternative<CommandObjectId>(v); break;
    }
    if (!matches) { check.error("argument_type", path, "Use the declared parameter type; coercion is not supported."); return; }
    if (const auto* number = std::get_if<double>(&v)) {
        if (!std::isfinite(*number)) check.error("invalid_number", path, "Supply a finite number.");
        else if ((p.minimum && *number < *p.minimum) || (p.maximum && *number > *p.maximum))
            check.error("out_of_range", path, "Supply a number within the inclusive descriptor bounds.");
    } else if (const auto* string = std::get_if<std::string>(&v)) {
        text(check, *string, path, false, p.max_bytes);
        if (p.type == CommandParameterType::enumeration &&
            std::find(p.choices.begin(), p.choices.end(), *string) == p.choices.end())
            check.error("unknown_value", path, "Use one of the descriptor's enumeration choices.");
    } else if (const auto* object = std::get_if<CommandObjectId>(&v)) {
        text(check, object->value, path, true, p.max_bytes);
        if (objects && !objects->contains(object->value))
            check.error("invalid_reference", path, "Publish the referenced host object ID in this generation.");
    }
}
bool available(const CommandDescriptor& d, CommandMode mode) {
    return d.availability == CommandAvailability::both ||
        (d.availability == CommandAvailability::development && mode == CommandMode::development) ||
        (d.availability == CommandAvailability::production && mode == CommandMode::production);
}
CommandState state(const detail::CommandSnapshotData& data, const std::string& id) {
    const auto found = data.states.find(id);
    if (found == data.states.end()) return {false, false, "Eligibility observation is unavailable.", {}};
    auto s = found->second;
    if (s.query_error) { s.enabled = false; s.checked = false; s.disabled_reason = *s.query_error; }
    return s;
}
std::uint64_t next_revision() {
    static std::atomic<std::uint64_t> next{1};
    auto revision = next.load(std::memory_order_relaxed);
    do {
        if (revision == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Command registry revision identities exhausted.");
    } while (!next.compare_exchange_weak(revision, revision + 1, std::memory_order_relaxed));
    return revision;
}
std::vector<Diagnostic> check_owner(const SemanticInput& ui, const ActionRequest& owner) {
    auto checked = request_semantic_action(ui, owner.target, owner.binding);
    if (!checked) return std::move(checked.diagnostics);
    if (*checked.value != owner)
        return {{"stale_binding", Severity::error, "/owner/action", "Resolve the current action binding again.", {}}};
    return {};
}
} // namespace

CommandRegistry::CommandRegistry(std::shared_ptr<const detail::CommandRegistryData> data) : data_(std::move(data)) {}
std::uint64_t CommandRegistry::revision() const noexcept { return data_->revision; }
Result<CommandRegistry> CommandRegistry::create(std::vector<CommandDescriptor> descriptors, CommandBindings bindings) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    if (descriptors.size() > max_commands) check.error("out_of_range", "/commands", "Register at most 1024 commands.");
    if (bindings.size() > max_commands) check.error("out_of_range", "/bindings", "Register at most 1024 action mappings.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto data = std::make_shared<detail::CommandRegistryData>();
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        auto& d = descriptors[i];
        const auto path = "/commands/" + std::to_string(i);
        if (d.id.size() > 128 || !command_id(d.id))
            check.error("invalid_identifier", path + "/id", "Use at least two dot-separated lower-case identifier segments, within 128 bytes.");
        text(check, d.label, path + "/label", true);
        text(check, d.description, path + "/description");
        text(check, d.category, path + "/category");
        check.enumeration(d.availability, CommandAvailability::production, path + "/availability");
        if (d.parameters.size() > max_command_parameters) {
            check.error("out_of_range", path + "/parameters", "Declare at most 64 parameters.");
            continue;
        }
        std::set<std::string> names;
        for (std::size_t j = 0; j < d.parameters.size(); ++j) {
            const auto& p = d.parameters[j];
            const auto pp = path + "/parameters/" + std::to_string(j);
            if (p.name.size() > 128 || !segment(p.name)) check.error("invalid_identifier", pp + "/name", "Use a lower-case identifier segment within 128 bytes.");
            if (!names.insert(p.name).second) check.error("duplicate_parameter", pp + "/name", "Declare each parameter name once.");
            check.enumeration(p.type, CommandParameterType::object_id, pp + "/type");
            if ((p.minimum && !std::isfinite(*p.minimum)) || (p.maximum && !std::isfinite(*p.maximum)) ||
                (p.minimum && p.maximum && *p.minimum > *p.maximum))
                check.error("invalid_bounds", pp, "Use ordered finite inclusive number bounds.");
            if (p.type != CommandParameterType::number && (p.minimum || p.maximum))
                check.error("invalid_schema", pp, "Number bounds apply only to number parameters.");
            if (p.max_bytes > max_command_string_bytes) check.error("out_of_range", pp + "/max_bytes", "Use a byte bound at most 4096.");
            if (p.type == CommandParameterType::object_id && p.max_bytes == 0)
                check.error("invalid_bounds", pp + "/max_bytes", "Object IDs require a nonzero byte bound.");
            if (p.type == CommandParameterType::enumeration) {
                if (p.choices.empty() || p.choices.size() > max_command_parameters)
                    check.error("out_of_range", pp + "/choices", "Declare between 1 and 64 enumeration choices.");
                else {
                    std::set<std::string> choices;
                    for (std::size_t k = 0; k < p.choices.size(); ++k) {
                        const auto cp = pp + "/choices/" + std::to_string(k);
                        text(check, p.choices[k], cp, false, p.max_bytes);
                        if (!choices.insert(p.choices[k]).second) check.error("duplicate_choice", cp, "Declare each choice once.");
                    }
                }
            } else if (!p.choices.empty()) check.error("invalid_schema", pp + "/choices", "Choices apply only to enumeration parameters.");
            if (p.default_value) value(check, p, *p.default_value, pp + "/default", nullptr);
        }
        if (!data->descriptors.emplace(d.id, d).second)
            check.error("duplicate_command", path + "/id", "Register each command ID once.");
    }
    for (const auto& [action, id] : bindings) {
        const auto path = "/bindings/" + detail::pointer_token(action);
        text(check, action, path, true, 128);
        if (!data->descriptors.contains(id)) check.error("unknown_command", path, "Map the action to an exact registered command ID.");
    }
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    data->revision = next_revision();
    data->bindings = std::move(bindings);
    return {CommandRegistry{std::move(data)}, {}};
}

CommandSnapshot::CommandSnapshot(std::shared_ptr<const detail::CommandSnapshotData> data) : data_(std::move(data)) {}
CommandInvocation::CommandInvocation(CommandInvocationRecord record, std::shared_ptr<const detail::CommandSnapshotData> data)
    : record_(std::move(record)), snapshot_(std::move(data)) {}
std::uint64_t CommandSnapshot::generation() const noexcept { return data_->generation; }
std::uint64_t CommandSnapshot::registry_revision() const noexcept { return data_->registry->revision; }
Result<CommandSnapshot> CommandSnapshot::publish(const CommandRegistry& registry, std::uint64_t generation,
    CommandMode mode, CommandStates states, CommandObjectIds objects) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    if (!generation) check.error("invalid_generation", "/generation", "Publish a nonzero host update sequence.");
    check.enumeration(mode, CommandMode::production, "/mode");
    if (states.size() > max_commands) check.error("out_of_range", "/states", "Publish at most 1024 eligibility observations.");
    if (objects.size() > max_document_nodes) check.error("out_of_range", "/objects", "Publish at most 10000 permitted object IDs.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    for (const auto& [id, s] : states) {
        const auto path = "/states/" + detail::pointer_token(id);
        if (!registry.data_->descriptors.contains(id)) check.error("unknown_command", path, "Observe only registered command IDs.");
        text(check, s.disabled_reason, path + "/disabled_reason");
        if (s.query_error) text(check, *s.query_error, path + "/query_error", true);
    }
    for (const auto& id : objects) text(check, id, "/objects/" + detail::pointer_token(id), true);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto data = std::make_shared<detail::CommandSnapshotData>(detail::CommandSnapshotData{
        registry.data_, generation, mode, std::move(states), std::move(objects)});
    return {CommandSnapshot{std::move(data)}, {}};
}
Result<CommandInfo> CommandSnapshot::lookup(std::string_view id) const {
    const auto found = data_->registry->descriptors.find(id);
    if (found == data_->registry->descriptors.end() || !available(found->second, data_->mode))
        return {std::nullopt, {{"unknown_command", Severity::error, "/command", "Use an exact command ID available in this mode.", {}}}};
    return {CommandInfo{found->second, state(*data_, found->first)}, {}};
}
std::vector<CommandInfo> CommandSnapshot::discover() const {
    std::vector<CommandInfo> out;
    for (const auto& [id, d] : data_->registry->descriptors)
        if (available(d, data_->mode)) out.push_back({d, state(*data_, id)});
    return out;
}
Result<UiDocument> CommandSnapshot::apply_eligibility(const UiDocument& authored, const ValidationContext& context) const {
    auto errors = validate(authored, context);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto document = authored;
    const auto lower = [&](auto&& self, UiNode& node) -> void {
        const auto event = node.events.find("activate");
        if (event != node.events.end()) {
            const auto binding = data_->registry->bindings.find(event->second);
            if (binding != data_->registry->bindings.end()) {
                const auto info = lookup(binding->second);
                if (!info || !info.value->state.enabled) node.properties["disabled"] = true;
            }
        }
        for (auto& child : node.children) self(self, child);
    };
    lower(lower, document.root);
    return {std::move(document), {}};
}
Result<CommandInvocation> CommandSnapshot::resolve(const CommandRequest& request) const {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    if (request.generation != generation()) check.error("stale_generation", "/generation", "Resolve against the current settled generation.");
    if (!request.sequence) check.error("invalid_sequence", "/sequence", "Supply a nonzero logical request sequence.");
    check.enumeration(request.source, CommandSource::replay, "/source");
    if (request.arguments.size() > max_command_parameters) check.error("out_of_range", "/arguments", "Supply at most 64 arguments.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto info = lookup(request.command);
    if (!info) return {std::nullopt, info.diagnostics};
    if (!info.value->state.enabled) {
        check.error("disabled_command", "/command", info.value->state.disabled_reason.empty() ?
            "The command is disabled in this generation." : info.value->state.disabled_reason);
        return {std::nullopt, std::move(errors)};
    }
    auto args = request.arguments;
    const auto& parameters = info.value->descriptor.parameters;
    for (const auto& [name, v] : args) {
        (void)v;
        if (std::none_of(parameters.begin(), parameters.end(), [&](const auto& p) { return p.name == name; }))
            check.error("unknown_argument", "/arguments/" + detail::pointer_token(name), "Use a declared parameter name.");
    }
    for (const auto& p : parameters) {
        auto found = args.find(p.name);
        if (found == args.end() && p.default_value) found = args.emplace(p.name, *p.default_value).first;
        const auto path = "/arguments/" + p.name;
        if (found != args.end()) value(check, p, found->second, path, &data_->objects);
        else if (p.required) check.error("missing_argument", path, "Supply this required parameter.");
    }
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {CommandInvocation{{generation(), registry_revision(), request.command, std::move(args),
        request.source, request.sequence, {}}, data_}, {}};
}
Result<CommandInvocation> CommandSnapshot::resolve_action(const SemanticInput& ui, const ActionRequest& action,
    std::uint64_t request_generation, CommandArguments arguments, CommandSource source, std::uint64_t sequence) const {
    if (request_generation != generation())
        return {std::nullopt, {{"stale_generation", Severity::error, "/generation", "Resolve against the current settled generation.", {}}}};
    auto errors = check_owner(ui, action);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto binding = data_->registry->bindings.find(action.action);
    if (binding == data_->registry->bindings.end())
        return {std::nullopt, {{"unmapped_action", Severity::error, "/action", "Register an explicit action-to-command mapping.", {}}}};
    auto result = resolve({request_generation, binding->second, std::move(arguments), source, sequence});
    if (result) result.value->record_.owner = action;
    return result;
}
std::vector<Diagnostic> CommandSnapshot::recheck(const CommandInvocation& invocation, const SemanticInput* ui) const {
    if (invocation.record_.registry_revision != registry_revision())
        return {{"stale_registry", Severity::error, "/registry_revision", "Resolve again after registry replacement.", {}}};
    if (invocation.snapshot_ != data_)
        return {{"stale_generation", Severity::error, "/generation", "Resolve again after publishing eligibility or UI state.", {}}};
    if (invocation.record_.owner) {
        if (!ui) return {{"missing_snapshot", Severity::error, "/owner", "Supply the current coherent UI snapshot before execution.", {}}};
        return check_owner(*ui, *invocation.record_.owner);
    }
    return {};
}

} // namespace tessera
