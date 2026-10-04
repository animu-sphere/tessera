#include <tessera/layout/layout_box.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using tessera::test::check;
using tessera::test::has;

namespace {

void resolved_style() {
    check(tessera::validate(tessera::ResolvedStyle{}).empty(), "Primitive defaults rejected");
    tessera::ResolvedStyle style;
    style.width = tessera::Dimension::points(std::nanf(""));
    style.min_width = 10;
    style.max_width = 5;
    style.max_height = std::nanf("");
    style.padding.left = -1;
    style.opacity = 2;
    style.direction = static_cast<tessera::FlexDirection>(7);
    style.background.a = std::numeric_limits<float>::infinity();
    style.text.size = 0;
    const auto errors = tessera::validate(style, "/styles/4");
    check(has(errors, "invalid_number", "/styles/4/width/value"), "Non-finite width accepted");
    check(has(errors, "conflicting_constraints", "/styles/4/min_width"), "min > max accepted");
    check(has(errors, "invalid_number", "/styles/4/max_height"), "NaN maximum accepted");
    check(has(errors, "out_of_range", "/styles/4/padding/left"), "Negative padding accepted");
    check(has(errors, "out_of_range", "/styles/4/opacity"), "Opacity range unchecked");
    check(has(errors, "unknown_value", "/styles/4/direction"), "Unknown enum accepted");
    check(has(errors, "invalid_number", "/styles/4/background/a"), "Non-finite color accepted");
    check(has(errors, "out_of_range", "/styles/4/text/size"), "Zero font size accepted");
    check(errors.size() == 8, "Each invalid value should report exactly once");
}

void layout_input() {
    tessera::UiDocument document;
    document.root.children.resize(2);
    auto tree = tessera::UiTree::create(document);
    check(static_cast<bool>(tree), "Tree creation failed");
    tessera::PlaceholderTextShaper text;
    std::vector<tessera::ResolvedStyle> styles(3);
    tessera::LayoutInput input{tree.value->get(), styles, {640, 480}, &text};
    check(tessera::validate(input).empty(), "Valid layout input rejected");

    styles[1].gap = -4;
    input.styles = std::span(styles).first(2);
    input.viewport.width = std::numeric_limits<float>::infinity();
    input.text = nullptr;
    const auto errors = tessera::validate(input);
    check(has(errors, "style_count", "/styles"), "Style count mismatch accepted");
    check(has(errors, "out_of_range", "/styles/1/gap"), "Style errors not located by node index");
    check(has(errors, "invalid_number", "/viewport/width"), "Unbounded viewport accepted");
    check(has(errors, "missing_input", "/text"), "Missing text shaper accepted");
    check(has(tessera::validate(tessera::LayoutInput{}), "missing_input", "/tree"), "Missing tree accepted");
}

void box_geometry() {
    tessera::LayoutBox box;
    box.border_box = {{10, 20}, {100, 50}};
    box.border = {1, 1, 1, 1};
    box.padding = {2, 3, 4, 5};
    check(box.padding_box() == tessera::Rect{{11, 21}, {98, 48}}, "Padding box differs");
    check(box.content_box() == tessera::Rect{{16, 23}, {90, 42}}, "Content box differs");
    box.padding.left = 200;
    check(box.content_box() == tessera::Rect{{211, 23}, {0, 42}}, "Overconstrained content box must clamp to zero");
}

} // namespace

int main() {
    try {
        resolved_style();
        layout_input();
        box_geometry();
        std::cout << "Resolved-style, layout input, and LayoutBox geometry checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
