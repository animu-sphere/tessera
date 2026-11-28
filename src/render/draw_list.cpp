#include <tessera/render/draw_list.hpp>
#include <tessera/render/renderer.hpp>
#include "../detail/checks.hpp"
#include "../detail/shadow.hpp"
#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

namespace tessera {

bool time_dependent(const UiDrawList& list) {
    return std::any_of(list.commands.begin(), list.commands.end(), [](const DrawCommand& command) {
        const auto* rect = std::get_if<DrawRect>(&command);
        return rect && ((rect->procedural && rect->procedural->time_rate != 0) ||
                        (rect->custom_shape && rect->custom_shape->time_rate != 0));
    });
}

std::vector<Diagnostic> validate(const UiDrawList& list) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    enum class Kind : std::uint8_t { clip, transform, layer };
    struct Entry {
        Kind kind;
        bool axis_aligned; // No rotation/skew in the composed transform.
    };
    std::vector<Entry> stack;
    std::size_t layers = 0;
    auto axis_aligned = [&] { return stack.empty() || stack.back().axis_aligned; };
    auto push = [&](Entry entry, const std::string& path) {
        stack.push_back(entry);
        if (stack.size() == max_draw_stack_depth + 1)
            check.error("stack_limit", path, "Clip/transform/layer nesting exceeds the limit (64).");
    };
    auto pop = [&](Kind kind, const std::string& path) {
        if (stack.empty() || stack.back().kind != kind) {
            check.error("unbalanced_stack", path,
                        kind == Kind::clip        ? "PopClip must close the innermost PushClip."
                        : kind == Kind::transform ? "PopTransform must close the innermost PushTransform."
                                                  : "PopLayer must close the innermost PushLayer.");
        } else {
            if (kind == Kind::layer) --layers;
            stack.pop_back();
        }
    };
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto path = "/commands/" + std::to_string(i);
        std::visit(
            [&](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, DrawRect> || std::is_same_v<T, DrawBorder> ||
                              std::is_same_v<T, DrawShadow> || std::is_same_v<T, DrawStroke>) {
                    check.rect(command.rect, path + "/rect");
                    check.color(command.color, path + "/color");
                    check.non_negative(command.corner_radius, path + "/corner_radius");
                    if constexpr (std::is_same_v<T, DrawRect>) {
                        check.enumeration(command.shape, FillShape::ellipse, path + "/shape");
                        if (command.gradient) check.gradient(*command.gradient, path + "/gradient");
                        if (command.procedural) check.procedural(*command.procedural, path + "/procedural");
                        if (command.custom_shape) {
                            check.custom_shape(*command.custom_shape, path + "/custom_shape");
                            if (command.procedural)
                                check.error("unsupported_combination", path + "/custom_shape",
                                            "Profile sdf-v1 paints custom shapes with color or gradient; remove the "
                                            "procedural fill or the custom shape.");
                        }
                    }
                    if constexpr (std::is_same_v<T, DrawBorder>) check.edges(command.widths, path + "/widths");
                    if constexpr (std::is_same_v<T, DrawStroke>)
                        check.stroke(command.width, command.dash_length, command.gap_length, command.dash_offset, path);
                    if constexpr (std::is_same_v<T, DrawShadow>) {
                        check.non_negative(command.softness, path + "/softness");
                        check.point(command.offset, path + "/offset");
                        if (!command.inset && command.offset != Point{})
                            check.error("out_of_range", path + "/offset",
                                        "Outer shadows use an already offset caster rectangle; offset must be zero.");
                        if (std::isfinite(command.softness) && command.softness >= 0 &&
                            std::isfinite(command.rect.origin.x) && std::isfinite(command.rect.origin.y) &&
                            std::isfinite(command.rect.size.width) && std::isfinite(command.rect.size.height) &&
                            !detail::shadow_bounds(command.rect, command.softness, command.inset))
                            check.error("geometry_overflow", path + "/rect",
                                        "Expanded shadow bounds must fit finite float coordinates.");
                    }
                } else if constexpr (std::is_same_v<T, DrawImage>) {
                    check.rect(command.rect, path + "/rect");
                    if (command.image.value == 0)
                        check.error("invalid_handle", path + "/image",
                                    "Use an image handle issued by the host resource system.");
                    check.rect(command.source, path + "/source");
                    check.color(command.tint, path + "/tint");
                } else if constexpr (std::is_same_v<T, DrawGlyphRun>) {
                    check.point(command.origin, path + "/origin");
                    check.positive(command.run.size, path + "/run/size");
                    for (std::size_t g = 0; g < command.run.glyphs.size(); ++g)
                        check.point(command.run.glyphs[g].position,
                                    path + "/run/glyphs/" + std::to_string(g) + "/position");
                    check.color(command.color, path + "/color");
                } else if constexpr (std::is_same_v<T, PushClip>) {
                    check.rect(command.rect, path + "/rect");
                    if (!axis_aligned())
                        check.error("unsupported_clip", path,
                                    "Clipping under a rotated or skewed transform is not supported.");
                    push({Kind::clip, axis_aligned()}, path);
                } else if constexpr (std::is_same_v<T, PushTransform>) {
                    const auto& t = command.transform;
                    const std::pair<float, const char*> values[] = {{t.a, "a"}, {t.b, "b"},   {t.c, "c"},
                                                                    {t.d, "d"}, {t.tx, "tx"}, {t.ty, "ty"}};
                    bool finite = true;
                    for (const auto& [value, name] : values)
                        finite = check.finite(value, path + "/transform/" + name) && finite;
                    if (finite && t.a * t.d - t.b * t.c == 0)
                        check.error("singular_transform", path + "/transform", "Use an invertible transform.");
                    push({Kind::transform, axis_aligned() && t.b == 0 && t.c == 0}, path);
                } else if constexpr (std::is_same_v<T, PushLayer>) {
                    check.range(command.opacity, path + "/opacity", 0, 1, "an opacity in [0, 1]");
                    if (++layers == max_draw_layer_depth + 1)
                        check.error("layer_limit", path, "Layer nesting exceeds the limit (8).");
                    push({Kind::layer, axis_aligned()}, path);
                } else if constexpr (std::is_same_v<T, PopClip>) {
                    pop(Kind::clip, path);
                } else if constexpr (std::is_same_v<T, PopLayer>) {
                    pop(Kind::layer, path);
                } else {
                    pop(Kind::transform, path);
                }
            },
            list.commands[i]);
    }
    if (!stack.empty())
        check.error("unbalanced_stack", "/commands",
                    std::to_string(stack.size()) + " pushed clip/transform/layer entries are never popped.");
    return errors;
}

std::vector<Diagnostic> validate(const FrameInfo& frame) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (frame.frame == 0) check.error("out_of_range", "/frame", "Frame numbers start at 1 and strictly increase.");
    check.positive(frame.logical_size.width, "/logical_size/width");
    check.positive(frame.logical_size.height, "/logical_size/height");
    check.positive(frame.device_scale, "/device_scale");
    return errors;
}

} // namespace tessera
