#include <tessera/style/style_sheet.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <string>

namespace tessera {
namespace {

std::string index_path(std::string_view base, std::size_t index) { return std::string(base) + "/" + std::to_string(index); }

void check_handle(const UiTree& tree, NodeHandle node, const std::string& path, detail::Checker& check) {
    if (node.tree != tree.root().tree || !tree.get(node))
        check.error("stale_target", path, "Resolve the node again in the current tree; this handle belongs to another tree.");
}

struct NodeStates {
    bool hover = false, active = false, focus = false, disabled = false;
};

bool matches(const StyleSelector& selector, const UiNode& node, const NodeStates& states) {
    switch (selector.kind) {
    case StyleSelector::Kind::type: if (node.kind != selector.type) return false; break;
    case StyleSelector::Kind::class_name:
        if (std::find(node.classes.begin(), node.classes.end(), selector.name) == node.classes.end()) return false;
        break;
    case StyleSelector::Kind::id: if (node.id != selector.name) return false; break;
    }
    const auto& required = selector.states;
    return (!required.hover || states.hover) && (!required.active || states.active) &&
           (!required.focus || states.focus) && (!required.disabled || states.disabled);
}

} // namespace

void apply(const StyleDeclarations& d, ResolvedStyle& s) {
    const auto set = [](const auto& declared, auto& value) { if (declared) value = *declared; };
    set(d.display, s.display);
    set(d.direction, s.direction);
    set(d.justify, s.justify);
    set(d.align, s.align);
    set(d.overflow, s.overflow);
    set(d.width, s.width);
    set(d.height, s.height);
    set(d.min_width, s.min_width);
    set(d.min_height, s.min_height);
    set(d.max_width, s.max_width);
    set(d.max_height, s.max_height);
    set(d.margin, s.margin);
    set(d.border, s.border);
    set(d.padding, s.padding);
    set(d.gap, s.gap);
    set(d.grow, s.grow);
    set(d.shrink, s.shrink);
    set(d.visibility, s.visibility);
    set(d.opacity, s.opacity);
    set(d.background, s.background);
    set(d.border_color, s.border_color);
    set(d.corner_radius, s.corner_radius);
    set(d.text.font, s.text.font);
    set(d.text.size, s.text.size);
    set(d.text.line_height, s.text.line_height);
    set(d.text.weight, s.text.weight);
    set(d.color, s.color);
}

// Primitive defaults are valid, so only declared values (and a min/max pair declared together) report.
std::vector<Diagnostic> validate(const StyleDeclarations& declarations, std::string_view path) {
    ResolvedStyle style;
    apply(declarations, style);
    return validate(style, path);
}

std::vector<Diagnostic> validate(const StyleSheet& sheet) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    for (std::size_t i = 0; i < sheet.rules.size(); ++i) {
        const auto& rule = sheet.rules[i];
        const auto path = index_path("/rules", i);
        const auto& selector = rule.selector;
        check.enumeration(selector.kind, StyleSelector::Kind::id, path + "/selector/kind");
        if (selector.kind == StyleSelector::Kind::type)
            check.enumeration(selector.type, NodeKind::text, path + "/selector/type");
        else if (selector.kind == StyleSelector::Kind::class_name || selector.kind == StyleSelector::Kind::id) {
            const auto& name = selector.name;
            if (!detail::valid_utf8(name)) check.error("invalid_utf8", path + "/selector/name", "Use valid UTF-8 text.");
            else if (name.empty() || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
                check.error("invalid_identifier", path + "/selector/name",
                            "Class names and IDs must be nonempty and contain no control characters.");
        }
        auto declared = validate(rule.declarations, path + "/declarations");
        errors.insert(errors.end(), declared.begin(), declared.end());
    }
    return errors;
}

std::vector<Diagnostic> validate(const StyleInput& input) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (!input.tree) check.error("missing_input", "/tree", "Provide the UiTree to style.");
    if (!input.sheet) check.error("missing_input", "/sheet", "Provide a StyleSheet; it may have no rules.");
    else {
        auto sheet = validate(*input.sheet);
        for (auto& diagnostic : sheet) diagnostic.path = "/sheet" + diagnostic.path;
        errors.insert(errors.end(), sheet.begin(), sheet.end());
    }
    if (input.tree) {
        const auto& tree = *input.tree;
        for (std::size_t i = 0; i < input.state.hovered.size(); ++i)
            check_handle(tree, input.state.hovered[i], index_path("/state/hovered", i), check);
        for (std::size_t i = 0; i < input.state.active.size(); ++i)
            check_handle(tree, input.state.active[i], index_path("/state/active", i), check);
        if (input.state.focused) check_handle(tree, *input.state.focused, "/state/focused", check);
        if (!input.overrides.empty() && input.overrides.size() != tree.size())
            check.error("override_count", "/overrides",
                        "Provide no overrides or one per tree node (" + std::to_string(tree.size()) + "), indexed by NodeHandle::index.");
    }
    for (std::size_t i = 0; i < input.overrides.size(); ++i) {
        auto declared = validate(input.overrides[i], index_path("/overrides", i));
        errors.insert(errors.end(), declared.begin(), declared.end());
    }
    return errors;
}

Result<std::vector<ResolvedStyle>> resolve_styles(const StyleInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto& tree = *input.tree;
    const auto& rules = input.sheet->rules;

    // Tree indices are preorder, so every parent precedes its children.
    std::vector<std::uint32_t> parents(tree.size(), 0);
    for (std::uint32_t i = 0; i < tree.size(); ++i)
        for (const auto child : tree.children({tree.root().tree, i})) parents[child.index] = i;

    std::vector<NodeStates> states(tree.size());
    const auto mark = [&](const std::vector<NodeHandle>& nodes, bool NodeStates::*state) {
        for (const auto node : nodes) {
            for (auto index = node.index;; index = parents[index]) {
                if (states[index].*state) break; // Ancestors are already marked.
                states[index].*state = true;
                if (index == 0) break;
            }
        }
    };
    mark(input.state.hovered, &NodeStates::hover);
    mark(input.state.active, &NodeStates::active);
    if (input.state.focused) states[input.state.focused->index].focus = true;
    for (std::uint32_t i = 0; i < tree.size(); ++i) {
        auto& node = states[i];
        node.disabled = std::get<bool>(*effective_property(*tree.get({tree.root().tree, i}), property_names::disabled)) ||
                        (i != 0 && states[parents[i]].disabled);
        if (node.disabled) node.hover = node.active = node.focus = false;
    }

    std::vector<ResolvedStyle> styles(tree.size());
    for (std::uint32_t i = 0; i < tree.size(); ++i) {
        const auto& node = *tree.get({tree.root().tree, i});
        auto& style = styles[i];
        if (i != 0) {
            style.text = styles[parents[i]].text;
            style.color = styles[parents[i]].color;
        }
        for (const bool stateful : {false, true}) {
            for (const auto& rule : rules) {
                if (rule.selector.states.any() == stateful && matches(rule.selector, node, states[i]))
                    apply(rule.declarations, style);
            }
        }
        if (!input.overrides.empty()) apply(input.overrides[i], style);
        auto resolved = validate(style, index_path("/nodes", i));
        errors.insert(errors.end(), resolved.begin(), resolved.end());
    }
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return {std::move(styles), {}};
}

} // namespace tessera
