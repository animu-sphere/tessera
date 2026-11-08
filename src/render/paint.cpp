#include <tessera/render/paint.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/stroke.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace tessera {
namespace {

bool has_error(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::error; });
}

Color faded(Color color, float opacity) {
    color.a *= opacity;
    return color;
}

bool has_border(const Edges& edges) { return edges.top > 0 || edges.right > 0 || edges.bottom > 0 || edges.left > 0; }

} // namespace

std::vector<Diagnostic> validate(const PaintInput& input) {
    auto errors = detail::validate_layout_snapshot(input.tree, input.styles, input.layout);
    detail::Checker check(errors);
    if (!input.text) check.error("missing_input", "/text", "Provide the TextShaper used for layout.");
    return errors;
}

Result<UiDrawList> build_paint_list(const PaintInput& input) {
    auto diagnostics = validate(input);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
    UiDrawList list;
    std::optional<Rect> clip; // Currently pushed in `list`.
    std::vector<float> opacities;
    opacities.reserve(input.layout->boxes.size());
    bool failed = false;
    for (const auto& box : input.layout->boxes) {
        const auto& style = input.styles[box.node.index];
        const float opacity = style.opacity * (box.parent == no_layout_parent ? 1 : opacities[box.parent]);
        opacities.push_back(opacity);
        if (!box.visible || opacity == 0) continue;

        UiDrawList commands;
        if (style.box_shadow && !style.box_shadow->inset && style.box_shadow->color.a > 0) {
            const auto& shadow = *style.box_shadow;
            const double x = double(box.border_box.origin.x) + shadow.offset.x,
                         y = double(box.border_box.origin.y) + shadow.offset.y;
            if (std::abs(x) > std::numeric_limits<float>::max() || std::abs(y) > std::numeric_limits<float>::max()) {
                diagnostics.push_back({"geometry_overflow",
                                       Severity::error,
                                       "/nodes/" + std::to_string(box.node.index) + "/box_shadow/offset",
                                       "Offset shadow origin must fit finite float coordinates.",
                                       {}});
            } else {
                commands.commands.push_back(DrawShadow{{{float(x), float(y)}, box.border_box.size},
                                                       faded(shadow.color, opacity),
                                                       style.corner_radius,
                                                       shadow.softness});
            }
        }
        const auto background = faded(style.background, opacity);
        if (style.background_gradient) {
            auto gradient = *style.background_gradient;
            bool visible = false;
            std::visit(
                [&](auto& fill) {
                    fill.start_color = faded(fill.start_color, opacity);
                    fill.end_color = faded(fill.end_color, opacity);
                    for (auto& stop : fill.stops) stop.color = faded(stop.color, opacity);
                    for (std::size_t i = 0; i < fill.stop_count(); ++i) visible |= fill.stop(i).color.a > 0;
                },
                gradient);
            if (visible)
                commands.commands.push_back(DrawRect{box.border_box,
                                                     {},
                                                     style.corner_radius,
                                                     gradient,
                                                     style.background_shape,
                                                     style.background_antialias});
        } else if (background.a > 0) {
            commands.commands.push_back(DrawRect{box.border_box,
                                                 background,
                                                 style.corner_radius,
                                                 {},
                                                 style.background_shape,
                                                 style.background_antialias});
        }
        if (style.box_shadow && style.box_shadow->inset && style.box_shadow->color.a > 0) {
            const auto& shadow = *style.box_shadow;
            commands.commands.push_back(DrawShadow{box.border_box, faded(shadow.color, opacity), style.corner_radius,
                                                   shadow.softness, true, shadow.offset});
        }
        const auto border = faded(style.border_color, opacity);
        if (border.a > 0 && has_border(box.border))
            commands.commands.push_back(DrawBorder{box.border_box, box.border, border, style.corner_radius});
        if (style.outline && style.outline->width > 0 && style.outline->color.a > 0 && box.border_box.size.width > 0 &&
            box.border_box.size.height > 0) {
            const auto& outline = *style.outline;
            const double expansion = double(outline.offset) + outline.width;
            const auto rect = detail::expanded_stroke_bounds(box.border_box, expansion, expansion);
            const double radius = style.corner_radius == 0
                                      ? 0
                                      : std::min(double(style.corner_radius),
                                                 std::min(box.border_box.size.width, box.border_box.size.height) * .5) +
                                            expansion;
            if (!rect || radius > std::numeric_limits<float>::max())
                diagnostics.push_back({"geometry_overflow",
                                       Severity::error,
                                       "/nodes/" + std::to_string(box.node.index) + "/outline",
                                       "Expanded outline geometry must fit finite float coordinates.",
                                       {}});
            else
                commands.commands.push_back(DrawStroke{*rect, faded(outline.color, opacity), outline.width,
                                                       float(radius), outline.dash_length, outline.gap_length,
                                                       outline.dash_offset});
        }
        const auto& node = *input.tree->get(box.node);
        const auto color = faded(style.color, opacity);
        const auto path = "/nodes/" + std::to_string(box.node.index);
        if (node.kind == NodeKind::text && color.a > 0) {
            const auto property = node.properties.find(property_names::text);
            const auto* text =
                property == node.properties.end() ? nullptr : std::get_if<std::string>(&property->second);
            auto shaped = input.text->shape(text ? std::string_view(*text) : std::string_view(), style.text,
                                            {box.content_box().size.width});
            if (!shaped) {
                failed = true;
                if (!has_error(shaped.diagnostics))
                    shaped.diagnostics.push_back({"text_shape_failed",
                                                  Severity::error,
                                                  "/text",
                                                  "TextShaper returned no glyph run; provide a failure diagnostic.",
                                                  {}});
            }
            for (auto& diagnostic : shaped.diagnostics) {
                diagnostic.path = path + diagnostic.path;
                diagnostics.push_back(std::move(diagnostic));
            }
            if (shaped && !shaped.value->glyphs.empty())
                commands.commands.push_back(DrawGlyphRun{box.content_box().origin, std::move(*shaped.value), color});
        }
        // Reject malformed runs from an injected shaper before exposing any commands.
        for (auto& diagnostic : validate(commands)) {
            diagnostic.path = path + diagnostic.path;
            diagnostics.push_back(std::move(diagnostic));
        }
        if (commands.commands.empty()) continue;
        // Consecutive boxes under the same recorded clip share one push/pop pair.
        if (box.clip != clip) {
            if (clip) list.commands.push_back(PopClip{});
            if (box.clip) list.commands.push_back(PushClip{*box.clip});
            clip = box.clip;
        }
        for (auto& command : commands.commands) list.commands.push_back(std::move(command));
    }
    if (clip) list.commands.push_back(PopClip{});
    if (failed || has_error(diagnostics)) return {std::nullopt, std::move(diagnostics)};
    return {std::move(list), std::move(diagnostics)};
}

} // namespace tessera
