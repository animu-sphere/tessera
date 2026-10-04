#include <tessera/layout/layout_box.hpp>
#include "../detail/checks.hpp"
#include <string>

namespace tessera {

std::vector<Diagnostic> validate(const LayoutInput& input) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (!input.tree) check.error("missing_input", "/tree", "Provide the UiTree to lay out.");
    if (!input.text) check.error("missing_input", "/text", "Provide a TextShaper, e.g. PlaceholderTextShaper.");
    check.size(input.viewport, "/viewport");
    if (input.tree && input.styles.size() != input.tree->size()) {
        check.error("style_count", "/styles", "Provide one resolved style per tree node (" +
                    std::to_string(input.tree->size()) + "), indexed by NodeHandle::index.");
    }
    for (std::size_t i = 0; i < input.styles.size(); ++i) {
        auto style = validate(input.styles[i], "/styles/" + std::to_string(i));
        errors.insert(errors.end(), style.begin(), style.end());
    }
    return errors;
}

} // namespace tessera
