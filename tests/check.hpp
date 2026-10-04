#pragma once

#include <tessera/ui/document.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tessera::test {

inline void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

inline bool has(const std::vector<Diagnostic>& diagnostics, std::string_view code, std::string_view path) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code && diagnostic.path == path && !diagnostic.message.empty() &&
            diagnostic.severity == Severity::error) return true;
    }
    return false;
}

} // namespace tessera::test
