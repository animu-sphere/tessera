#include "../detail/layout_snapshot.hpp"
#include "../detail/checks.hpp"
#include <cmath>
#include <string>

namespace tessera::detail {

std::vector<Diagnostic> validate_layout_snapshot(const UiTree* tree, std::span<const ResolvedStyle> styles,
                                                 const LayoutResult* layout) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    if (!tree) check.error("missing_input", "/tree", "Provide the UiTree used for layout.");
    if (!layout) check.error("missing_input", "/layout", "Provide the LayoutResult for this tree and styles.");
    if (tree && styles.size() != tree->size())
        check.error("style_count", "/styles", "Provide one resolved style per tree node, indexed by NodeHandle::index.");
    for (std::size_t i = 0; i < styles.size(); ++i) {
        auto style = validate(styles[i], "/styles/" + std::to_string(i));
        errors.insert(errors.end(), style.begin(), style.end());
    }
    if (!errors.empty()) return errors;

    struct Expected { NodeHandle node; std::uint32_t parent; };
    std::vector<Expected> expected;
    const auto visit = [&](const auto& self, NodeHandle node, std::uint32_t parent) -> void {
        if (styles[node.index].display == Display::none) return;
        const auto index = static_cast<std::uint32_t>(expected.size());
        expected.push_back({node, parent});
        for (const auto child : tree->children(node)) self(self, child, index);
    };
    visit(visit, tree->root(), no_layout_parent);
    const auto& boxes = layout->boxes;
    if (boxes.size() != expected.size())
        check.error("layout_count", "/layout/boxes", "Recompute layout: exactly one box per displayed node is required.");
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        const auto path = "/layout/boxes/" + std::to_string(i);
        check.rect(box.border_box, path + "/border_box");
        check.edges(box.border, path + "/border");
        check.edges(box.padding, path + "/padding");
        check.rect(box.content_box(), path + "/content_box");
        if (box.clip) check.rect(*box.clip, path + "/clip");
        if (i >= expected.size()) continue;
        if (box.node != expected[i].node)
            check.error("layout_node", path + "/node", "Recompute layout for this tree in displayed tree preorder.");
        if (box.parent != expected[i].parent)
            check.error("layout_parent", path + "/parent", "Use the parent's index in LayoutResult::boxes.");
        const auto& style = styles[expected[i].node.index];
        if (box.border != style.border || box.padding != style.padding ||
            box.visible != (style.visibility == Visibility::visible))
            check.error("layout_style", path, "Recompute layout after changing border, padding, or visibility.");
        const auto parent = expected[i].parent;
        const auto clip = parent == no_layout_parent ? std::nullopt :
            descendant_clip(boxes[parent], styles[expected[parent].node.index].overflow);
        if (box.clip != clip)
            check.error("layout_clip", path + "/clip", "Recompute layout after changing overflow or ancestor geometry.");
        if (box.scroll.has_value() != (style.overflow == Overflow::scroll)) {
            check.error("layout_scroll", path + "/scroll", "Recompute layout after changing overflow.");
        } else if (box.scroll) {
            const auto& scroll = *box.scroll;
            const auto limit = box.scroll_limit();
            const auto within = [](float offset, float maximum) { return offset >= 0 && offset <= maximum; };
            if (!std::isfinite(scroll.extent.width) || !std::isfinite(scroll.extent.height) ||
                !within(scroll.offset.x, limit.x) || !within(scroll.offset.y, limit.y))
                check.error("layout_scroll", path + "/scroll",
                            "Recompute layout: the extent must cover the padding box and the offset must lie in "
                            "[0, extent - viewport].");
        }
    }
    return errors;
}

} // namespace tessera::detail
