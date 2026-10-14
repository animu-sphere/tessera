#pragma once

#include <tessera/ui/component_mount.hpp>

namespace tessera::detail {
// Shared pre-read validation for lowering and owner/state registration.
std::vector<Diagnostic> validate_component_local_state(std::span<const ComponentLocalState>,
                                                     const std::string& path);
std::vector<Diagnostic> validate_component_cleanups(std::span<const std::string>, const std::string& path);
std::vector<Diagnostic> validate_component_cleanup_bindings(std::span<const std::string>,
    const ComponentCleanupBindings&, const std::string& path);
} // namespace tessera::detail
