#include <tessera/style/resolved_style.hpp>
#include "../detail/checks.hpp"
#include <cmath>
#include <string>

namespace tessera {

std::vector<Diagnostic> validate(const ResolvedStyle& style, std::string_view base) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    const std::string path(base);
    check.enumeration(style.display, Display::none, path + "/display");
    check.enumeration(style.direction, FlexDirection::row, path + "/direction");
    check.enumeration(style.justify, Justify::space_between, path + "/justify");
    check.enumeration(style.align, Align::stretch, path + "/align");
    check.enumeration(style.overflow, Overflow::scroll, path + "/overflow");
    check.enumeration(style.visibility, Visibility::hidden, path + "/visibility");

    auto dimension = [&](const Dimension& value, const std::string& location) {
        check.enumeration(value.kind, Dimension::Kind::points, location + "/kind");
        if (value.kind == Dimension::Kind::points) check.non_negative(value.value, location + "/value");
    };
    // Maximums may be +infinity (unconstrained); a fixed size is later clamped into [min, max].
    auto constraint = [&](float minimum, float maximum, const std::string& axis) {
        check.non_negative(minimum, path + "/min_" + axis);
        if (std::isnan(maximum)) check.error("invalid_number", path + "/max_" + axis, "Use a number or +infinity.");
        else if (maximum < 0) check.error("out_of_range", path + "/max_" + axis, "Expected a value >= 0.");
        else if (std::isfinite(minimum) && minimum > maximum)
            check.error("conflicting_constraints", path + "/min_" + axis,
                        "min_" + axis + " exceeds max_" + axis + "; lower the minimum or raise the maximum.");
    };
    dimension(style.width, path + "/width");
    dimension(style.height, path + "/height");
    constraint(style.min_width, style.max_width, "width");
    constraint(style.min_height, style.max_height, "height");
    check.edges(style.margin, path + "/margin");
    check.edges(style.border, path + "/border");
    check.edges(style.padding, path + "/padding");
    check.non_negative(style.gap, path + "/gap");
    check.non_negative(style.grow, path + "/grow");
    check.non_negative(style.shrink, path + "/shrink");

    check.range(style.opacity, path + "/opacity", 0, 1, "an opacity in [0, 1]");
    check.color(style.background, path + "/background");
    check.color(style.border_color, path + "/border_color");
    check.non_negative(style.corner_radius, path + "/corner_radius");

    auto text = validate(style.text, path + "/text");
    errors.insert(errors.end(), text.begin(), text.end());
    check.color(style.color, path + "/color");
    return errors;
}

} // namespace tessera
