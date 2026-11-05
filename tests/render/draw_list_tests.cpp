#include <tessera/render/renderer.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using tessera::test::check;
using tessera::test::has;

namespace {

void valid_list() {
    tessera::PlaceholderTextShaper shaper;
    auto run = shaper.shape("Start", {});
    check(static_cast<bool>(run), "Shaping failed");
    const tessera::UiDrawList list{{
        tessera::DrawRect{{{0, 0}, {200, 100}}, {0.1f, 0.1f, 0.1f, 1}, 4},
        tessera::PushClip{{{8, 8}, {184, 84}}},
        tessera::PushTransform{{2, 0, 0, 2, 8, 8}},
        tessera::DrawBorder{{{0, 0}, {50, 20}}, {1, 1, 1, 1}, {1, 1, 1, 0.5f}, 0},
        tessera::DrawImage{{{0, 0}, {16, 16}}, {42}},
        tessera::DrawGlyphRun{{20, 0}, *run.value, {1, 1, 1, 1}},
        tessera::PopTransform{},
        tessera::PopClip{},
    }};
    check(tessera::validate(list).empty(), "Valid draw list rejected");
    check(tessera::validate(tessera::UiDrawList{}).empty(), "Empty draw list rejected");
}

void invalid_lists() {
    const tessera::UiDrawList unbalanced{{
        tessera::PopClip{},
        tessera::PushClip{},
        tessera::PopTransform{},
    }};
    auto errors = tessera::validate(unbalanced);
    check(has(errors, "unbalanced_stack", "/commands/0"), "Pop without push accepted");
    check(has(errors, "unbalanced_stack", "/commands/2"), "Mismatched pop accepted");
    check(has(errors, "unbalanced_stack", "/commands"), "Unclosed push accepted");

    const tessera::UiDrawList invalid{{
        tessera::PushTransform{{0, 1, -1, 0, 0, 0}}, // 90-degree rotation.
        tessera::PushClip{{{0, 0}, {10, 10}}},
        tessera::PopClip{},
        tessera::PushTransform{{1, 2, 2, 4, 0, 0}},
        tessera::PopTransform{},
        tessera::PopTransform{},
        tessera::DrawImage{},
        tessera::DrawRect{{{0, 0}, {-1, 5}}, {2, 0, 0, 1}, 0},
    }};
    errors = tessera::validate(invalid);
    check(has(errors, "unsupported_clip", "/commands/1"), "Rotated clip accepted");
    check(has(errors, "singular_transform", "/commands/3/transform"), "Singular transform accepted");
    check(has(errors, "invalid_handle", "/commands/6/image"), "Null image handle accepted");
    check(has(errors, "out_of_range", "/commands/7/rect/size/width"), "Negative rectangle accepted");
    check(has(errors, "out_of_range", "/commands/7/color/r"), "Out-of-range color accepted");
    check(errors.size() == 5, "Unexpected draw-list diagnostics");

    tessera::UiDrawList deep;
    for (std::size_t i = 0; i < tessera::max_draw_stack_depth; ++i) deep.commands.push_back(tessera::PushClip{});
    for (std::size_t i = 0; i < tessera::max_draw_stack_depth; ++i) deep.commands.push_back(tessera::PopClip{});
    check(tessera::validate(deep).empty(), "Allowed stack depth rejected");
    deep.commands.insert(deep.commands.begin(), tessera::PushTransform{});
    deep.commands.push_back(tessera::PopTransform{});
    check(has(tessera::validate(deep), "stack_limit", "/commands/64"), "Excessive stack depth accepted");
}

void frame_info() {
    check(tessera::validate(tessera::FrameInfo{1, {800, 600}, 1.5f}).empty(), "Valid frame rejected");
    const auto errors = tessera::validate(tessera::FrameInfo{});
    check(has(errors, "out_of_range", "/frame") && has(errors, "out_of_range", "/logical_size/width"),
          "Invalid frame accepted");
}

void gradients() {
    tessera::DrawRect rect{
        {{0, 0}, {20, 10}}, {}, 4, tessera::LinearGradient{{0, 0}, {1, 1}, {1, 0, 0, 0}, {0, 0, 1, 1}}};
    check(tessera::validate(tessera::UiDrawList{{rect}}).empty(), "Valid gradient rejected");
    rect.gradient->end = rect.gradient->start;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "degenerate_gradient", "/commands/0/gradient/end"),
          "Coincident gradient endpoints accepted");
    rect.gradient->end = {2, 1};
    rect.gradient->start_color.a = -1;
    auto errors = tessera::validate(tessera::UiDrawList{{rect}});
    check(has(errors, "out_of_range", "/commands/0/gradient/end/x") &&
              has(errors, "out_of_range", "/commands/0/gradient/start_color/a"),
          "Gradient coordinates and colors must be validated");
    rect.gradient = tessera::LinearGradient{
        {0, 0}, {1, 0}, {}, {}, {{.2f, {1, 0, 0, 1}}, {.5f, {}}, {.5f, {}}, {.8f, {0, 0, 1, 1}}}};
    check(tessera::validate(tessera::UiDrawList{{rect}}).empty(), "Interior and coincident stops must be valid");
    const auto valid = *rect.gradient;
    for (float offset : {-1.f, 2.f, std::numeric_limits<float>::infinity(), NAN}) {
        rect.gradient = valid;
        rect.gradient->stops[1].offset = offset;
        const auto diagnostics = tessera::validate(tessera::UiDrawList{{rect}});
        check(has(diagnostics, std::isfinite(offset) ? "out_of_range" : "invalid_number",
                  "/commands/0/gradient/stops/1/offset"),
              "Stop offsets must validate with exact locations");
    }
    rect.gradient = valid;
    rect.gradient->stops[2].offset = .3f;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "invalid_gradient_stops",
              "/commands/0/gradient/stops/2/offset"),
          "Unsorted stops must reject instead of being silently reordered");
    rect.gradient = valid;
    rect.gradient->stops[1].color.a = -1;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "out_of_range", "/commands/0/gradient/stops/1/color/a"),
          "Stop colors must validate");
    rect.gradient->stops.resize(1);
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "invalid_gradient_stops", "/commands/0/gradient/stops"),
          "A nonempty explicit stop list needs at least two stops");
}

} // namespace

int main() {
    try {
        valid_list();
        invalid_lists();
        frame_info();
        gradients();
        std::cout << "Draw-list order/stack/resource-handle and frame checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
