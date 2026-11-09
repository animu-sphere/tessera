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
// existing vocabulary, including Box control markers and bounded collection roles.
enum class SemanticRole : std::uint8_t {
    root,      // The document root when it has no other role.
    button,    // A node with an `activate` binding. Text descendants name it and get no entries.
    text,      // A Text node outside a button.
    separator, // A Box with separator=true; its activate binding requests host adjustment.
    dialog,    // The root-sibling Modal; labelled_by supplies its title.
    list,
    tree,
    list_item,
    tree_item,
    menu,
    menu_item,
    scrollbar, // A Box with scrollbar=true; activation requests host-owned scrolling.
    radio_group,
    radio,        // A radio_group option; `selected` is its checked state.
    checkbox,     // A Box with toggle_role=checkbox; `selected` is its checked state.
    switch_,      // A Box with toggle_role=switch (encoded "switch"); `selected` is its on state.
    text_input,   // A Box with edit_role=text_input; `value` is its presented text.
    number_input, // A Box with edit_role=number_input; `value` is its presented draft text.
    tab_list,
    tab,      // A tab_list entry; `selected` marks the tab whose panel is shown.
    toolbar,  // A Box with collection_role=toolbar grouping ordinary controls.
    combobox, // A select-only trigger; `value` is the selected option label, `expanded` its list state.
    listbox,  // The root-sibling option list of a combobox.
    option,   // A listbox entry; `selected` marks the accepted choice.
    group,    // A Box with collection_role=group naming related ordinary controls, such as VectorInput components.
};
enum class NameSource : std::uint8_t { none, relationship, content };

inline constexpr std::uint32_t no_semantic_parent = std::numeric_limits<std::uint32_t>::max();

struct SemanticAction {
    std::string binding; // "activate", or the currently applicable tree "expand"/"collapse".
    std::string action;  // Host action name from the node's binding.
    bool operator==(const SemanticAction&) const = default;
};

// One included node. Handles and the layout index belong to the input snapshot.
struct SemanticNode {
    NodeHandle node;
    std::optional<std::string> id;             // Document-scoped author ID, when present.
    std::uint32_t parent = no_semantic_parent; // Index into SemanticTree::nodes.
    std::uint32_t layout_box = 0;              // Index into LayoutResult::boxes for bounds.
    SemanticRole role = SemanticRole::root;
    std::string name;
    NameSource name_source = NameSource::none;
    std::optional<NodeHandle> labelled_by; // Referenced node; it need not have an entry.
    bool enabled = true;                   // False when disabled or blocked by an active Modal.
    bool focusable = false;                // Focus eligibility, as FocusDispatcher decides it.
    bool focused = false;                  // The supplied focus is this eligible node.
    std::vector<SemanticAction> actions;   // Eligible actions only; empty when disabled.
    std::optional<bool> selected;          // Collection items, radios, tabs and toggles only; independent of focus.
    std::optional<bool> expanded;          // Tree branches and comboboxes only; absent for leaves.
    std::optional<std::string> value;      // Text/number inputs: presented text, never a parsed number;
                                           // comboboxes: the selected option label.
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
    std::optional<NodeHandle> focused; // Host focus for this snapshot, e.g. FocusDispatchResult::focused.
};

std::vector<Diagnostic> validate(const SemanticInput&);

// Display-none and locally hidden nodes are excluded; visible descendants of a hidden node attach to
// the nearest included ancestor. A successful result may carry missing_name warnings for buttons and a
// focus_not_exposed warning when an eligible focused node has no entry.
Result<SemanticTree> build_semantic_tree(const SemanticInput&);

// Invokes a semantic action through the ordinary eligibility rules and returns the same ActionRequest
// a pointer activation would. Rejects stale, hidden, and disabled targets and unexposed bindings.
Result<ActionRequest> request_semantic_action(const SemanticInput&, NodeHandle target, std::string_view binding);

} // namespace tessera
