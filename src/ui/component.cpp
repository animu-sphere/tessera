#include <tessera/ui/component.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>

namespace tessera {
namespace {
using Errors = std::vector<Diagnostic>;
using ComposedSlots = std::map<std::string, std::span<const BoundComponent>, std::less<>>;
bool input_limit(const ComponentDescription& description, const ComponentArguments& arguments, std::size_t composed_slots) {
    return description.props.size() > max_component_props || arguments.props.size() > max_component_props ||
        description.slots.size() > max_component_slots || arguments.slots.size() > max_component_slots ||
        composed_slots > max_component_slots - arguments.slots.size() ||
        description.actions.size() > max_component_actions || arguments.actions.size() > max_component_actions ||
        description.styles.size() > max_component_styles || arguments.styles.size() > max_component_styles ||
        description.properties.size() > max_property_bindings ||
        description.events.size() > max_property_bindings - description.properties.size() ||
        description.style_bindings.size() > max_property_bindings - description.properties.size() - description.events.size();
}
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
void host_action(Errors& errors, const std::string& value, const std::string& path,
                 const ValidationContext& context) {
    name(errors, value, path);
    if (!context.actions.contains(value))
        error(errors, "unknown_action", path, "Register host action '" + value + "' in ValidationContext before lowering.");
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
NodeSource source_record(const UiNode& node, const std::string& path, const DocumentSourceMap* map, std::size_t& index) {
    NodeSource source;
    if (map) {
        source = map->nodes[index++];
        if (source.mapped && !source.file) source.file = map->file;
    } else {
        source.mapped = false;
        for (const auto& [property, value] : node.properties) {
            (void)value;
            source.unmapped_properties.insert(property);
        }
    }
    source.pointer = path;
    return source;
}
void subtree_sources(const UiNode& node, const std::string& path, const DocumentSourceMap* map,
                     std::size_t& index, std::vector<NodeSource>& out) {
    out.push_back(source_record(node, path, map, index));
    for (std::size_t i = 0; i < node.children.size(); ++i)
        subtree_sources(node.children[i], path + "/children/" + std::to_string(i), map, index, out);
}
void component_sources(const UiNode& node, const std::string& path, const ComponentDescription& description,
                       const ComponentArguments& arguments, const ComposedSlots& slots,
                       std::size_t& template_index, std::vector<NodeSource>& out) {
    auto source = source_record(node, path, description.sources ? &*description.sources : nullptr, template_index);
    // A template placeholder is not the origin of an instance value, even when
    // the value happens to equal it. Preserve the node declaration's span only.
    if (node.id) for (const auto& binding : description.properties) {
        if (binding.target != *node.id) continue;
        source.properties.erase(binding.property);
        source.unmapped_properties.insert(binding.property);
    }
    out.push_back(std::move(source));
    if (node.id) for (const auto& slot : description.slots) {
        if (slot.target != *node.id) continue;
        const auto composed = slots.find(slot.name);
        std::size_t child_index = 0;
        if (composed != slots.end()) {
            for (const auto& child : composed->second) {
                std::size_t index = 0;
                subtree_sources(child.document.root, path + "/children/" + std::to_string(child_index++),
                    child.sources ? &*child.sources : nullptr, index, out);
            }
        } else {
            const auto raw = arguments.slots.find(slot.name);
            if (raw != arguments.slots.end()) for (const auto& child : raw->second) {
                std::size_t index = 0;
                subtree_sources(child, path + "/children/" + std::to_string(child_index++), nullptr, index, out);
            }
        }
        return;
    }
    for (std::size_t i = 0; i < node.children.size(); ++i)
        component_sources(node.children[i], path + "/children/" + std::to_string(i),
                          description, arguments, slots, template_index, out);
}
void style_overrides(const UiNode& node, const std::map<std::string, StyleDeclarations, std::less<>>& targets,
                     const ComponentDescription& description, const ComponentArguments& arguments, const ComposedSlots& composed_slots,
                     std::vector<StyleDeclarations>& overrides) {
    const auto found = node.id ? targets.find(*node.id) : targets.end();
    overrides.push_back(found == targets.end() ? StyleDeclarations{} : found->second);
    if (node.id) for (const auto& slot : description.slots) {
        if (slot.target != *node.id) continue;
        const auto composed = composed_slots.find(slot.name);
        if (composed != composed_slots.end()) {
            for (const auto& child : composed->second) {
                if (!child.overrides.empty()) overrides.insert(overrides.end(), child.overrides.begin(), child.overrides.end());
                else {
                    std::size_t count = 0;
                    fits(child.document.root, 1, count);
                    overrides.resize(overrides.size() + count);
                }
            }
        } else {
            const auto raw = arguments.slots.find(slot.name);
            if (raw != arguments.slots.end()) for (const auto& child : raw->second) {
                std::size_t count = 0;
                fits(child, 1, count);
                overrides.resize(overrides.size() + count);
            }
        }
        return; // Parent mappings never address child component content.
    }
    for (const auto& child : node.children) style_overrides(child, targets, description, arguments, composed_slots, overrides);
}
void lower(UiNode& node, const ComponentDescription& description, const ComponentArguments& arguments,
           const ComposedSlots& composed_slots,
           const std::map<std::string, Property, std::less<>>& values,
           const std::map<std::string, std::string, std::less<>>& actions, PropertyStages& stages) {
    if (node.id) {
        for (const auto& binding : description.properties) {
            if (binding.target != *node.id) continue;
            const auto& value = values.at(binding.prop);
            const auto previous = node.properties.find(binding.property);
            if (previous == node.properties.end() || previous->second != value)
                stages = stages | find_property_descriptor(binding.property)->stages;
            node.properties.insert_or_assign(binding.property, value);
        }
        for (const auto& binding : description.events) {
            if (binding.target != *node.id) continue;
            const auto& action = actions.at(binding.action);
            const auto previous = node.events.find(binding.event);
            if (previous == node.events.end() || previous->second != action)
                stages = stages | PropertyStages::input | PropertyStages::semantics;
            node.events.insert_or_assign(binding.event, action);
        }
        for (const auto& slot : description.slots) {
            if (slot.target != *node.id) continue;
            const auto found = arguments.slots.find(slot.name);
            const auto composed = composed_slots.find(slot.name);
            if (found != arguments.slots.end() && !found->second.empty()) {
                node.children = found->second;
            } else if (composed != composed_slots.end()) {
                for (const auto& child : composed->second) node.children.push_back(child.document.root);
            }
            if (!node.children.empty()) {
                stages = stages | PropertyStages::style | PropertyStages::layout | PropertyStages::paint |
                    PropertyStages::input | PropertyStages::semantics;
            }
            return; // Slot content is opaque to this component's mappings.
        }
    }
    for (auto& child : node.children) lower(child, description, arguments, composed_slots, values, actions, stages);
}

Result<BoundComponent> lower_component_impl(const ReactiveRuntime& runtime, const ComponentDescription& description,
    const ComponentArguments& arguments, const ComposedSlots& composed_slots, const ValidationContext& context) {
    Errors errors;
    if (input_limit(description, arguments, composed_slots.size())) {
        error(errors, "component_limit", "/component", "Supply at most 256 props, slots, actions and styles each, and 4096 combined property/event/style mappings.");
        return {{}, std::move(errors)};
    }
    name(errors, description.kind, "/component/kind");
    auto template_errors = validate(description.document, context);
    for (auto& diagnostic : template_errors) {
        diagnostic.path = "/component/document" + diagnostic.path;
        errors.push_back(std::move(diagnostic));
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    if (description.sources) {
        errors = validate_sources(description.document, *description.sources, "/component/sources");
        if (!errors.empty()) return {{}, std::move(errors)};
    }
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
    std::map<std::string, const ComponentAction*, std::less<>> actions;
    for (std::size_t i = 0; i < description.actions.size(); ++i) {
        const auto& action = description.actions[i];
        const auto path = "/component/actions/" + std::to_string(i);
        name(errors, action.name, path + "/name");
        if (!actions.emplace(action.name, &action).second)
            error(errors, "component_duplicate_action", path + "/name", "Declare each action name once.");
        if (action.default_action) host_action(errors, *action.default_action, path + "/default", context);
    }
    std::set<std::pair<std::string, std::string>> event_writers;
    for (std::size_t i = 0; i < description.events.size(); ++i) {
        const auto& binding = description.events[i];
        const auto path = "/component/events/" + std::to_string(i);
        if (!actions.contains(binding.action))
            error(errors, "component_unknown_action", path + "/action", "Map a declared component action name.");
        if (!targets.contains(binding.target))
            error(errors, "component_target", path + "/target", "Map an author ID in the component template.");
        if (binding.event != "activate" && binding.event != "cancel")
            error(errors, "unknown_event", path + "/event", "Supported binding names are activate and cancel.");
        if (!event_writers.emplace(binding.target, binding.event).second)
            error(errors, "component_duplicate_event", path + "/event", "Give each target event one action writer.");
    }
    std::map<std::string, const ComponentStyle*, std::less<>> styles;
    for (std::size_t i = 0; i < description.styles.size(); ++i) {
        const auto& style = description.styles[i];
        const auto path = "/component/styles/" + std::to_string(i);
        name(errors, style.name, path + "/name");
        if (!styles.emplace(style.name, &style).second)
            error(errors, "component_duplicate_style", path + "/name", "Declare each style name once.");
        if (style.default_value) {
            auto diagnostics = validate(*style.default_value, path + "/default");
            errors.insert(errors.end(), diagnostics.begin(), diagnostics.end());
        }
    }
    std::set<std::string, std::less<>> style_targets;
    for (std::size_t i = 0; i < description.style_bindings.size(); ++i) {
        const auto& binding = description.style_bindings[i];
        const auto path = "/component/style_bindings/" + std::to_string(i);
        if (!styles.contains(binding.style))
            error(errors, "component_unknown_style", path + "/style", "Map a declared style name.");
        if (!targets.contains(binding.target))
            error(errors, "component_target", path + "/target", "Map an author ID in the component template.");
        if (!style_targets.insert(binding.target).second)
            error(errors, "component_duplicate_style_target", path + "/target", "Give each target one style writer.");
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
    for (const auto& [key, value] : arguments.actions) {
        const auto path = "/actions/" + detail::pointer_token(key);
        if (!actions.contains(key))
            error(errors, "component_unknown_action", path, "Supply only declared component actions.");
        if (const auto* action = std::get_if<std::string>(&value)) host_action(errors, *action, path, context);
    }
    for (const auto& action : description.actions)
        if (!arguments.actions.contains(action.name) && !action.default_action)
            error(errors, "component_missing_action", "/actions/" + detail::pointer_token(action.name),
                  "Supply this required action or declare a registered default.");
    for (const auto& [key, value] : arguments.styles) {
        const auto path = "/styles/" + detail::pointer_token(key);
        if (!styles.contains(key))
            error(errors, "component_unknown_style", path, "Supply only declared component styles.");
        if (const auto* declarations = std::get_if<StyleDeclarations>(&value)) {
            auto diagnostics = validate(*declarations, path);
            errors.insert(errors.end(), diagnostics.begin(), diagnostics.end());
        }
    }
    for (const auto& style : description.styles)
        if (!arguments.styles.contains(style.name) && !style.default_value)
            error(errors, "component_missing_style", "/styles/" + detail::pointer_token(style.name),
                  "Supply this required style or declare a default.");
    for (const auto& [key, children] : arguments.slots) {
        if (!slots.contains(key))
            error(errors, "component_unknown_slot", "/slots/" + detail::pointer_token(key), "Supply only declared slots.");
    }
    for (const auto& [key, children] : composed_slots) {
        const auto path = "/slots/" + detail::pointer_token(key);
        if (!slots.contains(key))
            error(errors, "component_unknown_slot", path, "Supply only declared slots.");
        if (arguments.slots.contains(key))
            error(errors, "component_duplicate_slot_argument", path, "Supply raw or composed children for this slot, not both.");
    }
    for (const auto& slot : description.slots) {
        const auto path = "/slots/" + detail::pointer_token(slot.name);
        const auto found = arguments.slots.find(slot.name);
        const auto composed = composed_slots.find(slot.name);
        const auto size = found != arguments.slots.end() ? found->second.size() :
            composed != composed_slots.end() ? composed->second.size() : 0;
        if (size < slot.minimum || size > slot.maximum) {
            error(errors, "component_slot_count", path, "Supply between " + std::to_string(slot.minimum) + " and " +
                std::to_string(slot.maximum) + " children in this slot.");
            continue;
        }
        if (found == arguments.slots.end() && composed == composed_slots.end()) continue;
        for (std::size_t i = 0; i < size; ++i) {
            const auto* bound = found == arguments.slots.end() ? &composed->second[i] : nullptr;
            const auto& child = bound ? bound->document.root : found->second[i];
            const auto child_path = path + "/" + std::to_string(i);
            if (!contains(slot.accepted, child.kind))
                error(errors, "component_slot_kind", child_path + "/type", "Supply a root node kind accepted by this slot.");
            const JsonValue::Object empty_extensions;
            auto diagnostics = detail::validate_document_parts(bound ? bound->document.version : 1, child,
                bound ? bound->document.extensions : empty_extensions, context);
            bool valid = true;
            for (auto& diagnostic : diagnostics) {
                // References can cross from slot content into the enclosing
                // component. The final document validates their existence.
                if (diagnostic.code == "invalid_reference") continue;
                valid = false;
                diagnostic.path = child_path + (diagnostic.path.starts_with("/root") ? diagnostic.path.substr(5) : diagnostic.path);
                errors.push_back(std::move(diagnostic));
            }
            const auto target = targets.find(slot.target);
            const auto previous_count = count;
            if (valid && target != targets.end() && !fits(child, target->second.depth + 1, count)) {
                error(errors, "component_document_limit", child_path, "Keep the composed document within 64 levels and 10000 nodes.");
                return {{}, std::move(errors)};
            }
            if (bound && valid && target != targets.end()) {
                if (bound->sources) {
                    auto source_errors = validate_sources(bound->document, *bound->sources, child_path + "/sources");
                    errors.insert(errors.end(), source_errors.begin(), source_errors.end());
                }
                if (!bound->overrides.empty() && bound->overrides.size() != count - previous_count)
                    error(errors, "override_count", child_path + "/overrides", "Supply no overrides or one declaration per child document node in preorder.");
                else for (std::size_t j = 0; j < bound->overrides.size(); ++j) {
                    auto style_errors = validate(bound->overrides[j], child_path + "/overrides/" + std::to_string(j));
                    errors.insert(errors.end(), style_errors.begin(), style_errors.end());
                }
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
    std::map<std::string, std::string, std::less<>> action_values;
    for (const auto& action : description.actions) {
        const auto path = "/actions/" + detail::pointer_token(action.name);
        const auto found = arguments.actions.find(action.name);
        try {
            auto value = found == arguments.actions.end() ? *action.default_action : std::visit([&](const auto& data) -> std::string {
                if constexpr (std::is_same_v<std::decay_t<decltype(data)>, std::string>) return data;
                else return runtime.snapshot(data);
            }, found->second);
            host_action(errors, value, path, context);
            action_values.emplace(action.name, std::move(value));
        } catch (const ReactiveError& e) {
            auto cause = e.diagnostic();
            cause.message += " Reactive location: " + cause.path + ".";
            cause.path = path;
            errors.push_back(std::move(cause));
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    std::map<std::string, StyleDeclarations, std::less<>> style_values;
    for (const auto& style : description.styles) {
        const auto path = "/styles/" + detail::pointer_token(style.name);
        const auto found = arguments.styles.find(style.name);
        try {
            auto value = found == arguments.styles.end() ? *style.default_value : std::visit([&](const auto& data) -> StyleDeclarations {
                if constexpr (std::is_same_v<std::decay_t<decltype(data)>, StyleDeclarations>) return data;
                else return runtime.snapshot(data);
            }, found->second);
            auto diagnostics = validate(value, path);
            errors.insert(errors.end(), diagnostics.begin(), diagnostics.end());
            style_values.emplace(style.name, std::move(value));
        } catch (const ReactiveError& e) {
            auto cause = e.diagnostic();
            cause.message += " Reactive location: " + cause.path + ".";
            cause.path = path;
            errors.push_back(std::move(cause));
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    BoundComponent candidate;
    candidate.document = description.document;
    lower(candidate.document.root, description, arguments, composed_slots, values, action_values, candidate.stages);
    errors = validate(candidate.document, context);
    if (!errors.empty()) return {{}, std::move(errors)};
    const bool child_styles = std::any_of(composed_slots.begin(), composed_slots.end(), [](const auto& slot) {
        return std::any_of(slot.second.begin(), slot.second.end(), [](const auto& child) { return !child.overrides.empty(); });
    });
    if (!description.style_bindings.empty() || child_styles) {
        std::map<std::string, StyleDeclarations, std::less<>> mapped_styles;
        for (const auto& binding : description.style_bindings) {
            const auto& declarations = style_values.at(binding.style);
            mapped_styles.emplace(binding.target, declarations);
            if (declarations != StyleDeclarations{})
                candidate.stages = candidate.stages | PropertyStages::style | PropertyStages::layout |
                    PropertyStages::paint | PropertyStages::input | PropertyStages::semantics;
        }
        style_overrides(description.document.root, mapped_styles, description, arguments, composed_slots, candidate.overrides);
    }
    const bool child_sources = std::any_of(composed_slots.begin(), composed_slots.end(), [](const auto& slot) {
        return std::any_of(slot.second.begin(), slot.second.end(), [](const auto& child) { return child.sources.has_value(); });
    });
    if (description.sources || child_sources) {
        candidate.sources.emplace();
        if (description.sources) candidate.sources->file = description.sources->file;
        std::size_t index = 0;
        component_sources(description.document.root, "/root", description, arguments, composed_slots, index, candidate.sources->nodes);
    }
    return {std::move(candidate), {}};
}
} // namespace

Result<BoundComponent> lower_component(const ReactiveRuntime& runtime, const ComponentDescription& description,
                                     const ComponentArguments& arguments, const ValidationContext& context) {
    if (input_limit(description, arguments, arguments.composed_slots.size()))
        return {{}, {{"component_limit", Severity::error, "/component", "Supply at most 256 props, slots, actions and styles each, and 4096 combined property/event/style mappings.", {}}}};
    ComposedSlots slots;
    for (const auto& [key, children] : arguments.composed_slots) slots.emplace(key, children);
    return lower_component_impl(runtime, description, arguments, slots, context);
}

Result<BoundComponent> lower_component_tree(const ReactiveRuntime& runtime, std::span<const ComponentInvocation> invocations,
                                          std::size_t root, const ValidationContext& context) {
    Errors errors;
    if (invocations.empty() || invocations.size() > max_component_invocations) {
        error(errors, "component_invocation_limit", "/invocations", "Supply between 1 and 4096 invocation records.");
        return {{}, std::move(errors)};
    }
    if (root >= invocations.size()) {
        error(errors, "component_invocation_index", "/root", "Select a root index within the invocation array.");
        return {{}, std::move(errors)};
    }
    const auto location = [](std::size_t i) { return "/invocations/" + std::to_string(i); };
    std::vector<unsigned char> visits(invocations.size());
    std::vector<std::size_t> order;
    auto visit = [&](auto&& self, std::size_t i, std::size_t depth, const std::string& path) -> bool {
        if (i >= invocations.size()) {
            error(errors, "component_invocation_index", path, "Use a child index within the invocation array.");
            return false;
        }
        if (visits[i]) {
            error(errors, visits[i] == 1 ? "component_invocation_cycle" : "component_invocation_reuse", path,
                  "Each invocation must occur once in an acyclic tree; supply a separate record for each instance.");
            return false;
        }
        if (depth > max_document_depth) {
            error(errors, "component_document_limit", path, "Keep the invocation tree within 64 levels.");
            return false;
        }
        visits[i] = 1;
        const auto& invocation = invocations[i];
        const auto& arguments = invocation.arguments;
        if (input_limit(invocation.component, arguments, arguments.composed_slots.size()) ||
            invocation.invocation_slots.size() > max_component_slots - arguments.slots.size() - arguments.composed_slots.size()) {
            error(errors, "component_limit", location(i) + "/component", "Supply at most 256 props, slots, actions and styles each, and 4096 combined mappings; all three slot maps share the slot budget.");
            return false;
        }
        for (const auto& [key, children] : invocation.invocation_slots) {
            const auto slot_path = location(i) + "/slots/" + detail::pointer_token(key);
            const auto& declarations = invocation.component.slots;
            const auto slot = std::find_if(declarations.begin(), declarations.end(), [&](const auto& s) { return s.name == key; });
            if (slot == declarations.end()) {
                error(errors, "component_unknown_slot", slot_path, "Supply only declared slots.");
                return false;
            }
            if (arguments.slots.contains(key) || arguments.composed_slots.contains(key)) {
                error(errors, "component_duplicate_slot_argument", slot_path, "Give this slot one raw, composed or invocation writer.");
                return false;
            }
            if (children.size() < slot->minimum || children.size() > slot->maximum || children.size() > max_document_nodes) {
                error(errors, "component_slot_count", slot_path, "Supply the declared number of invocation children.");
                return false;
            }
        }
        // Declared slot order, then supplied child order, determines postorder.
        for (const auto& slot : invocation.component.slots) {
            const auto found = invocation.invocation_slots.find(slot.name);
            if (found == invocation.invocation_slots.end()) continue;
            for (std::size_t j = 0; j < found->second.size(); ++j) {
                const auto child = found->second[j];
                const auto child_path = location(i) + "/slots/" + detail::pointer_token(slot.name) + "/" + std::to_string(j);
                if (!self(self, child, depth + 1, child_path)) return false;
                if (!contains(slot.accepted, invocations[child].component.document.root.kind)) {
                    error(errors, "component_slot_kind", child_path + "/type", "Supply a root node kind accepted by this slot.");
                    return false;
                }
            }
        }
        visits[i] = 2;
        order.push_back(i);
        return true;
    };
    if (!visit(visit, root, 1, "/root")) return {{}, std::move(errors)};
    for (std::size_t i = 0; i < visits.size(); ++i) if (!visits[i]) {
        error(errors, "component_invocation_unreachable", location(i), "Connect every invocation to the selected root or remove it.");
        return {{}, std::move(errors)};
    }

    // Count all authored nodes once before any subtree copying. Each record and
    // each supplied child contributes to the final tree; no shared expansion.
    std::size_t count = 0;
    auto measure = [&](auto&& self, const UiNode& node, std::size_t depth, std::size_t& height) -> bool {
        if (++count > max_document_nodes || depth > max_document_depth) return false;
        height = std::max(height, depth);
        for (const auto& child : node.children) if (!self(self, child, depth + 1, height)) return false;
        return true;
    };
    std::vector<std::size_t> heights(invocations.size());
    for (const auto i : order) {
        const auto& invocation = invocations[i];
        const auto base = location(i);
        if (!measure(measure, invocation.component.document.root, 1, heights[i])) {
            error(errors, "component_document_limit", base + "/component/document/root", "Keep all invocation input nodes within 64 levels and 10000 nodes combined.");
            return {{}, std::move(errors)};
        }
        std::map<std::string, Target, std::less<>> targets;
        std::size_t template_count = 0;
        index(invocation.component.document.root, "/root", 1, targets, template_count);
        // Measure raw/composed children even for invalid/unknown slot names so
        // malformed input cannot bypass the pre-copy aggregate budget.
        auto supplied = [&](const auto& slots, auto get_node) {
            for (const auto& [key, children] : slots) for (std::size_t j = 0; j < children.size(); ++j) {
                std::size_t height = 0;
                const auto path = base + "/slots/" + detail::pointer_token(key) + "/" + std::to_string(j);
                if (!measure(measure, get_node(children[j]), 1, height)) {
                    error(errors, "component_document_limit", path, "Keep all invocation input nodes within 64 levels and 10000 nodes combined.");
                    return false;
                }
                for (const auto& slot : invocation.component.slots) if (slot.name == key) {
                    const auto target = targets.find(slot.target);
                    if (target != targets.end()) heights[i] = std::max(heights[i], target->second.depth + height);
                }
            }
            return true;
        };
        if (!supplied(invocation.arguments.slots, [](const UiNode& node) -> const UiNode& { return node; }) ||
            !supplied(invocation.arguments.composed_slots, [](const BoundComponent& child) -> const UiNode& { return child.document.root; }))
            return {{}, std::move(errors)};
        for (const auto& slot : invocation.component.slots) {
            const auto found = invocation.invocation_slots.find(slot.name);
            const auto target = targets.find(slot.target);
            if (found == invocation.invocation_slots.end() || target == targets.end()) continue;
            for (const auto child : found->second) heights[i] = std::max(heights[i], target->second.depth + heights[child]);
        }
        if (heights[i] > max_document_depth) {
            error(errors, "component_document_limit", base + "/component/document/root", "Keep the composed invocation document within 64 levels.");
            return {{}, std::move(errors)};
        }
    }

    std::vector<std::optional<BoundComponent>> results(invocations.size());
    for (const auto i : order) {
        const auto& invocation = invocations[i];
        std::map<std::string, std::vector<BoundComponent>, std::less<>> children;
        ComposedSlots slots;
        for (const auto& [key, values] : invocation.arguments.composed_slots) slots.emplace(key, values);
        for (const auto& [key, indices] : invocation.invocation_slots) {
            auto& values = children[key];
            values.reserve(indices.size());
            for (const auto child : indices) {
                values.push_back(std::move(*results[child]));
                results[child].reset();
            }
            slots.emplace(key, values);
        }
        auto lowered = lower_component_impl(runtime, invocation.component, invocation.arguments, slots, context);
        if (!lowered) {
            for (auto& diagnostic : lowered.diagnostics) {
                // Whole-graph entry failures keep their graph location.
                if (!diagnostic.path.starts_with("/reactive")) diagnostic.path = location(i) + diagnostic.path;
            }
            return {{}, std::move(lowered.diagnostics)};
        }
        results[i] = std::move(*lowered.value);
    }
    return {std::move(results[root]), {}};
}
} // namespace tessera
