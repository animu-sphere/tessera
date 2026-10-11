#include <tessera/ui/component.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {
using Errors = std::vector<Diagnostic>;
void error(Errors& errors, std::string code, std::string path, std::string message) {
    errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
void name(Errors& errors, std::string_view value, const std::string& path) {
    const bool controls = std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    if (value.empty() || value.size() > 256 || controls || !detail::valid_utf8(value))
        error(errors, "component_name", path, "Supply a name of 1 to 256 UTF-8 bytes without control characters.");
}
void literal(Errors& errors, const Property& value, const std::string& path) {
    if (const auto* text = std::get_if<std::string>(&value); text && !detail::valid_utf8(*text))
        error(errors, "invalid_utf8", path, "Use valid UTF-8 text.");
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        error(errors, "invalid_number", path, "Numbers must be finite.");
    if (const auto* ref = std::get_if<NodeReference>(&value)) name(errors, ref->id, path + "/ref");
}
PropertyType type(const ComponentValue& value) {
    return std::visit([](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Property>) return property_type(data);
        else if constexpr (std::is_same_v<T, Signal<bool>> || std::is_same_v<T, Computed<bool>>) return PropertyType::boolean;
        else if constexpr (std::is_same_v<T, Signal<double>> || std::is_same_v<T, Computed<double>>) return PropertyType::number;
        else if constexpr (std::is_same_v<T, Signal<std::string>> || std::is_same_v<T, Computed<std::string>>) return PropertyType::string;
        else return PropertyType::reference;
    }, value);
}
struct Target { const UiNode* node; std::string path; std::size_t depth; };
void index(const UiNode& node, const std::string& path, std::size_t depth,
           std::map<std::string, Target, std::less<>>& targets, std::size_t& count) {
    ++count;
    if (node.id) targets.emplace(*node.id, Target{&node, path, depth});
    for (std::size_t i = 0; i < node.children.size(); ++i)
        index(node.children[i], path + "/children/" + std::to_string(i), depth + 1, targets, count);
}
bool fits(const UiNode& node, std::size_t depth, std::size_t& count) {
    if (++count > max_document_nodes || depth > max_document_depth) return false;
    for (const auto& child : node.children)
        if (!fits(child, depth + 1, count)) return false;
    return true;
}
void lower(UiNode& node, const ComponentDescription& description, const ComponentArguments& arguments,
           const std::map<std::string, Property, std::less<>>& values, PropertyStages& stages) {
    if (node.id) {
        for (const auto& binding : description.properties) {
            if (binding.target != *node.id) continue;
            const auto& value = values.at(binding.prop);
            const auto previous = node.properties.find(binding.property);
            if (previous == node.properties.end() || previous->second != value)
                stages = stages | find_property_descriptor(binding.property)->stages;
            node.properties.insert_or_assign(binding.property, value);
        }
        for (const auto& slot : description.slots) {
            if (slot.target != *node.id) continue;
            const auto found = arguments.slots.find(slot.name);
            if (found != arguments.slots.end() && !found->second.empty()) {
                node.children = found->second;
                stages = stages | PropertyStages::style | PropertyStages::layout | PropertyStages::paint |
                    PropertyStages::input | PropertyStages::semantics;
            }
            return; // Slot content is opaque to this component's mappings.
        }
    }
    for (auto& child : node.children) lower(child, description, arguments, values, stages);
}
} // namespace

Result<BoundDocument> lower_component(const ReactiveRuntime& runtime, const ComponentDescription& description,
                                     const ComponentArguments& arguments, const ValidationContext& context) {
    Errors errors;
    if (description.props.size() > max_component_props || arguments.props.size() > max_component_props ||
        description.slots.size() > max_component_slots || arguments.slots.size() > max_component_slots ||
        description.properties.size() > max_property_bindings) {
        error(errors, "component_limit", "/component", "Supply at most 256 props, 256 slots and 4096 property mappings.");
        return {{}, std::move(errors)};
    }
    name(errors, description.kind, "/component/kind");
    auto template_errors = validate(description.document, context);
    for (auto& diagnostic : template_errors) {
        diagnostic.path = "/component/document" + diagnostic.path;
        errors.push_back(std::move(diagnostic));
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    std::map<std::string, Target, std::less<>> targets;
    std::size_t count = 0;
    index(description.document.root, "/root", 1, targets, count);
    std::map<std::string, const ComponentProp*, std::less<>> props;
    for (std::size_t i = 0; i < description.props.size(); ++i) {
        const auto& prop = description.props[i];
        const auto path = "/component/props/" + std::to_string(i);
        name(errors, prop.name, path + "/name");
        if (!props.emplace(prop.name, &prop).second)
            error(errors, "component_duplicate_prop", path + "/name", "Declare each prop name once.");
        if (prop.type != PropertyType::boolean && prop.type != PropertyType::number &&
            prop.type != PropertyType::string && prop.type != PropertyType::reference)
            error(errors, "component_prop_type", path + "/type", "Use boolean, number, string or reference.");
        if (prop.default_value) {
            if (property_type(*prop.default_value) != prop.type)
                error(errors, "component_prop_type", path + "/default", "Use the declared prop type; values are not coerced.");
            literal(errors, *prop.default_value, path + "/default");
        }
    }
    std::set<std::pair<std::string, std::string>> writers;
    for (std::size_t i = 0; i < description.properties.size(); ++i) {
        const auto& binding = description.properties[i];
        const auto path = "/component/properties/" + std::to_string(i);
        const auto prop = props.find(binding.prop);
        const auto target = targets.find(binding.target);
        const auto* descriptor = find_property_descriptor(binding.property);
        if (prop == props.end())
            error(errors, "component_unknown_prop", path + "/prop", "Map a declared prop name.");
        if (target == targets.end())
            error(errors, "component_target", path + "/target", "Map an author ID in the component template.");
        if (!descriptor || (target != targets.end() && !contains(descriptor->accepted, target->second.node->kind)))
            error(errors, "component_property", path + "/property", "Use a reflected property accepted by the target node kind.");
        else if (prop != props.end() && descriptor->type != prop->second->type)
            error(errors, "component_prop_type", path + "/prop", "Map a prop with the reflected property's exact type.");
        if (!writers.emplace(binding.target, binding.property).second)
            error(errors, "component_duplicate_property", path + "/property", "Give each target property one prop writer.");
    }
    std::map<std::string, const ComponentSlot*, std::less<>> slots;
    std::set<std::string, std::less<>> slot_targets;
    for (std::size_t i = 0; i < description.slots.size(); ++i) {
        const auto& slot = description.slots[i];
        const auto path = "/component/slots/" + std::to_string(i);
        name(errors, slot.name, path + "/name");
        if (!slots.emplace(slot.name, &slot).second)
            error(errors, "component_duplicate_slot", path + "/name", "Declare each slot name once.");
        const auto target = targets.find(slot.target);
        if (target == targets.end() || target->second.node->kind != NodeKind::box || !target->second.node->children.empty())
            error(errors, "component_slot_target", path + "/target", "Target an empty Box in the component template.");
        if (!slot_targets.insert(slot.target).second)
            error(errors, "component_duplicate_slot_target", path + "/target", "Give each slot its own empty Box.");
        if (slot.accepted == NodeKinds::none || (static_cast<unsigned>(slot.accepted) & ~static_cast<unsigned>(NodeKinds::all)) != 0)
            error(errors, "component_slot_kind", path + "/accepted", "Accept Box, Text or both node kinds.");
        if (slot.minimum > slot.maximum || slot.maximum > max_document_nodes)
            error(errors, "component_slot_bounds", path, "Use ordered slot bounds between 0 and 10000 children.");
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    for (const auto& [key, value] : arguments.props) {
        const auto path = "/props/" + detail::pointer_token(key);
        const auto prop = props.find(key);
        if (prop == props.end()) error(errors, "component_unknown_prop", path, "Supply only declared props.");
        else if (type(value) != prop->second->type)
            error(errors, "component_prop_type", path, "Supply the declared prop type; values are not coerced.");
        if (const auto* data = std::get_if<Property>(&value)) literal(errors, *data, path);
    }
    for (const auto& prop : description.props)
        if (!arguments.props.contains(prop.name) && !prop.default_value)
            error(errors, "component_missing_prop", "/props/" + detail::pointer_token(prop.name), "Supply this required prop or declare a default.");
    for (const auto& [key, children] : arguments.slots) {
        if (!slots.contains(key))
            error(errors, "component_unknown_slot", "/slots/" + detail::pointer_token(key), "Supply only declared slots.");
    }
    for (const auto& slot : description.slots) {
        const auto path = "/slots/" + detail::pointer_token(slot.name);
        const auto found = arguments.slots.find(slot.name);
        const auto size = found == arguments.slots.end() ? 0 : found->second.size();
        if (size < slot.minimum || size > slot.maximum) {
            error(errors, "component_slot_count", path, "Supply between " + std::to_string(slot.minimum) + " and " +
                std::to_string(slot.maximum) + " children in this slot.");
            continue;
        }
        if (found == arguments.slots.end()) continue;
        for (std::size_t i = 0; i < size; ++i) {
            const auto& child = found->second[i];
            const auto child_path = path + "/" + std::to_string(i);
            if (!contains(slot.accepted, child.kind))
                error(errors, "component_slot_kind", child_path + "/type", "Supply a root node kind accepted by this slot.");
            auto diagnostics = detail::validate_document_parts(1, child, {}, context);
            bool valid = true;
            for (auto& diagnostic : diagnostics) {
                // References can cross from slot content into the enclosing
                // component. The final document validates their existence.
                if (diagnostic.code == "invalid_reference") continue;
                valid = false;
                diagnostic.path = child_path + diagnostic.path.substr(5); // Replace /root.
                errors.push_back(std::move(diagnostic));
            }
            if (valid && !fits(child, targets.at(slot.target).depth + 1, count)) {
                error(errors, "component_document_limit", child_path, "Keep the composed document within 64 levels and 10000 nodes.");
                return {{}, std::move(errors)};
            }
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    try { runtime.check_settled(); }
    catch (const ReactiveError& e) { return {{}, {e.diagnostic()}}; }
    std::map<std::string, Property, std::less<>> values;
    for (const auto& prop : description.props) {
        const auto path = "/props/" + detail::pointer_token(prop.name);
        const auto found = arguments.props.find(prop.name);
        try {
            Property value = found == arguments.props.end() ? *prop.default_value : std::visit([&](const auto& data) -> Property {
                if constexpr (std::is_same_v<std::decay_t<decltype(data)>, Property>) return data;
                else return runtime.snapshot(data);
            }, found->second);
            literal(errors, value, path);
            values.emplace(prop.name, std::move(value));
        } catch (const ReactiveError& e) {
            auto cause = e.diagnostic();
            cause.message += " Reactive location: " + cause.path + ".";
            cause.path = path;
            errors.push_back(std::move(cause));
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    BoundDocument candidate{description.document};
    lower(candidate.document.root, description, arguments, values, candidate.stages);
    errors = validate(candidate.document, context);
    if (!errors.empty()) return {{}, std::move(errors)};
    return {std::move(candidate), {}};
}
} // namespace tessera
