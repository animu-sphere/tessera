#include <tessera/inspection/inspection.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/checks.hpp"
#include <charconv>
#include <utility>

namespace tessera {
namespace {

bool same_properties(const UiNode& node, const NodeSource& source) {
    if (node.properties.size() != source.properties.size()) return false;
    for (const auto& [name, value] : node.properties) {
        (void)value;
        if (!source.properties.contains(name)) return false;
    }
    return true;
}

// Half-open with double subtraction, matching hit testing.
bool inside(const Rect& rect, Point point) noexcept {
    const double x = static_cast<double>(point.x) - rect.origin.x;
    const double y = static_cast<double>(point.y) - rect.origin.y;
    return x >= 0 && x < rect.size.width && y >= 0 && y < rect.size.height;
}

// Semantic warnings are located at "/nodes/<preorder index>".
std::optional<std::uint32_t> node_index(std::string_view path) {
    constexpr std::string_view prefix = "/nodes/";
    if (!path.starts_with(prefix)) return std::nullopt;
    path.remove_prefix(prefix.size());
    std::uint32_t index = 0;
    const auto [end, error] = std::from_chars(path.data(), path.data() + path.size(), index);
    if (error != std::errc{} || end != path.data() + path.size()) return std::nullopt;
    return index;
}

std::string node_list(const std::vector<std::uint32_t>& matches) {
    std::string result;
    for (const auto index : matches) {
        if (!result.empty()) result += ", ";
        result += "/nodes/" + std::to_string(index);
    }
    return result;
}

} // namespace

std::vector<Diagnostic> validate(const InspectionInput& input) {
    auto errors = validate(SemanticInput{input.tree, input.styles, input.layout, input.focused});
    if (!errors.empty() || !input.sources) return errors;
    detail::Checker check(errors);
    const auto& nodes = input.sources->nodes;
    if (nodes.size() != input.tree->size()) {
        check.error("source_map_mismatch", "/sources/nodes",
                    "Supply the source map loaded with the document this tree was created from.");
        return errors;
    }
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        if (same_properties(*input.tree->get({input.tree->root().tree, i}), nodes[i])) continue;
        check.error("source_map_mismatch", "/sources/nodes/" + std::to_string(i),
                    "Supply the source map loaded with the document this tree was created from.");
        break;
    }
    return errors;
}

Result<InspectionSnapshot> capture_inspection(const InspectionInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    // Shares the snapshot validation above, so projection cannot fail here.
    auto semantics = build_semantic_tree({input.tree, input.styles, input.layout, input.focused});

    const auto& tree = *input.tree;
    InspectionSnapshot snapshot;
    snapshot.generation = tree.root().tree;
    snapshot.elements.resize(tree.size());
    for (std::uint32_t i = 0; i < tree.size(); ++i) {
        const NodeHandle handle{snapshot.generation, i};
        const auto& node = *tree.get(handle);
        auto& element = snapshot.elements[i];
        element.kind = node.kind;
        element.id = node.id;
        element.classes = node.classes;
        element.events = node.events;
        for (const auto& descriptor : property_descriptors()) {
            if (!contains(descriptor.accepted, node.kind)) continue;
            const auto* value = effective_property(node, descriptor.name);
            element.properties.push_back({std::string(descriptor.name),
                                          value ? std::optional<Property>(*value) : std::nullopt,
                                          node.properties.contains(descriptor.name)});
        }
        element.style = input.styles[i];
        for (const auto child : tree.children(handle)) {
            element.children.push_back(child.index);
            snapshot.elements[child.index].parent = i;
        }
        if (input.sources) element.source = input.sources->nodes[i];
    }
    for (const auto& box : input.layout->boxes)
        snapshot.elements[box.node.index].geometry = {box.border_box, box.padding_box(), box.content_box(), box.visible,
                                                     box.clip, box.scroll};
    const auto& entries = semantics.value->nodes;
    for (std::uint32_t i = 0; i < entries.size(); ++i) snapshot.elements[entries[i].node.index].semantic = i;
    snapshot.semantics = std::move(*semantics.value);
    snapshot.diagnostics = std::move(semantics.diagnostics);
    if (input.sources) {
        snapshot.sources = SourceStatus::mapped;
        snapshot.source_file = input.sources->file;
        for (auto& diagnostic : snapshot.diagnostics) {
            if (const auto index = node_index(diagnostic.path); index && *index < tree.size())
                diagnostic.byte_offset = input.sources->nodes[*index].span.begin;
        }
    }
    return {std::move(snapshot), {}};
}

Result<NodeHandle> resolve_target(const InspectionSnapshot& snapshot, const InspectionTarget& target,
                                  std::optional<NodeHandle> scope) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    const auto& elements = snapshot.elements;
    if (scope && (scope->tree != snapshot.generation || scope->index >= elements.size())) {
        check.error("stale_target", "/scope", "Resolve the scope in the captured generation.");
        return {std::nullopt, std::move(errors)};
    }
    // Mark the subtree once; walking ancestors per candidate would be quadratic for deep trees.
    std::vector<bool> included(elements.size(), !scope);
    if (scope) {
        included[scope->index] = true;
        for (std::size_t i = scope->index + 1; i < elements.size(); ++i) {
            const auto parent = elements[i].parent;
            if (parent < i) included[i] = included[parent];
        }
    }
    std::vector<std::uint32_t> matches;
    if (const auto* id = std::get_if<AuthorIdTarget>(&target)) {
        for (std::uint32_t i = 0; i < elements.size(); ++i)
            if (included[i] && elements[i].id == id->id) matches.push_back(i);
    } else if (const auto* semantic = std::get_if<SemanticTarget>(&target)) {
        for (const auto& entry : snapshot.semantics.nodes)
            if (entry.node.index < included.size() && included[entry.node.index] &&
                entry.role == semantic->role && entry.name == semantic->name) matches.push_back(entry.node.index);
    } else if (const auto* path = std::get_if<PathTarget>(&target)) {
        if (!elements.empty()) {
            std::uint32_t current = scope ? scope->index : 0;
            for (std::size_t i = 0; i < path->children.size(); ++i) {
                const auto& children = elements[current].children;
                if (path->children[i] >= children.size()) {
                    check.error("target_not_found", "/target/children/" + std::to_string(i),
                                "The element has " + std::to_string(children.size()) + " children.");
                    return {std::nullopt, std::move(errors)};
                }
                current = children[path->children[i]];
            }
            matches.push_back(current);
        }
    } else {
        const auto position = std::get<PointTarget>(target).position;
        check.point(position, "/target/position");
        if (!errors.empty()) return {std::nullopt, std::move(errors)};
        for (auto i = elements.size(); i-- > 0;) {
            if (!included[i]) continue;
            const auto& geometry = elements[i].geometry;
            if (geometry && geometry->visible && inside(geometry->border_box, position) &&
                (!geometry->clip || inside(*geometry->clip, position))) {
                matches.push_back(static_cast<std::uint32_t>(i));
                break;
            }
        }
    }
    if (matches.empty()) {
        check.error("target_not_found", "/target", "No element in this snapshot matches the target.");
        return {std::nullopt, std::move(errors)};
    }
    if (matches.size() > 1) {
        check.error("ambiguous_target", "/target",
                    std::to_string(matches.size()) + " elements match (" + node_list(matches) +
                        "); use an author ID or a more specific target.");
        return {std::nullopt, std::move(errors)};
    }
    return {NodeHandle{snapshot.generation, matches.front()}, {}};
}

} // namespace tessera
