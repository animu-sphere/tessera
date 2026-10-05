#include <tessera/semantics/semantics.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include <algorithm>
#include <utility>

namespace tessera {
namespace {

const std::string* text_of(const UiNode& node) {
    if (node.kind != NodeKind::text) return nullptr;
    const auto property = node.properties.find(property_names::text);
    return property == node.properties.end() ? nullptr : std::get_if<std::string>(&property->second);
}

// Joins nonempty runs with one space; whitespace inside a run is preserved.
void append(std::string& name, std::string_view run) {
    if (run.empty()) return;
    if (!name.empty()) name += ' ';
    name += run;
}

// Explicit references include hidden and display-none content and never follow nested labelled_by.
void subtree_text(const UiNode& node, std::string& out) {
    if (const auto* text = text_of(node)) append(out, *text);
    for (const auto& child : node.children) subtree_text(child, out);
}

class Projection {
public:
    explicit Projection(const SemanticInput& input) : input_(input) {}

    SemanticTree build() {
        const auto& boxes = input_.layout->boxes;
        // Nearest included entry for each box, or no_semantic_parent.
        std::vector<std::uint32_t> owner(boxes.size(), no_semantic_parent);
        std::vector<bool> disabled(boxes.size(), false);
        std::vector<std::string> content;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto& box = boxes[i];
            const auto& node = *input_.tree->get(box.node);
            const auto inherited = box.parent == no_layout_parent ? no_semantic_parent : owner[box.parent];
            disabled[i] = std::get<bool>(*effective_property(node, property_names::disabled)) ||
                          (box.parent != no_layout_parent && disabled[box.parent]);
            owner[i] = inherited;
            if (!box.visible) continue;

            const auto binding = node.events.find("activate");
            const auto* activate = binding == node.events.end() ? nullptr : &binding->second;
            const auto* text = text_of(node);
            std::optional<SemanticRole> role;
            if (activate) role = SemanticRole::button;
            else if (text && !(inherited != no_semantic_parent && tree_.nodes[inherited].role == SemanticRole::button))
                role = SemanticRole::text;
            else if (box.parent == no_layout_parent && node.kind == NodeKind::box) role = SemanticRole::root;
            if (!role) {
                // Presentational text names its button; other Boxes are flattened.
                if (text) append(content[inherited], *text);
                continue;
            }

            SemanticNode entry{box.node, node.id, inherited, static_cast<std::uint32_t>(i), *role};
            entry.enabled = !disabled[i];
            entry.focusable = std::get<bool>(*effective_property(node, property_names::focusable));
            if (activate && entry.enabled) entry.actions.push_back({"activate", *activate});
            if (const auto* reference = effective_property(node, property_names::labelled_by)) {
                entry.labelled_by = input_.tree->find(std::get<NodeReference>(*reference).id);
                subtree_text(*input_.tree->get(*entry.labelled_by), entry.name);
                if (!entry.name.empty()) entry.name_source = NameSource::relationship;
            }
            owner[i] = static_cast<std::uint32_t>(tree_.nodes.size());
            tree_.nodes.push_back(std::move(entry));
            content.emplace_back(text ? *text : std::string());
        }
        for (std::size_t i = 0; i < tree_.nodes.size(); ++i) {
            auto& entry = tree_.nodes[i];
            if (entry.name_source != NameSource::none || content[i].empty()) continue;
            entry.name = std::move(content[i]);
            entry.name_source = NameSource::content;
        }
        return std::move(tree_);
    }

private:
    const SemanticInput& input_;
    SemanticTree tree_;
};

} // namespace

std::vector<Diagnostic> validate(const SemanticInput& input) {
    return detail::validate_layout_snapshot(input.tree, input.styles, input.layout);
}

Result<SemanticTree> build_semantic_tree(const SemanticInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto tree = Projection(input).build();
    std::vector<Diagnostic> warnings;
    for (const auto& entry : tree.nodes) {
        if (entry.role == SemanticRole::button && entry.name.empty())
            warnings.push_back({"missing_name", Severity::warning, "/nodes/" + std::to_string(entry.node.index),
                                "Give the button visible Text content or a labelled_by reference to named text.", {}});
    }
    return {std::move(tree), std::move(warnings)};
}

Result<ActionRequest> request_semantic_action(const SemanticInput& input, NodeHandle target, std::string_view binding) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    detail::Checker check(errors);
    if (!input.tree->get(target)) {
        check.error("stale_target", "/target", "Resolve the target again in the current snapshot; this handle belongs to another tree.");
        return {std::nullopt, std::move(errors)};
    }
    const auto& boxes = input.layout->boxes;
    const auto box = std::find_if(boxes.begin(), boxes.end(), [&](const LayoutBox& item) { return item.node == target; });
    if (box == boxes.end() || !box->visible) {
        check.error("hidden_target", "/target", "The target is display-none or hidden and exposes no semantic actions.");
        return {std::nullopt, std::move(errors)};
    }
    const auto tree = Projection(input).build();
    const auto entry = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                                    [&](const SemanticNode& item) { return item.node == target; });
    if (entry != tree.nodes.end() && !entry->enabled) {
        check.error("disabled_target", "/target", "The target or an ancestor is disabled.");
        return {std::nullopt, std::move(errors)};
    }
    if (entry != tree.nodes.end()) {
        for (const auto& action : entry->actions) {
            if (action.binding == binding) return {ActionRequest{action.binding, action.action, target}, {}};
        }
    }
    check.error("unsupported_action", "/binding",
                "The target does not expose this action; invoke a button's activate action, not its label or container.");
    return {std::nullopt, std::move(errors)};
}

} // namespace tessera
