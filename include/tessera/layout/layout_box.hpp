#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/resolved_style.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/tree.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace tessera {

inline constexpr std::uint32_t no_layout_parent = std::numeric_limits<std::uint32_t>::max();

// Geometry of one displayed node, in root logical coordinates.
struct LayoutBox {
    NodeHandle node;
    std::uint32_t parent = no_layout_parent; // Index into LayoutResult::boxes.
    Rect border_box;
    Edges border;
    Edges padding;
    bool visible = true; // Hidden boxes keep geometry but neither paint nor receive input.

    Rect padding_box() const noexcept { return inset(border_box, border); }
    Rect content_box() const noexcept { return inset(padding_box(), padding); }
    bool operator==(const LayoutBox&) const = default;
};

// Boxes in tree preorder. Display::none subtrees are absent. Owned; handles refer to the input tree.
struct LayoutResult {
    std::vector<LayoutBox> boxes;
    bool operator==(const LayoutResult&) const = default;
};

// Everything is borrowed for one layout pass. styles[i] belongs to the node whose handle index is i.
struct LayoutInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    Size viewport; // Available size for the root's margin box.
    TextShaper* text = nullptr;
};

std::vector<Diagnostic> validate(const LayoutInput&);

// Full-tree fixed/stack/flex layout; see docs/design/layout.md for the rules. Fails with validate()
// diagnostics, text measurement errors under /nodes/<index>, or non_finite_geometry.
Result<LayoutResult> compute_layout(const LayoutInput&);

} // namespace tessera
