#pragma once

#include <tessera/ui/bindings.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/style/style_sheet.hpp>
#include <span>

namespace tessera {
inline constexpr std::size_t max_component_props = 256;
inline constexpr std::size_t max_component_slots = 256;
inline constexpr std::size_t max_component_actions = 256;
inline constexpr std::size_t max_component_styles = 256;
inline constexpr std::size_t max_component_invocations = 4096;

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
// Names a host action argument, required unless a registered default is supplied.
// This declaration contains no callback or command implementation.
struct ComponentAction {
    std::string name;
    std::optional<std::string> default_action;
};
struct ComponentEvent {
    std::string action; // A declared component action, not a host action name.
    std::string target; // Author ID in the template, not in slot content.
    std::string event;  // activate or cancel.
};
// A complete typed instance override, required unless a literal default exists.
struct ComponentStyle {
    std::string name;
    std::optional<StyleDeclarations> default_value;
};
struct ComponentStyleBinding {
    std::string style;  // Declared style argument.
    std::string target; // Template author ID; slot content is opaque.
};
// Data-only composition IR. The host supplies document-unique author IDs and
// uses `kind` with KeyedCollection for instance ownership/local state.
struct ComponentDescription {
    std::string kind;
    UiDocument document;
    std::vector<ComponentProp> props;
    std::vector<ComponentProperty> properties;
    std::vector<ComponentSlot> slots;
    std::vector<ComponentAction> actions;
    std::vector<ComponentEvent> events;
    std::vector<ComponentStyle> styles;
    std::vector<ComponentStyleBinding> style_bindings;
    std::optional<DocumentSourceMap> sources; // Template preorder, outside serialization.
};
// Reactive alternatives are weak references to the host's instance owner.
using ComponentValue = std::variant<Property,
    Signal<bool>, Computed<bool>, Signal<double>, Computed<double>,
    Signal<std::string>, Computed<std::string>, Signal<NodeReference>, Computed<NodeReference>>;
using ComponentActionValue = std::variant<std::string, Signal<std::string>, Computed<std::string>>;
using ComponentStyleValue = std::variant<StyleDeclarations, Signal<StyleDeclarations>, Computed<StyleDeclarations>>;
struct BoundComponent : BoundDocument {
    // Empty without template mappings or child override arrays; otherwise one
    // entry per composed node, directly usable as StyleInput::overrides.
    std::vector<StyleDeclarations> overrides;
    std::optional<DocumentSourceMap> sources; // Composed preorder; missing origins stay explicit.
};
struct ComponentArguments {
    std::map<std::string, ComponentValue, std::less<>> props;
    std::map<std::string, std::vector<UiNode>, std::less<>> slots;
    std::map<std::string, ComponentActionValue, std::less<>> actions;
    std::map<std::string, ComponentStyleValue, std::less<>> styles;
    // Already-lowered children, preserving their root and preorder overrides.
    // Each slot uses either slots or composed_slots, never both. Child document
    // extensions belong to that document and are not merged into the parent.
    std::map<std::string, std::vector<BoundComponent>, std::less<>> composed_slots;
};

// Validates schema/arguments before reading settled values or copying subtrees.
// Returns an ordinary, fully validated document; never creates/disposes owners,
// evaluates calculations, publishes a generation, or invokes external code.
Result<BoundComponent> lower_component(const ReactiveRuntime&, const ComponentDescription&,
                                     const ComponentArguments&, const ValidationContext& = {});

// A flat, owned invocation tree: slot children are indices into the input span.
// Indices are call-local identities, distinct from keys, author IDs and owners.
// A slot supplies raw, composed or invocation children, with exactly one writer.
struct ComponentInvocation {
    ComponentDescription component;
    ComponentArguments arguments;
    std::map<std::string, std::vector<std::size_t>, std::less<>> invocation_slots;
};

// Borrows the tree for this call, lowers children before parents, and returns
// only the root candidate. All entries must belong to one unshared, acyclic tree.
// Topology and aggregate geometry bounds are checked before reactive reads or
// subtree copies. Ordinary component validation applies to each invocation.
Result<BoundComponent> lower_component_tree(const ReactiveRuntime&, std::span<const ComponentInvocation>,
                                          std::size_t root = 0, const ValidationContext& = {});
} // namespace tessera
