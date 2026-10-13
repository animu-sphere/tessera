#pragma once

#include <tessera/ui/component.hpp>

namespace tessera::detail {
// Shared topology/geometry preflight; never reads reactive values or copies nodes.
Result<std::vector<std::size_t>> component_tree_order(std::span<const ComponentInvocation>, std::size_t root);
}
