#pragma once

#include <tessera/layout/layout_box.hpp>

namespace tessera::detail {

// Shared layout-consumer validation. No renderer or input implementation dependency.
std::vector<Diagnostic> validate_layout_snapshot(const UiTree*, std::span<const ResolvedStyle>, const LayoutResult*);

} // namespace tessera::detail
