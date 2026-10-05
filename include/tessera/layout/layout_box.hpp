#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/resolved_style.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/tree.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace tessera {

inline constexpr std::uint32_t no_layout_parent = std::numeric_limits<std::uint32_t>::max();

// Scroll state of an Overflow::scroll box. The viewport is the box's padding box.
struct ScrollGeometry {
    Size extent;  // Scrollable size measured from the padding box origin; never smaller than the viewport.
    Point offset; // Applied offset, clamped into [0, extent - viewport] on each axis.
    bool operator==(const ScrollGeometry&) const = default;
};

// Geometry of one displayed node, in root logical coordinates.
struct LayoutBox {
    NodeHandle node;
    std::uint32_t parent = no_layout_parent; // Index into LayoutResult::boxes.
    Rect border_box;
    Edges border;
    Edges padding;
    bool visible = true; // Hidden boxes keep geometry but neither paint nor receive input.
    // Intersection of every clipping ancestor's padding box; absent when no ancestor clips.
    // Paint and hit testing restrict this box to it; the box's own overflow affects only descendants.
    std::optional<Rect> clip;
    // Present exactly for Overflow::scroll boxes. Descendant geometry already includes the offset.
    std::optional<ScrollGeometry> scroll;

    Rect padding_box() const noexcept { return inset(border_box, border); }
    Rect content_box() const noexcept { return inset(padding_box(), padding); }
    // Largest offset on each axis; zero without scroll geometry.
    Point scroll_limit() const noexcept {
        if (!scroll) return {};
        const auto viewport = padding_box().size;
        return {scroll->extent.width - viewport.width, scroll->extent.height - viewport.height};
    }
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
    // Empty, or one requested scroll offset per node by NodeHandle::index. Layout clamps each scroll box's
    // offset into its limits and ignores entries for other nodes.
    std::span<const Point> scroll_offsets;
};

std::vector<Diagnostic> validate(const LayoutInput&);

// Full-tree fixed/stack/flex layout; see docs/design/layout.md for the rules. Fails with validate()
// diagnostics, text measurement errors under /nodes/<index>, or non_finite_geometry.
Result<LayoutResult> compute_layout(const LayoutInput&);

} // namespace tessera
