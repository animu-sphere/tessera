#include <tessera/render/paint.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/checks.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/paint_order.hpp"
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
    if (!std::isfinite(input.host_time))
        check.error("invalid_number", "/host_time", "Use a finite host time in seconds.");
    if (!input.tree) return errors;
    std::vector<bool> edited(input.tree->size());
    for (std::size_t i = 0; i < input.text_editing.size(); ++i) {
        const auto& editing = input.text_editing[i];
        const auto path = "/text_editing/" + std::to_string(i);
        const auto* node = input.tree->get(editing.node);
        if (!node || node->kind != NodeKind::text)
            check.error("text_editing_node", path + "/node", "Present editing on a Text node of this tree.");
        else if (edited[editing.node.index])
            check.error("text_editing_node", path + "/node", "Present at most one editing state per Text node.");
        else
            edited[editing.node.index] = true;
        for (std::size_t r = 0; r < editing.selection.size(); ++r)
            check.rect(editing.selection[r], path + "/selection/" + std::to_string(r));
        check.color(editing.selection_color, path + "/selection_color");
        if (editing.caret) check.rect(*editing.caret, path + "/caret");
        check.color(editing.caret_color, path + "/caret_color");
    }
    return errors;
}

Result<UiDrawList> build_paint_list(const PaintInput& input) {
    auto diagnostics = validate(input);
    if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
    const auto& boxes = input.layout->boxes;
    // Opacity composes down tree preorder; emission follows the z_index paint order. A layer node's commands and
    // descendants paint inside its layer relative to it, and the layer composes with the opacity accumulated so far.
    std::vector<float> opacities, composed;
    opacities.reserve(boxes.size());
    composed.reserve(boxes.size());
    std::vector<std::size_t> subtree(boxes.size(), 1);
    for (const auto& box : boxes) {
        const auto& style = input.styles[box.node.index];
        const float inherited = box.parent == no_layout_parent ? 1 : opacities[box.parent];
        composed.push_back(inherited * style.opacity);
        opacities.push_back(style.composition == Composition::layer ? 1 : composed.back());
    }
    for (std::size_t i = boxes.size(); i-- > 0;)
        if (boxes[i].parent != no_layout_parent) subtree[boxes[i].parent] += subtree[i];
    // One open surface: the root list or a layer collecting its node's subtree. `base` is the clip its parent
    // pushed around it; `clip` is the clip in effect for the commands appended last.
    struct Frame {
        std::vector<DrawCommand> commands;
        std::optional<Rect> base, clip;
        std::size_t end = 0; // Paint-order position after the layer node's subtree.
        std::uint32_t box = 0;
    };
    std::vector<Frame> frames(1);
    frames[0].end = boxes.size();
    // Moves the frame's clip to `wanted`; consecutive boxes under the same recorded clip share one push/pop pair.
    const auto switch_clip = [](Frame& frame, const std::optional<Rect>& wanted) {
        if (frame.clip == wanted) return;
        if (frame.clip != frame.base) frame.commands.push_back(PopClip{});
        if (wanted && wanted != frame.base) frame.commands.push_back(PushClip{*wanted});
        frame.clip = wanted;
    };
    // Closes the innermost layer: a layer that painted nothing emits nothing.
    const auto close_layer = [&] {
        auto layer = std::move(frames.back());
        frames.pop_back();
        switch_clip(layer, layer.base);
        if (layer.commands.empty()) return;
        auto& parent = frames.back();
        switch_clip(parent, boxes[layer.box].clip);
        parent.commands.push_back(PushLayer{composed[layer.box]});
        for (auto& command : layer.commands) parent.commands.push_back(std::move(command));
        parent.commands.push_back(PopLayer{});
    };
    bool failed = false;
    std::vector<const TextEditingPaint*> editing(input.tree->size());
    for (const auto& entry : input.text_editing) editing[entry.node.index] = &entry;
    // Translates content-relative editing geometry; reports rectangles that leave float range.
    const auto shifted = [&](Rect rect, Point origin, const std::string& path) -> std::optional<Rect> {
        const double x = double(origin.x) + rect.origin.x, y = double(origin.y) + rect.origin.y;
        if (std::abs(x) + rect.size.width > std::numeric_limits<float>::max() ||
            std::abs(y) + rect.size.height > std::numeric_limits<float>::max()) {
            diagnostics.push_back({"geometry_overflow",
                                   Severity::error,
                                   path,
                                   "Editing geometry must fit finite float coordinates.",
                                   {}});
            return std::nullopt;
        }
        return Rect{{float(x), float(y)}, rect.size};
    };
    const auto order = detail::paint_order(*input.tree, input.styles, *input.layout);
    for (std::size_t position = 0; position < order.size(); ++position) {
        while (position >= frames.back().end) close_layer();
        const auto index = order[position];
        const auto& box = boxes[index];
        const auto& style = input.styles[box.node.index];
        if (composed[index] == 0) {
            position += subtree[index] - 1; // Zero opacity hides the whole subtree, including nested layers.
            continue;
        }
        if (style.composition == Composition::layer) {
            if (frames.size() == max_draw_layer_depth + 1) {
                diagnostics.push_back({"layer_limit",
                                       Severity::error,
                                       "/nodes/" + std::to_string(box.node.index) + "/composition",
                                       "Layer nesting exceeds the limit (8).",
                                       {}});
                position += subtree[index] - 1;
                continue;
            }
            frames.push_back({{}, box.clip, box.clip, position + subtree[index], index});
        }
        const float opacity = opacities[index];
        if (!box.visible) continue;

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
        // Background/gradient are the declared fallback of a procedural fill, faded the same way.
        DrawRect fill{
            box.border_box, background, style.corner_radius, {}, style.background_shape, style.background_antialias};
        bool fill_visible = background.a > 0;
        if (style.background_gradient) {
            auto gradient = *style.background_gradient;
            fill_visible = false;
            std::visit(
                [&](auto& g) {
                    g.start_color = faded(g.start_color, opacity);
                    g.end_color = faded(g.end_color, opacity);
                    for (auto& stop : g.stops) stop.color = faded(stop.color, opacity);
                    for (std::size_t i = 0; i < g.stop_count(); ++i) fill_visible |= g.stop(i).color.a > 0;
                },
                gradient);
            fill.color = {};
            fill.gradient = std::move(gradient);
        }
        // Shader phase plus rate times host time, rounded once; reports overflow at the style field.
        const auto evaluate = [&](float& time, float rate, const char* field) {
            if (rate == 0) return true;
            const double evaluated = double(time) + double(rate) * input.host_time;
            if (std::abs(evaluated) > std::numeric_limits<float>::max()) {
                diagnostics.push_back({"time_overflow",
                                       Severity::error,
                                       "/nodes/" + std::to_string(box.node.index) + "/" + field + "/time",
                                       "Evaluated shader time must fit a finite float.",
                                       {}});
                return false;
            }
            time = float(evaluated);
            return true;
        };
        if (style.background_procedural) {
            fill.procedural = *style.background_procedural;
            auto& procedural = *fill.procedural;
            procedural.opacity *= opacity;
            fill_visible |= procedural.opacity > 0;
            if (!evaluate(procedural.time, procedural.time_rate, "background_procedural")) fill_visible = false;
        }
        // The custom shape changes coverage only; color/gradient visibility decides emission.
        if (style.background_custom_shape) {
            fill.custom_shape = *style.background_custom_shape;
            if (!evaluate(fill.custom_shape->time, fill.custom_shape->time_rate, "background_custom_shape"))
                fill_visible = false;
        }
        if (fill_visible) commands.commands.push_back(std::move(fill));
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
        const auto* edit = editing[box.node.index];
        const auto origin = box.content_box().origin;
        if (edit && edit->selection_color.a * opacity > 0)
            for (const auto& rect : edit->selection)
                if (rect.size.width > 0 && rect.size.height > 0)
                    if (const auto placed = shifted(rect, origin, path + "/text_editing/selection"))
                        commands.commands.push_back(DrawRect{*placed, faded(edit->selection_color, opacity)});
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
                commands.commands.push_back(DrawGlyphRun{origin, std::move(*shaped.value), color});
        }
        if (edit && edit->caret && edit->caret_color.a * opacity > 0 && edit->caret->size.width > 0 &&
            edit->caret->size.height > 0)
            if (const auto placed = shifted(*edit->caret, origin, path + "/text_editing/caret"))
                commands.commands.push_back(DrawRect{*placed, faded(edit->caret_color, opacity)});
        // Reject malformed runs from an injected shaper before exposing any commands.
        for (auto& diagnostic : validate(commands)) {
            diagnostic.path = path + diagnostic.path;
            diagnostics.push_back(std::move(diagnostic));
        }
        if (commands.commands.empty()) continue;
        auto& frame = frames.back();
        switch_clip(frame, box.clip);
        for (auto& command : commands.commands) frame.commands.push_back(std::move(command));
    }
    while (frames.size() > 1) close_layer();
    switch_clip(frames[0], std::nullopt);
    UiDrawList list{std::move(frames[0].commands)};
    if (failed || has_error(diagnostics)) return {std::nullopt, std::move(diagnostics)};
    return {std::move(list), std::move(diagnostics)};
}

} // namespace tessera
