#pragma once

#include <tessera/layout/layout_box.hpp>
#include <tessera/render/draw_list.hpp>
#include <span>

namespace tessera {

// Host-presented editing state painted with one Text node: `selection` rectangles under its glyphs
// and an optional `caret` above them, both relative to the node's content-box origin, for example
// from TextCaretLayout::range and TextCaretLayout::caret. The commands follow the node's opacity,
// visibility and recorded clip, and paint whether or not the text itself is visible.
struct TextEditingPaint {
    NodeHandle node;
    std::vector<Rect> selection;
    Color selection_color{.2f, .4f, .85f, .3f};
    std::optional<Rect> caret;
    Color caret_color{0, 0, 0, 1};
    bool operator==(const TextEditingPaint&) const = default;
};

// Borrowed for one paint pass. Recompute layout after geometry/text style changes.
// styles[i] belongs to NodeHandle::index i; layout must come from the same tree/styles.
struct PaintInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    const LayoutResult* layout = nullptr;
    TextShaper* text = nullptr;
    // Finite host time in seconds for procedural fills with a nonzero `time_rate`. The host owns
    // its origin, pausing and scheduling; changing it needs no restyle or layout, only paint.
    double host_time = 0;
    // At most one entry per Text node of this tree.
    std::span<const TextEditingPaint> text_editing;
};

std::vector<Diagnostic> validate(const PaintInput&);

// Owned background/border/glyph commands in tree preorder with siblings ordered by z_index, in root logical
// coordinates.
// Ancestor opacity multiplies command alpha; this is not offscreen group composition.
// Hidden boxes suppress only their own commands. Recorded layout clips become PushClip/PopClip
// around each box's commands; other overflow is not clipped.
// Invalid input or shaping failure returns diagnostics and no partial draw list.
Result<UiDrawList> build_paint_list(const PaintInput&);

} // namespace tessera
