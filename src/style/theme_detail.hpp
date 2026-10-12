#pragma once

#include <tessera/style/style_sheet.hpp>

namespace tessera::detail {
// Syntax checks do not require a variable table. Materialization checks the
// complete candidate, including unused variables and unmatched rules.
std::vector<Diagnostic> validate_theme_bindings(const StyleSheet&);
struct PreparedStyles {
    std::vector<StyleDeclarations> theme;
    std::vector<StyleDeclarations> sheet;
};
Result<PreparedStyles> prepare_theme(const Theme*, const StyleSheet&);
} // namespace tessera::detail
