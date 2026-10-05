#pragma once

#include <tessera/layout/layout_box.hpp>

namespace tessera::detail {

// Clip recorded for the children of `parent`: its own clip, narrowed by its padding box when it clips or
// scrolls.
inline std::optional<Rect> descendant_clip(const LayoutBox& parent, Overflow overflow) {
    if (overflow == Overflow::visible) return parent.clip;
    const auto own = parent.padding_box();
    return parent.clip ? intersect(*parent.clip, own) : own;
}

// Shared layout-consumer validation. No renderer or input implementation dependency.
std::vector<Diagnostic> validate_layout_snapshot(const UiTree*, std::span<const ResolvedStyle>, const LayoutResult*);

} // namespace tessera::detail
