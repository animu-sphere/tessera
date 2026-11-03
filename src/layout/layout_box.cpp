#include <tessera/layout/layout_box.hpp>
#include "../detail/checks.hpp"
#include "../detail/popup.hpp"
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
        const auto& resolved = input.styles[i];
        if (resolved.display != Display::grid) continue;
        const auto path = "/styles/" + std::to_string(i);
        if (resolved.grid_columns.empty())
            check.error("grid_tracks", path + "/grid_columns", "Declare at least one explicit Grid column.");
        if (resolved.grid_rows.empty())
            check.error("grid_tracks", path + "/grid_rows", "Declare at least one explicit Grid row.");
        if (input.tree && input.styles.size() == input.tree->size()) {
            std::size_t count = 0;
            for (const auto child : input.tree->children({input.tree->root().tree, static_cast<std::uint32_t>(i)}))
                if (input.styles[child.index].display != Display::none && !detail::popup_anchor(*input.tree->get(child))) ++count;
            if (count > resolved.grid_columns.size() * resolved.grid_rows.size())
                check.error("grid_capacity", path,
                    "Declare enough explicit Grid cells for every displayed child; implicit tracks are unsupported.");
        }
    }
    if (input.tree && !input.scroll_offsets.empty() && input.scroll_offsets.size() != input.tree->size()) {
        check.error("scroll_count", "/scroll_offsets", "Provide no scroll offsets, or one per tree node (" +
                    std::to_string(input.tree->size()) + "), indexed by NodeHandle::index.");
    }
    for (std::size_t i = 0; i < input.scroll_offsets.size(); ++i)
        check.point(input.scroll_offsets[i], "/scroll_offsets/" + std::to_string(i));
    return errors;
}

} // namespace tessera
