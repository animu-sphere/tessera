#pragma once

#include <tessera/ui/bindings.hpp>

namespace tessera {
inline constexpr std::size_t max_component_props = 256;
inline constexpr std::size_t max_component_slots = 256;

// Required unless a literal default is supplied. Types are never coerced.
struct ComponentProp {
    std::string name;
    PropertyType type = PropertyType::string;
    std::optional<Property> default_value;
};
struct ComponentProperty {
    std::string prop;
    std::string target;   // Author ID in the template, not in slot content.
    std::string property; // A reflected semantic property.
};
// Ordered children inserted into one empty Box in the template. Accepted kinds
// constrain each supplied child's root; ordinary validation checks descendants.
struct ComponentSlot {
    std::string name;
    std::string target;
    NodeKinds accepted = NodeKinds::all;
    std::size_t minimum = 0;
    std::size_t maximum = max_document_nodes;
};
// Data-only composition IR. The host supplies document-unique author IDs and
// uses `kind` with KeyedCollection for instance ownership/local state.
struct ComponentDescription {
    std::string kind;
    UiDocument document;
    std::vector<ComponentProp> props;
    std::vector<ComponentProperty> properties;
    std::vector<ComponentSlot> slots;
};
// Reactive alternatives are weak references to the host's instance owner.
using ComponentValue = std::variant<Property,
    Signal<bool>, Computed<bool>, Signal<double>, Computed<double>,
    Signal<std::string>, Computed<std::string>, Signal<NodeReference>, Computed<NodeReference>>;
struct ComponentArguments {
    std::map<std::string, ComponentValue, std::less<>> props;
    std::map<std::string, std::vector<UiNode>, std::less<>> slots;
};

// Validates schema/arguments before reading settled values or copying subtrees.
// Returns an ordinary, fully validated document; never creates/disposes owners,
// evaluates calculations, publishes a generation, or invokes external code.
Result<BoundDocument> lower_component(const ReactiveRuntime&, const ComponentDescription&,
                                     const ComponentArguments&, const ValidationContext& = {});
} // namespace tessera
