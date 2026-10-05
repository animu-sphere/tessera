#pragma once

#include <tessera/input/pointer.hpp>
#include <tessera/ui/property_metadata.hpp>
#include <string_view>

namespace tessera::detail {

// Half-open containment; double subtraction avoids float overflow at a finite rectangle's far edge.
inline bool inside(const Rect& rect, Point point) {
    const double x = static_cast<double>(point.x) - rect.origin.x;
    const double y = static_cast<double>(point.y) - rect.origin.y;
    return x >= 0 && x < rect.size.width && y >= 0 && y < rect.size.height;
}

// Input eligibility over one validated snapshot, shared by pointer and focus dispatch. Independent of
// paint generation or renderer state.
class Targeting {
public:
    explicit Targeting(const HitTestInput& input) : input_(input), by_node_(input.tree->size(), no_layout_parent) {
        const auto& boxes = input.layout->boxes;
        disabled_.reserve(boxes.size());
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto& box = boxes[i];
            by_node_[box.node.index] = static_cast<std::uint32_t>(i);
            disabled_.push_back(std::get<bool>(*effective_property(*input.tree->get(box.node), property_names::disabled)) ||
                                (box.parent != no_layout_parent && disabled_[box.parent]));
        }
    }

    std::optional<NodeHandle> hit(Point point) const {
        const auto& boxes = input_.layout->boxes;
        for (std::size_t i = boxes.size(); i > 0; --i) {
            const auto& box = boxes[i - 1];
            if (eligible(i - 1) && inside(box.border_box, point) && (!box.clip || inside(*box.clip, point)))
                return box.node;
        }
        return {};
    }

    // Displayed, locally visible, and not disabled by the node or an ancestor.
    bool eligible(NodeHandle node) const {
        return input_.tree->get(node) && by_node_[node.index] != no_layout_parent && eligible(by_node_[node.index]);
    }
    bool eligible(std::size_t box) const { return input_.layout->boxes[box].visible && !disabled_[box]; }
    // Disabled by the node or an ancestor, regardless of visibility.
    bool disabled(std::size_t box) const { return disabled_[box]; }
    bool focusable(std::size_t box) const {
        return eligible(box) &&
               std::get<bool>(*effective_property(*input_.tree->get(input_.layout->boxes[box].node), property_names::focusable));
    }
    // Index into LayoutResult::boxes, or no_layout_parent for a display-none node.
    std::uint32_t box(NodeHandle node) const { return by_node_[node.index]; }

    // First eligible node with the binding on the target's ancestry; hidden ancestors are skipped.
    std::optional<NodeHandle> binding_owner(std::optional<NodeHandle> target, std::string_view binding = "activate") const {
        if (!target) return {};
        auto index = by_node_[target->index];
        while (index != no_layout_parent) {
            const auto& box = input_.layout->boxes[index];
            if (eligible(index) && input_.tree->get(box.node)->events.contains(binding)) return box.node;
            index = box.parent;
        }
        return {};
    }

    void refresh(PointerState& pointer) const {
        pointer.hovered = hit(pointer.position);
        if (pointer.pressed && !eligible(*pointer.pressed)) pointer.pressed.reset();
        pointer.active = pointer.pressed && pointer.pressed == binding_owner(pointer.hovered) ? pointer.pressed :
                                                                                              std::nullopt;
    }

private:
    const HitTestInput& input_;
    std::vector<std::uint32_t> by_node_;
    std::vector<bool> disabled_;
};

} // namespace tessera::detail
