#include <tessera/render/draw_list.hpp>
#include <tessera/render/renderer.hpp>
#include "../detail/checks.hpp"
#include <string>
#include <type_traits>
#include <utility>

namespace tessera {

std::vector<Diagnostic> validate(const UiDrawList& list) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    struct Entry {
        bool clip;
        bool axis_aligned; // No rotation/skew in the composed transform.
    };
    std::vector<Entry> stack;
    auto axis_aligned = [&] { return stack.empty() || stack.back().axis_aligned; };
    auto push = [&](Entry entry, const std::string& path) {
        stack.push_back(entry);
        if (stack.size() == max_draw_stack_depth + 1)
            check.error("stack_limit", path, "Clip/transform nesting exceeds the limit (64).");
    };
    auto pop = [&](bool clip, const std::string& path) {
        if (stack.empty() || stack.back().clip != clip) {
            check.error("unbalanced_stack", path, clip ? "PopClip must close the innermost PushClip." :
                                                         "PopTransform must close the innermost PushTransform.");
        } else {
            stack.pop_back();
        }
    };
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto path = "/commands/" + std::to_string(i);
        std::visit([&](const auto& command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, DrawRect> || std::is_same_v<T, DrawBorder>) {
                check.rect(command.rect, path + "/rect");
                check.color(command.color, path + "/color");
                check.non_negative(command.corner_radius, path + "/corner_radius");
                if constexpr (std::is_same_v<T, DrawBorder>) check.edges(command.widths, path + "/widths");
            } else if constexpr (std::is_same_v<T, DrawImage>) {
                check.rect(command.rect, path + "/rect");
                if (command.image.value == 0)
                    check.error("invalid_handle", path + "/image", "Use an image handle issued by the host resource system.");
                check.rect(command.source, path + "/source");
                check.color(command.tint, path + "/tint");
            } else if constexpr (std::is_same_v<T, DrawGlyphRun>) {
                check.point(command.origin, path + "/origin");
                check.positive(command.run.size, path + "/run/size");
                for (std::size_t g = 0; g < command.run.glyphs.size(); ++g)
                    check.point(command.run.glyphs[g].position, path + "/run/glyphs/" + std::to_string(g) + "/position");
                check.color(command.color, path + "/color");
            } else if constexpr (std::is_same_v<T, PushClip>) {
                check.rect(command.rect, path + "/rect");
                if (!axis_aligned())
                    check.error("unsupported_clip", path, "Clipping under a rotated or skewed transform is not supported.");
                push({true, axis_aligned()}, path);
            } else if constexpr (std::is_same_v<T, PushTransform>) {
                const auto& t = command.transform;
                const std::pair<float, const char*> values[] = {
                    {t.a, "a"}, {t.b, "b"}, {t.c, "c"}, {t.d, "d"}, {t.tx, "tx"}, {t.ty, "ty"}};
                bool finite = true;
                for (const auto& [value, name] : values)
                    finite = check.finite(value, path + "/transform/" + name) && finite;
                if (finite && t.a * t.d - t.b * t.c == 0)
                    check.error("singular_transform", path + "/transform", "Use an invertible transform.");
                push({false, axis_aligned() && t.b == 0 && t.c == 0}, path);
            } else if constexpr (std::is_same_v<T, PopClip>) {
                pop(true, path);
            } else {
                pop(false, path);
            }
        }, list.commands[i]);
    }
    if (!stack.empty())
        check.error("unbalanced_stack", "/commands", std::to_string(stack.size()) + " pushed clip/transform entries are never popped.");
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
