#pragma once

#include <tessera/input/event.hpp>
#include <tessera/layout/layout_box.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tessera {

// Prototype in-process projection; not a serialized schema or stable API. Roles are derived from the
// existing vocabulary because JSON v1 has no role property.
enum class SemanticRole : std::uint8_t {
    root,   // The document root when it has no other role.
    button, // A node with an `activate` binding. Text descendants name it and get no entries.
    text,   // A Text node outside a button.
};
enum class NameSource : std::uint8_t { none, relationship, content };

inline constexpr std::uint32_t no_semantic_parent = std::numeric_limits<std::uint32_t>::max();

struct SemanticAction {
    std::string binding; // Event binding name; only "activate" is exposed.
    std::string action;  // Host action name from the node's binding.
    bool operator==(const SemanticAction&) const = default;
};

// One included node. Handles and the layout index belong to the input snapshot.
struct SemanticNode {
    NodeHandle node;
    std::optional<std::string> id; // Document-scoped author ID, when present.
    std::uint32_t parent = no_semantic_parent; // Index into SemanticTree::nodes.
    std::uint32_t layout_box = 0;              // Index into LayoutResult::boxes for bounds.
    SemanticRole role = SemanticRole::root;
    std::string name;
    NameSource name_source = NameSource::none;
    std::optional<NodeHandle> labelled_by; // Referenced node; it need not have an entry.
    bool enabled = true;                   // False when the node or an ancestor is disabled.
    bool focusable = false;                // Authored intent; there is no focus runtime yet.
    std::vector<SemanticAction> actions;   // Eligible actions only; empty when disabled.
    bool operator==(const SemanticNode&) const = default;
};

// Entries in tree preorder; non-semantic Boxes are flattened into their nearest included ancestor.
struct SemanticTree {
    std::vector<SemanticNode> nodes;
    bool operator==(const SemanticTree&) const = default;
};

// Borrowed for one projection or invocation; use a coherent tree/styles/layout snapshot.
struct SemanticInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    const LayoutResult* layout = nullptr;
};

std::vector<Diagnostic> validate(const SemanticInput&);

// Display-none and locally hidden nodes are excluded; visible descendants of a hidden node attach to
// the nearest included ancestor. A successful result may carry missing_name warnings for buttons.
Result<SemanticTree> build_semantic_tree(const SemanticInput&);

// Invokes a semantic action through the ordinary eligibility rules and returns the same ActionRequest
// a pointer activation would. Rejects stale, hidden, and disabled targets and unexposed bindings.
Result<ActionRequest> request_semantic_action(const SemanticInput&, NodeHandle target, std::string_view binding);

} // namespace tessera
