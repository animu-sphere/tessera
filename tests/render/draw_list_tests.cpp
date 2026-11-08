#include <tessera/render/renderer.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

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
    tessera::DrawRect shape{{{0, 0}, {10, 5}}, {1, 1, 1, 1}, 100, {}, tessera::FillShape::ellipse, true};
    check(tessera::validate(tessera::UiDrawList{{shape}}).empty(), "Valid ellipse fill rejected");
    shape.shape = static_cast<tessera::FillShape>(255);
    check(has(tessera::validate(tessera::UiDrawList{{shape}}), "unknown_value", "/commands/0/shape"),
          "Invalid shape must not silently become a rectangle");
    tessera::DrawRect procedural{{{0, 0}, {10, 5}}, {}, 0};
    procedural.procedural = tessera::ProceduralFill{"example.wave", {1.f, tessera::Point{0, 1}}, 3, 1};
    check(tessera::validate(tessera::UiDrawList{{procedural}}).empty(), "Valid procedural fill rejected");
    for (const auto& shader :
         {std::string(""), std::string("a b"), std::string(129, 'a'), std::string("caf\xc3\xa9")}) {
        procedural.procedural->shader = shader;
        check(has(tessera::validate(tessera::UiDrawList{{procedural}}), "invalid_identifier",
                  "/commands/0/procedural/shader"),
              "Procedural identity must be 1-128 printable ASCII bytes without spaces");
    }
    procedural.procedural->shader = "example.wave";
    procedural.procedural->parameters = {std::numeric_limits<float>::infinity(), tessera::Color{2, 0, 0, 1}};
    procedural.procedural->time = std::numeric_limits<float>::quiet_NaN();
    procedural.procedural->time_rate = std::numeric_limits<float>::infinity();
    procedural.procedural->opacity = -1;
    const auto procedural_errors = tessera::validate(tessera::UiDrawList{{procedural}});
    check(has(procedural_errors, "invalid_number", "/commands/0/procedural/parameters/0") &&
              has(procedural_errors, "out_of_range", "/commands/0/procedural/parameters/1/r") &&
              has(procedural_errors, "invalid_number", "/commands/0/procedural/time") &&
              has(procedural_errors, "invalid_number", "/commands/0/procedural/time_rate") &&
              has(procedural_errors, "out_of_range", "/commands/0/procedural/opacity"),
          "Procedural values must use located shared numeric validation");
    procedural.procedural = tessera::ProceduralFill{"example.wave", {}, 2};
    tessera::UiDrawList moving{{tessera::DrawRect{}, procedural}};
    check(!tessera::time_dependent(moving) && !tessera::time_dependent(tessera::UiDrawList{}),
          "Static procedural fills and plain lists must not request motion frames");
    std::get<tessera::DrawRect>(moving.commands[1]).procedural->time_rate = .25f;
    check(tessera::time_dependent(moving), "A nonzero time rate must request motion frames");
    procedural.procedural = tessera::ProceduralFill{"example.wave", std::vector<tessera::ProceduralValue>(17, 0.f)};
    check(has(tessera::validate(tessera::UiDrawList{{procedural}}), "procedural_parameter_limit",
              "/commands/0/procedural/parameters"),
          "Procedural parameters must be bounded");
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

void shadows() {
    tessera::DrawShadow shadow{{{4, 4}, {20, 10}}, {.2f, .3f, .4f, .5f}, 4, 8};
    check(tessera::validate(tessera::UiDrawList{{shadow}}).empty(), "Valid shadow rejected");
    shadow.softness = -1;
    shadow.corner_radius = NAN;
    shadow.color.a = 2;
    auto errors = tessera::validate(tessera::UiDrawList{{shadow}});
    check(errors.size() == 3 && has(errors, "out_of_range", "/commands/0/softness") &&
              has(errors, "invalid_number", "/commands/0/corner_radius") &&
              has(errors, "out_of_range", "/commands/0/color/a"),
          "Shadow commands must validate geometry and color with exact locations");
    shadow = {{{4, 4}, {20, 10}}, {}, 0, std::numeric_limits<float>::max()};
    check(has(tessera::validate(tessera::UiDrawList{{shadow}}), "geometry_overflow", "/commands/0/rect"),
          "Unrepresentable expanded shadow bounds must reject before backend execution");
    shadow.softness = std::numeric_limits<float>::infinity();
    errors = tessera::validate(tessera::UiDrawList{{shadow}});
    check(errors.size() == 1 && has(errors, "invalid_number", "/commands/0/softness"),
          "Nonfinite softness must report once");
    shadow.softness = 0;
    check(tessera::validate(tessera::UiDrawList{{shadow}}).empty(), "Hard shadows must be valid");
    shadow.inset = true;
    shadow.offset = {-2, 4};
    shadow.softness = std::numeric_limits<float>::max();
    check(tessera::validate(tessera::UiDrawList{{shadow}}).empty(), "Inset softness must not expand the clipping box");
    shadow.offset.x = NAN;
    check(has(tessera::validate(tessera::UiDrawList{{shadow}}), "invalid_number", "/commands/0/offset/x"),
          "Inset cutout offsets must reject nonfinite coordinates");
    shadow.offset = {1, 0};
    shadow.inset = false;
    shadow.softness = 1;
    check(has(tessera::validate(tessera::UiDrawList{{shadow}}), "out_of_range", "/commands/0/offset"),
          "Outer shadows must not silently ignore inset-only offsets");
}

void strokes() {
    tessera::DrawStroke stroke{{{2, 2}, {20, 10}}, {1, 1, 1, 1}, 1, 4, 3, 2, -7};
    check(tessera::validate(tessera::UiDrawList{{stroke}}).empty(), "Valid rounded dashed stroke rejected");
    stroke.width = -1;
    stroke.corner_radius = NAN;
    stroke.dash_offset = INFINITY;
    stroke.dash_length = 0;
    const auto errors = tessera::validate(tessera::UiDrawList{{stroke}});
    check(errors.size() == 4 && has(errors, "out_of_range", "/commands/0/width") &&
              has(errors, "invalid_number", "/commands/0/corner_radius") &&
              has(errors, "invalid_number", "/commands/0/dash_offset") &&
              has(errors, "invalid_dash", "/commands/0/dash_length"),
          "Stroke geometry and degenerate dash patterns must reject with exact locations");
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
    auto& gradient = std::get<tessera::LinearGradient>(*rect.gradient);
    gradient.end = gradient.start;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "degenerate_gradient", "/commands/0/gradient/end"),
          "Coincident gradient endpoints accepted");
    gradient.end = {2, 1};
    gradient.start_color.a = -1;
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
        gradient.stops[1].offset = offset;
        const auto diagnostics = tessera::validate(tessera::UiDrawList{{rect}});
        check(has(diagnostics, std::isfinite(offset) ? "out_of_range" : "invalid_number",
                  "/commands/0/gradient/stops/1/offset"),
              "Stop offsets must validate with exact locations");
    }
    rect.gradient = valid;
    gradient.stops[2].offset = .3f;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "invalid_gradient_stops",
              "/commands/0/gradient/stops/2/offset"),
          "Unsorted stops must reject instead of being silently reordered");
    rect.gradient = valid;
    gradient.stops[1].color.a = -1;
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "out_of_range", "/commands/0/gradient/stops/1/color/a"),
          "Stop colors must validate");
    gradient.stops.resize(1);
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "invalid_gradient_stops", "/commands/0/gradient/stops"),
          "A nonempty explicit stop list needs at least two stops");
    rect.gradient = tessera::RadialGradient{};
    check(tessera::validate(tessera::UiDrawList{{rect}}).empty(), "Default radial geometry must be valid");
    auto& radial = std::get<tessera::RadialGradient>(*rect.gradient);
    radial.radius = {2, 3};
    check(tessera::validate(tessera::UiDrawList{{rect}}).empty(), "Radii may extend beyond the box");
    radial.center = {-1, NAN};
    radial.radius = {0, -1};
    radial.stops = {{.8f, {}}, {.2f, {}}};
    errors = tessera::validate(tessera::UiDrawList{{rect}});
    check(errors.size() == 5 && has(errors, "out_of_range", "/commands/0/gradient/center/x") &&
              has(errors, "invalid_number", "/commands/0/gradient/center/y") &&
              has(errors, "out_of_range", "/commands/0/gradient/radius/width") &&
              has(errors, "out_of_range", "/commands/0/gradient/radius/height") &&
              has(errors, "invalid_gradient_stops", "/commands/0/gradient/stops/1/offset"),
          "Radial geometry and stops must use located validation");
    radial = {};
    radial.radius.width = std::numeric_limits<float>::infinity();
    check(has(tessera::validate(tessera::UiDrawList{{rect}}), "invalid_number", "/commands/0/gradient/radius/width"),
          "Nonfinite radial radii must reject");
}

} // namespace

int main() {
    try {
        valid_list();
        invalid_lists();
        frame_info();
        gradients();
        shadows();
        strokes();
        std::cout << "Draw-list order/stack/resource-handle and frame checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
