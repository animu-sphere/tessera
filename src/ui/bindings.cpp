#include <tessera/ui/bindings.hpp>
#include "json_detail.hpp"
#include <algorithm>

namespace tessera {
namespace {
std::string_view name(BoundProperty property) {
    switch (property) {
    case BoundProperty::text: return property_names::text;
    case BoundProperty::focusable: return property_names::focusable;
    case BoundProperty::disabled: return property_names::disabled;
    case BoundProperty::labelled_by: return property_names::labelled_by;
    }
    return {};
}
struct Target { UiNode* node; std::string path; };
void index(UiNode& node, std::string path, std::map<std::string, Target, std::less<>>& targets) {
    if (node.id) targets.emplace(*node.id, Target{&node, path});
    for (std::size_t i = 0; i < node.children.size(); ++i)
        index(node.children[i], path + "/children/" + std::to_string(i), targets);
}
} // namespace

Result<std::size_t> PropertyBindings::add(std::string target, BoundProperty property, Value value) {
    const auto path = "/bindings/" + std::to_string(entries_.size());
    if (entries_.size() >= max_property_bindings)
        return {{}, {{"binding_limit", Severity::error, "/bindings", "Supply at most 4096 property bindings.", {}}}};
    const bool controls = std::any_of(target.begin(), target.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    if (target.empty() || target.size() > 256 || controls || !detail::valid_utf8(target))
        return {{}, {{"invalid_binding_target", Severity::error, path + "/target",
            "Supply an author ID of 1 to 256 UTF-8 bytes without control characters.", {}}}};
    for (const auto& entry : entries_)
        if (entry.target == target && entry.property == property)
            return {{}, {{"duplicate_binding", Severity::error, path + "/property",
                "Bind each target property once; compose competing values in the reactive graph.", {}}}};
    entries_.push_back({std::move(target), property, std::move(value)});
    return {entries_.size() - 1, {}};
}

Result<BoundDocument> PropertyBindings::apply(const ReactiveRuntime& runtime, const UiDocument& authored,
                                            const ValidationContext& context) const {
    auto errors = validate(authored, context);
    if (!errors.empty()) return {{}, std::move(errors)};
    BoundDocument candidate{authored};
    std::map<std::string, Target, std::less<>> targets;
    index(candidate.document.root, "/root", targets);
    // Reject all structural errors before reading a reactive value.
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        const auto found = targets.find(entry.target);
        if (found == targets.end()) {
            errors.push_back({"binding_target_missing", Severity::error, "/bindings/" + std::to_string(i) + "/target",
                "Add a node with author ID '" + entry.target + "' or remove its binding.", {}});
        } else if (!contains(find_property_descriptor(name(entry.property))->accepted, found->second.node->kind)) {
            errors.push_back({"binding_node_kind", Severity::error, found->second.path + "/properties/" + std::string(name(entry.property)),
                "Bind this property to a node kind accepted by its property descriptor.", {}});
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    try { runtime.check_settled(); }
    catch (const ReactiveError& e) { return {{}, {e.diagnostic()}}; }
    for (const auto& entry : entries_) {
        const auto& target = targets.at(entry.target);
        const auto property = name(entry.property);
        try {
            const Property value = std::visit([&](const auto& source) -> Property { return runtime.snapshot(source); }, entry.value);
            const auto old = target.node->properties.find(property);
            if (old == target.node->properties.end() || old->second != value)
                candidate.stages = candidate.stages | find_property_descriptor(property)->stages;
            target.node->properties.insert_or_assign(std::string(property), value);
        } catch (const ReactiveError& e) {
            auto cause = e.diagnostic();
            cause.message += " Reactive location: " + cause.path + ".";
            cause.path = target.path + "/properties/" + std::string(property);
            errors.push_back(std::move(cause));
        }
    }
    if (!errors.empty()) return {{}, std::move(errors)};
    errors = validate(candidate.document, context);
    if (!errors.empty()) return {{}, std::move(errors)};
    return {std::move(candidate), {}};
}
} // namespace tessera
