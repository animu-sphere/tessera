#pragma once

#include <tessera/ui/component.hpp>

namespace tessera::detail {
// Shared pre-read validation for lowering and owner/state registration.
std::vector<Diagnostic> validate_component_local_state(std::span<const ComponentLocalState>,
                                                     const std::string& path);
} // namespace tessera::detail
