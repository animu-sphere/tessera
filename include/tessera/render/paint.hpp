#pragma once

#include <tessera/layout/layout_box.hpp>
#include <tessera/render/draw_list.hpp>
#include <span>

namespace tessera {

// Borrowed for one paint pass. Recompute layout after geometry/text style changes.
// styles[i] belongs to NodeHandle::index i; layout must come from the same tree/styles.
struct PaintInput {
    const UiTree* tree = nullptr;
    std::span<const ResolvedStyle> styles;
    const LayoutResult* layout = nullptr;
    TextShaper* text = nullptr;
};

std::vector<Diagnostic> validate(const PaintInput&);

// Owned background/border/glyph commands in tree preorder, in root logical coordinates.
// Ancestor opacity multiplies command alpha; this is not offscreen group composition.
// Hidden boxes suppress only their own commands. Recorded layout clips become PushClip/PopClip
// around each box's commands; other overflow is not clipped.
// Invalid input or shaping failure returns diagnostics and no partial draw list.
Result<UiDrawList> build_paint_list(const PaintInput&);

} // namespace tessera
