#include <tessera/ui/property_metadata.hpp>
#include <tessera/ui/serialization.hpp>
#include "../check.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <string>

using tessera::test::check;
using tessera::test::has;

namespace {

constexpr std::array types{tessera::PropertyType::boolean, tessera::PropertyType::number,
                           tessera::PropertyType::string, tessera::PropertyType::reference};
constexpr std::array kinds{tessera::NodeKind::box, tessera::NodeKind::text};

// A valid sample of each type and its canonical JSON v1 encoding.
tessera::Property sample(tessera::PropertyType type) {
    switch (type) {
    case tessera::PropertyType::boolean: return true;
    case tessera::PropertyType::number: return 2.5;
    case tessera::PropertyType::string: return std::string("x");
    case tessera::PropertyType::reference: return tessera::NodeReference{"target"};
    }
    return false;
}
std::string encoded(tessera::PropertyType type) {
    switch (type) {
    case tessera::PropertyType::boolean: return "true";
    case tessera::PropertyType::number: return "2.5";
    case tessera::PropertyType::string: return "\"x\"";
    case tessera::PropertyType::reference: return R"({"ref":"target"})";
    }
    return {};
}

// Root Box "target" with one child of `kind`; the child satisfies every other required property.
tessera::UiDocument document(tessera::NodeKind kind, std::string_view except) {
    tessera::UiDocument result;
    result.root.id = "target";
    tessera::UiNode child;
    child.kind = kind;
    for (const auto& descriptor : tessera::property_descriptors()) {
        if (descriptor.name != except && tessera::contains(descriptor.required, kind))
            child.properties.emplace(descriptor.name, sample(descriptor.type));
    }
    result.root.children.push_back(std::move(child));
    return result;
}

void descriptor_table() {
    const auto descriptors = tessera::property_descriptors();
    check(descriptors.size() == 4, "Descriptor vocabulary size differs");
    std::set<std::string_view> names;
    for (const auto& descriptor : descriptors) {
        check(!descriptor.name.empty() && names.insert(descriptor.name).second, "Descriptor names must be unique");
        check(tessera::find_property_descriptor(descriptor.name) == &descriptor, "Lookup must return the table entry");
        check(descriptor.accepted != tessera::NodeKinds::none, "Descriptor accepts no node kind");
        check((static_cast<unsigned>(descriptor.required) & ~static_cast<unsigned>(descriptor.accepted)) == 0,
              "Required kinds must be accepted");
        check(descriptor.stages != tessera::PropertyStages::none, "Authored properties must affect a stage");
        if (descriptor.absent_value)
            check(descriptor.required != descriptor.accepted &&
                  tessera::property_type(*descriptor.absent_value) == descriptor.type,
                  "Absent value must match the descriptor type");
    }
    check(std::is_sorted(descriptors.begin(), descriptors.end(),
                         [](const auto& a, const auto& b) { return a.name < b.name; }), "Descriptors must be ordered by name");
    for (const auto name : {tessera::property_names::text, tessera::property_names::focusable,
                            tessera::property_names::disabled, tessera::property_names::labelled_by})
        check(names.contains(name), "Named property lacks a descriptor");
    check(!tessera::find_property_descriptor("Text") && !tessera::find_property_descriptor(""),
          "Lookup must be exact and case-sensitive");

    const auto& text = *tessera::find_property_descriptor(tessera::property_names::text);
    check(text.type == tessera::PropertyType::string && text.required == tessera::NodeKinds::text &&
          tessera::affects(text.stages, tessera::PropertyStages::layout) &&
          tessera::affects(text.stages, tessera::PropertyStages::paint) &&
          tessera::affects(text.stages, tessera::PropertyStages::semantics), "Text descriptor differs");
    const auto& disabled = *tessera::find_property_descriptor(tessera::property_names::disabled);
    check(tessera::affects(disabled.stages, tessera::PropertyStages::input), "Disabled must invalidate input targeting");
    const auto& focusable = *tessera::find_property_descriptor(tessera::property_names::focusable);
    check(tessera::affects(focusable.stages, tessera::PropertyStages::input), "Focusable must invalidate focus eligibility");
}

// Every descriptor/kind/type combination must agree with validation and JSON v1 encoding.
void validation_and_encoding_agreement() {
    for (const auto& descriptor : tessera::property_descriptors()) {
        const auto path = "/root/children/0/properties/" + std::string(descriptor.name);
        for (const auto kind : kinds) {
            for (const auto type : types) {
                auto candidate = document(kind, descriptor.name);
                candidate.root.children[0].properties.insert_or_assign(std::string(descriptor.name), sample(type));
                const auto errors = tessera::validate(candidate);
                if (!tessera::contains(descriptor.accepted, kind)) {
                    check(has(errors, "unknown_property", path), "Unaccepted node kind must reject the property");
                } else if (type != descriptor.type) {
                    check(has(errors, "property_type", path), "Mismatched type must be rejected without coercion");
                } else {
                    check(errors.empty(), "Descriptor-typed value must validate");
                    const auto saved = tessera::save_document(candidate);
                    check(saved && saved.value->find('"' + std::string(descriptor.name) + "\":" + encoded(type)) !=
                                       std::string::npos, "Encoded name/value differs from the descriptor");
                    const auto loaded = tessera::load_document(*saved.value);
                    check(loaded && *loaded.value == candidate, "Descriptor-typed value must round trip");
                }
            }
            const auto absent = document(kind, descriptor.name);
            const auto errors = tessera::validate(absent);
            if (tessera::contains(descriptor.required, kind)) {
                check(has(errors, "missing_property", path), "Required property absence must be rejected");
            } else {
                check(errors.empty(), "Optional property absence must validate");
                const auto* value = tessera::effective_property(absent.root.children[0], descriptor.name);
                check(descriptor.absent_value ? value == &*descriptor.absent_value : value == nullptr,
                      "Effective absent value must come from the descriptor");
                const auto restored = tessera::load_document(*tessera::save_document(absent).value);
                check(restored && !restored.value->root.children[0].properties.contains(descriptor.name),
                      "Absent values must not be materialized");
            }
        }
    }
    tessera::UiNode authored;
    authored.properties[std::string(tessera::property_names::focusable)] = true;
    check(*tessera::effective_property(authored, tessera::property_names::focusable) == tessera::Property{true},
          "Authored value must override the absent value");
    check(!tessera::effective_property(authored, "unknown"), "Unknown absent property has no effective value");
}

} // namespace

int main() {
    try {
        descriptor_table();
        validation_and_encoding_agreement();
        std::cout << "Property descriptor, validation, default, and encoding agreement checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
