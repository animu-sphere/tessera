#pragma once

#include <tessera/layout/layout_box.hpp>
#include <tessera/style/resolved_style.hpp>
#include <tessera/ui/tree.hpp>
#include "modal.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace tessera::detail {

inline constexpr std::uint32_t no_order_parent = std::numeric_limits<std::uint32_t>::max();

// Stacking order over `count` items whose parents precede them (`parent(i)` is no_order_parent for roots): preorder
// in which each item's children are stably ordered by `key(child)`, each child followed by its whole subtree.
template<class Parent, class Key> std::vector<std::uint32_t> stacking_order(std::size_t count, Parent parent, Key key) {
    std::vector<std::vector<std::uint32_t>> children(count);
    std::vector<std::uint32_t> pending;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = parent(i);
        if (at == no_order_parent)
            pending.push_back(i);
        else
            children[at].push_back(i);
    }
    const auto before = [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); };
    for (auto& list : children) std::stable_sort(list.begin(), list.end(), before);
    std::stable_sort(pending.begin(), pending.end(), before);
    std::reverse(pending.begin(), pending.end());
    std::vector<std::uint32_t> order;
    order.reserve(count);
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        order.push_back(item);
        pending.insert(pending.end(), children[item].rbegin(), children[item].rend());
    }
    return order;
}

// Sort key of one child: root layers follow every ordinary root child in declared order, ignoring z_index; other
// children order by ascending z_index.
inline std::pair<bool, std::int32_t> stacking_key(bool root_child, bool layer, std::int32_t z_index) {
    const bool raised = root_child && layer;
    return {raised, raised ? 0 : z_index};
}

// Box indices in paint order over a validated snapshot: tree preorder in which each box's displayed children are
// stably ordered by ascending z_index, each child followed by its whole subtree. Root layers keep following the
// ordinary root children. Hit testing visits the reverse. Equals box order when every z_index is zero.
inline std::vector<std::uint32_t> paint_order(const UiTree& tree, std::span<const ResolvedStyle> styles,
                                              const LayoutResult& layout) {
    const auto& boxes = layout.boxes;
    if (std::all_of(boxes.begin(), boxes.end(),
                    [&](const LayoutBox& box) { return styles[box.node.index].z_index == 0; })) {
        std::vector<std::uint32_t> order(boxes.size());
        std::iota(order.begin(), order.end(), std::uint32_t{0});
        return order;
    }
    return stacking_order(
        boxes.size(), [&](std::uint32_t i) { return boxes[i].parent; },
        [&](std::uint32_t i) {
            const auto parent = boxes[i].parent;
            return stacking_key(parent != no_layout_parent && boxes[parent].parent == no_layout_parent,
                                root_layer(*tree.get(boxes[i].node)), styles[boxes[i].node.index].z_index);
        });
}

} // namespace tessera::detail
