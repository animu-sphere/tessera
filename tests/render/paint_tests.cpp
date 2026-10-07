#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using tessera::Dimension;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.children = std::move(children);
    return result;
}
tessera::UiNode text(std::string content) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.properties["text"] = std::move(content);
    return result;
}

struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;

    explicit Fixture(tessera::UiNode root) {
        tessera::UiDocument document;
        document.root = std::move(root);
        auto created = tessera::UiTree::create(document);
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
    }
    void compute(tessera::Size viewport = {100, 80}) {
        auto result = tessera::compute_layout({tree.get(), styles, viewport, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::PaintInput input() { return {tree.get(), styles, &layout, &shaper}; }
    tessera::UiDrawList paint() {
        const auto result = tessera::build_paint_list(input());
        check(result && result.diagnostics.empty(), "Valid paint rejected");
        const auto repeated = tessera::build_paint_list(input());
        check(repeated && *repeated.value == *result.value, "Repeated paint differs");
        check(tessera::validate(*result.value).empty(), "Paint produced an invalid draw list");
        return *result.value;
    }
};

void order_geometry_opacity_and_ownership() {
    Fixture f(box({text("A\nあ"), box()}));
    auto& root = f.styles[0];
    root.background = {1, 0, 0, 0.5f};
    root.border_color = {0, 1, 0, 1};
    root.border = {1, 2, 3, 4};
    root.padding = {5, 6, 7, 8};
    root.corner_radius = 6;
    root.opacity = 0.5f;
    auto& label = f.styles[1];
    label.background = {0, 0, 1, 1};
    label.border_color = {1, 1, 1, 1};
    label.border = {2, 2, 2, 2};
    label.padding = {3, 3, 3, 3};
    label.color = {0.25f, 0.5f, 1, 0.5f};
    label.opacity = 0.5f;
    label.text.font = {17};
    label.text.size = 20;
    // The last sibling overflows the root. Paint must preserve it without inserting a clip.
    f.styles[2].height = Dimension::points(100);
    f.styles[2].background = {1, 1, 0, 1};
    f.compute();
    auto list = f.paint();
    check(list.commands.size() == 6, "Expected parent background/border, text background/border/glyphs, sibling");
    check(std::get<tessera::DrawRect>(list.commands[0]) ==
          tessera::DrawRect{f.layout.boxes[0].border_box, {1, 0, 0, 0.25f}, 6}, "Root paint differs");
    check(std::get<tessera::DrawBorder>(list.commands[1]) ==
          tessera::DrawBorder{f.layout.boxes[0].border_box, root.border, {0, 1, 0, 0.5f}, 6}, "Border paint differs");
    check(std::get<tessera::DrawRect>(list.commands[2]).color == tessera::Color{0, 0, 1, 0.25f},
          "Ancestor opacity must multiply child alpha without changing RGB");
    check(std::holds_alternative<tessera::DrawBorder>(list.commands[3]), "Text border must precede glyphs");
    const auto& glyphs = std::get<tessera::DrawGlyphRun>(list.commands[4]);
    auto shaped = f.shaper.shape("A\nあ", label.text);
    check(shaped && glyphs.run == *shaped.value, "Paint must preserve the shaper's owned run and metrics");
    check(glyphs.origin == f.layout.boxes[1].content_box().origin, "Glyph origin must exclude border and padding");
    check(glyphs.color == tessera::Color{0.25f, 0.5f, 1, 0.125f}, "Glyph alpha differs");
    check(std::get<tessera::DrawRect>(list.commands[5]).rect == f.layout.boxes[2].border_box,
          "Overflowing sibling must paint last at layout coordinates");
    f.tree.reset();
    f.styles.clear();
    f.layout.boxes.clear();
    check(tessera::validate(list).empty() && std::get<tessera::DrawGlyphRun>(list.commands[4]).run.glyphs.size() == 2,
          "Returned draw list must outlive all borrowed inputs");
}

void visibility_display_and_empty() {
    Fixture f(box({box({text("omitted")}), box({text("visible child")}), text(""), text("transparent")}));
    f.styles[0].background = {1, 0, 0, 1};
    f.styles[1].display = tessera::Display::none;
    f.styles[3].visibility = tessera::Visibility::hidden;
    f.styles[3].opacity = 0.5f;
    f.styles[3].background = {0, 1, 0, 1};
    f.styles[6].color.a = 0;
    f.compute();
    auto list = f.paint();
    check(list.commands.size() == 2, "None subtrees, hidden boxes, empty and transparent text must emit nothing");
    check(std::get<tessera::DrawGlyphRun>(list.commands[1]).color.a == 0.5f,
          "Locally visible child of hidden box must paint with ancestor opacity");
    f.styles[0].opacity = 0;
    check(f.paint().commands.empty(), "Zero ancestor opacity must suppress descendant commands");
    f.styles[0].display = tessera::Display::none;
    f.compute();
    check(f.layout.boxes.empty() && f.paint().commands.empty(), "Non-displayed root must produce an empty list");

    Fixture defaults(box());
    defaults.styles[0].border = {2, 2, 2, 2};
    defaults.compute();
    check(defaults.paint().commands.empty(), "Transparent backgrounds/borders must emit nothing");
    defaults.styles[0].border_color = {1, 1, 1, 1};
    defaults.styles[0].border = {};
    defaults.compute({0, 0});
    check(defaults.paint().commands.empty(), "Zero-width border must emit nothing even when opaque");
}

void clip_runs() {
    // Preorder: 0 root (clips), 1 first, 2 nested (clips), 3 inner, 4 last.
    Fixture f(box({box(), box({box()}), box()}));
    f.styles[0].padding = {4, 4, 4, 4};
    f.styles[0].overflow = tessera::Overflow::clip;
    f.styles[2].height = Dimension::points(20);
    f.styles[2].overflow = tessera::Overflow::clip;
    f.styles[3].height = Dimension::points(30);
    for (auto& style : f.styles) style.background = {1, 1, 1, 1};
    f.styles[1].background = {};
    f.compute({100, 30});
    const auto& boxes = f.layout.boxes;
    const auto list = f.paint();
    const tessera::UiDrawList expected{{
        tessera::DrawRect{boxes[0].border_box, {1, 1, 1, 1}},
        tessera::PushClip{*boxes[2].clip},
        tessera::DrawRect{boxes[2].border_box, {1, 1, 1, 1}}, tessera::PopClip{},
        tessera::PushClip{*boxes[3].clip},
        tessera::DrawRect{boxes[3].border_box, {1, 1, 1, 1}}, tessera::PopClip{},
        tessera::PushClip{*boxes[4].clip},
        tessera::DrawRect{boxes[4].border_box, {1, 1, 1, 1}}, tessera::PopClip{},
    }};
    check(list == expected, "Each emitting box must paint inside its own recorded clip");
    check(*boxes[3].clip == tessera::intersect(*boxes[2].clip, boxes[2].padding_box()), "Nested clip differs");

    f.styles[3].background = {};
    f.styles[4].background = {};
    f.styles[1].background = {1, 1, 1, 1};
    check(f.paint().commands.size() == 5, "Adjacent boxes under one clip must share a push/pop pair");

    f.styles[2].overflow = tessera::Overflow::visible;
    check(has(tessera::validate(f.input()), "layout_clip", "/layout/boxes/3/clip"), "Stale clip accepted");
    f.compute({100, 30});
    f.layout.boxes[1].clip->size.width = std::numeric_limits<float>::infinity();
    const auto invalid = tessera::validate(f.input());
    check(has(invalid, "invalid_number", "/layout/boxes/1/clip/size/width") &&
              has(invalid, "layout_clip", "/layout/boxes/1/clip"), "Non-finite clip accepted");
}

void invalid_input_and_topology() {
    const auto missing = tessera::build_paint_list({});
    check(!missing && missing.diagnostics.size() == 3 && has(missing.diagnostics, "missing_input", "/layout"),
          "Missing paint dependencies accepted");
    Fixture f(box({text("label"), box()}));
    f.compute();
    auto input = f.input();
    input.styles = input.styles.first(1);
    check(has(tessera::validate(input), "style_count", "/styles"), "Wrong style count accepted");
    f.styles[2].opacity = 2;
    check(has(tessera::validate(f.input()), "out_of_range", "/styles/2/opacity"), "Invalid style accepted");
    f.styles[2].opacity = 1;
    const auto original = f.layout;
    Fixture other(box());
    f.layout.boxes[0].node = other.tree->root();
    check(has(tessera::validate(f.input()), "layout_node", "/layout/boxes/0/node"), "Foreign tree handle accepted");
    f.layout = original;
    std::swap(f.layout.boxes[1], f.layout.boxes[2]);
    check(has(tessera::validate(f.input()), "layout_node", "/layout/boxes/1/node"), "Reordered boxes accepted");
    f.layout = original;
    f.layout.boxes[1].parent = 2;
    check(has(tessera::validate(f.input()), "layout_parent", "/layout/boxes/1/parent"), "Forward parent accepted");
    f.layout = original;
    f.layout.boxes.pop_back();
    check(has(tessera::validate(f.input()), "layout_count", "/layout/boxes"), "Missing box accepted");
    f.layout = original;
    f.layout.boxes.push_back(original.boxes[0]);
    check(!tessera::build_paint_list(f.input()), "Extra box accepted");
    f.layout = original;
    f.styles[1].display = tessera::Display::none;
    check(!tessera::build_paint_list(f.input()), "Stale display topology accepted");
    f.styles[1].display = tessera::Display::flex;
    f.styles[1].padding.left = 2;
    check(has(tessera::validate(f.input()), "layout_style", "/layout/boxes/1"), "Stale padding accepted");
    f.styles[1].padding.left = 0;
    f.layout.boxes[1].visible = false;
    check(has(tessera::validate(f.input()), "layout_style", "/layout/boxes/1"), "Stale visibility accepted");
    f.layout = original;
    f.layout.boxes[1].border_box.size.width = -1;
    check(has(tessera::validate(f.input()), "out_of_range", "/layout/boxes/1/border_box/size/width"),
          "Negative layout size accepted");
    f.layout = original;
    f.layout.boxes[1].border_box.origin.x = std::numeric_limits<float>::infinity();
    check(has(tessera::validate(f.input()), "invalid_number", "/layout/boxes/1/border_box/origin/x"),
          "Non-finite layout geometry accepted");
    check(!tessera::build_paint_list(f.input()), "Invalid input exposed a partial draw list");
}

class InjectedShaper final : public tessera::TextShaper {
public:
    enum class Mode { failure, silent_failure, malformed, warning, error_with_value } mode = Mode::failure;
    int calls = 0;
    tessera::PlaceholderTextShaper placeholder;
    tessera::Result<tessera::TextMetrics> measure(std::string_view text, const tessera::TextStyle& style,
                                                  const tessera::TextConstraints& constraints) override {
        return placeholder.measure(text, style, constraints);
    }
    tessera::Result<tessera::GlyphRun> shape(std::string_view text, const tessera::TextStyle& style,
                                            const tessera::TextConstraints& constraints) override {
        ++calls;
        if (mode == Mode::failure)
            return {std::nullopt, {{"font_missing", tessera::Severity::error, "/style/font", "Font unavailable.", {}}}};
        if (mode == Mode::silent_failure) return {};
        auto result = placeholder.shape(text, style, constraints);
        if (mode == Mode::malformed) result.value->glyphs[0].position.x = std::numeric_limits<float>::quiet_NaN();
        if (mode == Mode::warning || mode == Mode::error_with_value)
            result.diagnostics.push_back({"font_fallback", mode == Mode::warning ? tessera::Severity::warning :
                                          tessera::Severity::error, "/style/font", "Fallback font used.", {}});
        return result;
    }
};

void shaping_failures_and_warnings() {
    Fixture f(box({text("abc")}));
    f.styles[0].background = {1, 0, 0, 1};
    f.compute();
    InjectedShaper injected;
    auto input = f.input();
    input.text = &injected;
    auto result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "font_missing", "/nodes/1/style/font"),
          "Shaping failure must be located and discard preceding background commands");
    injected.mode = InjectedShaper::Mode::silent_failure;
    result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "text_shape_failed", "/nodes/1/text"), "Silent shaping failure accepted");
    injected.mode = InjectedShaper::Mode::malformed;
    result = tessera::build_paint_list(input);
    check(!result && has(result.diagnostics, "invalid_number", "/nodes/1/commands/0/run/glyphs/0/position/x"),
          "Malformed injected glyph positions accepted");
    injected.mode = InjectedShaper::Mode::warning;
    result = tessera::build_paint_list(input);
    check(result && result.diagnostics.size() == 1 && result.diagnostics[0].path == "/nodes/1/style/font" &&
          result.diagnostics[0].severity == tessera::Severity::warning, "Successful shaping warning lost");
    injected.mode = InjectedShaper::Mode::error_with_value;
    check(!tessera::build_paint_list(input), "Error diagnostic accompanied by a glyph run must still fail");
    const int before = injected.calls;
    f.styles[1].color.a = 0;
    check(tessera::build_paint_list(input) && injected.calls == before, "Transparent text must not call the shaper");
}

} // namespace

int main() {
    try {
        order_geometry_opacity_and_ownership();
        visibility_display_and_empty();
        clip_runs();
        invalid_input_and_topology();
        shaping_failures_and_warnings();
        std::cout << "Paint order/geometry/opacity, ownership, topology, and shaping checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
