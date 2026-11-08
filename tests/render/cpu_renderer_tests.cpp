#include <tessera/render/cpu_renderer.hpp>
#include <tessera/inspection/offscreen_runner.hpp>
#include "../check.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
namespace {
std::array<std::uint8_t, 4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto i = (std::size_t(y) * image.extent.width + x) * 4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}
CapturedImage capture(CpuFrameCapture& renderer, const UiDrawList& list, Size size = {8, 8}, float scale = 1) {
    auto extent = capture_extent(size, scale);
    auto result = renderer.capture({0, size, scale, *extent.value, &list});
    if (!result)
        for (const auto& d : result.diagnostics) std::cerr << d.code << ' ' << d.path << ' ' << d.message << '\n';
    check(bool(result), "CPU capture rejected valid fixture");
    return std::move(*result.value);
}
void pixels_and_geometry() {
    CpuFrameCapture frames;
    UiDrawList list{{DrawRect{{{0, 0}, {8, 8}}, {1, 0, 0, 1}}, DrawRect{{{2, 2}, {4, 4}}, {0, 0, 1, 0.5f}}}};
    auto image = capture(frames, list);
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255}, "Fill pixels differ");
    check(pixel(image, 3, 3) == std::array<std::uint8_t, 4>{188, 0, 188, 255},
          "Source-over must blend in linear light");
    check(capture(frames, list) == image, "Repeated CPU rendering differs");
    image = capture(frames, {{DrawRect{{{0, 0}, {8, 8}}, {1, 1, 1, 1}, 4}}});
    check(pixel(image, 0, 0)[3] == 0 && pixel(image, 4, 0)[3] == 255, "Circular rounded corner coverage differs");
    image = capture(frames, {{DrawBorder{{{0, 0}, {8, 8}}, {1, 2, 3, 1}, {0, 1, 0, 1}, 0}}});
    check(pixel(image, 0, 3)[1] == 255 && pixel(image, 5, 3)[3] == 0 && pixel(image, 6, 3)[1] == 255 &&
              pixel(image, 4, 4)[3] == 0 && pixel(image, 4, 5)[1] == 255,
          "Unequal inside border widths differ");
    image = capture(frames, {{DrawBorder{{{0, 0}, {8, 8}}, {5, 5, 5, 5}, {1, 1, 1, 1}, 4}}});
    check(pixel(image, 4, 4)[3] == 255 && pixel(image, 0, 0)[3] == 0, "Collapsed inner border or rounded edge differs");
    list = {{PushClip{{{0.6f, 0}, {2, 8}}}, PushTransform{{-1, 0, 0, 1, 8, 0}}, PushClip{{{5.4f, 0}, {2, 8}}},
             DrawRect{{{0, 0}, {8, 8}}, {1, 1, 1, 1}}, PopClip{}, PopTransform{}, PopClip{}}};
    image = capture(frames, list);
    check(pixel(image, 0, 3)[3] == 0 && pixel(image, 1, 3)[3] == 255 && pixel(image, 2, 3)[3] == 255 &&
              pixel(image, 3, 3)[3] == 0,
          "Mirrored nested fractional clips disagree with pixel centers");
    image = capture(frames, {{PushTransform{{1, 0, 0, 1, 3, 0}}, PushTransform{{2, 0, 0, 1, 0, 2}},
                              DrawRect{{{0, 0}, {1, 1}}, {1, 1, 1, 1}}, PopTransform{}, PopTransform{},
                              DrawRect{{{0, 0}, {1, 1}}, {1, 0, 0, 1}}}});
    check(pixel(image, 3, 2)[3] == 255 && pixel(image, 4, 2)[3] == 255 && pixel(image, 0, 0)[0] == 255 &&
              pixel(image, 2, 2)[3] == 0,
          "Transform composition/restoration differs");
    image = capture(frames,
                    {{PushTransform{{0, 1, -1, 0, 4, 0}}, DrawRect{{{0, 0}, {2, 1}}, {1, 1, 1, 1}}, PopTransform{}}});
    check(pixel(image, 3, 0)[3] == 255 && pixel(image, 3, 1)[3] == 255 && pixel(image, 4, 0)[3] == 0,
          "Rotated primitive coverage differs");
    image = capture(frames,
                    {{PushTransform{{0, 1, -1, 0, 4, 0}}, DrawRect{{{0, 0}, {2, 2}}, {1, 1, 1, 1}}, PopTransform{}}},
                    {8, 8}, 1.25f);
    check(pixel(image, 2, 0)[3] == 255 && pixel(image, 5, 0)[3] == 0,
          "Rotated edge at pixel center violates framebuffer top/left rule");
    image = capture(frames,
                    {{PushTransform{{-1, 0, 0, 1, 4, 0}}, DrawRect{{{0, 0}, {2, 2}}, {1, 1, 1, 1}}, PopTransform{}}},
                    {8, 8}, 1.25f);
    check(pixel(image, 2, 0)[3] == 255 && pixel(image, 5, 0)[3] == 0,
          "Mirrored edge at pixel center violates framebuffer top/left rule");
    image = capture(frames,
                    {{PushTransform{{1, 0, 0.5f, 1, 0, 0}}, DrawRect{{{0, 0}, {2, 2}}, {1, 1, 1, 1}}, PopTransform{}}});
    check(pixel(image, 0, 0)[3] == 255 && pixel(image, 0, 1)[3] == 0 && pixel(image, 2, 1)[3] == 255,
          "Skewed primitive coverage differs");
    image = capture(frames, {{DrawRect{{{0, 0}, {1, 1}}, {1, 1, 1, 1}}}}, {2.1f, 2.1f}, 1.5f);
    check(image.extent == CaptureExtent{4, 4} && pixel(image, 0, 0)[3] == 255 && pixel(image, 1, 0)[3] == 0,
          "Fractional scale/half-open edges differ");
}
void strokes() {
    CpuFrameCapture frames;
    DrawStroke thin{{{3.25f, 3.25f}, {9.5f, 7.5f}}, {1, 1, 1, 1}, .5f};
    auto image = capture(frames, {{thin}}, {16, 16});
    check(pixel(image, 7, 3)[3] == 128 && pixel(image, 7, 4)[3] == 0 && pixel(image, 2, 7)[3] == 0 &&
              pixel(image, 3, 7)[3] == 128,
          "Half-pixel strokes must retain half coverage on both axes instead of disappearing");
    thin.width = .25f;
    check(pixel(capture(frames, {{thin}}, {16, 16}), 7, 3)[3] == 64,
          "Quarter-pixel strokes must retain measurable coverage");
    thin.width = .5f;
    check(pixel(capture(frames, {{thin}}, {16, 16}, 1.5f), 6, 5)[3] == 128,
          "Subsample positions must transform directly so exact inner boundaries exclude their samples");
    DrawStroke dashed{{{2, 2}, {20, 12}}, {0, 0, 1, 1}, 1, 0, 3, 2};
    image = capture(frames, {{dashed}}, {24, 16});
    check(pixel(image, 4, 2)[3] == 255 && pixel(image, 6, 2)[3] == 0 && pixel(image, 21, 5)[3] == 255 &&
              pixel(image, 21, 6)[3] == 128 && pixel(image, 21, 7)[3] == 0,
          "Dash arclength must continue from the top edge around the corner without restarting on the right");
    dashed.dash_offset = -2;
    const auto shifted = capture(frames, {{dashed}}, {24, 16});
    dashed.dash_offset = 3;
    check(capture(frames, {{dashed}}, {24, 16}) == shifted,
          "Negative dash phases must normalize to the same periodic pattern");
    dashed.corner_radius = 5;
    dashed.width = 1.5f;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        image = capture(frames, {{dashed}}, {24, 16}, scale);
        check(capture(frames, {{dashed}}, {24, 16}, scale) == image, "Stroke captures must be deterministic");
        const auto clipped = capture(frames, {{PushClip{{{8.25f, 0}, {8, 16}}}, dashed, PopClip{}}}, {24, 16}, scale);
        const auto mirrored =
            capture(frames, {{PushTransform{{-1, 0, 0, 1, 24, 0}}, dashed, PopTransform{}}}, {24, 16}, scale);
        unsigned partial = 0, opaque = 0;
        for (unsigned y = 0; y < image.extent.height; ++y)
            for (unsigned x = 0; x < image.extent.width; ++x) {
                const auto alpha = pixel(image, x, y)[3];
                partial += alpha > 0 && alpha < 255;
                opaque += alpha == 255;
                const double lx = (x + .5) / scale;
                check(pixel(clipped, x, y) ==
                          (lx >= 8.25 && lx < 16.25 ? pixel(image, x, y) : std::array<std::uint8_t, 4>{}),
                      "Clips must trim stroke pixels without moving dash phase or changing clip-edge sampling");
                check(pixel(mirrored, x, y) == pixel(image, image.extent.width - 1 - x, y),
                      "Mirroring must carry the entire rounded dash pattern and coverage");
            }
        check(partial >= 8 && opaque >= 8, "Rounded dashes must retain smooth edge pixels and opaque dash interiors");
        auto solid = dashed;
        solid.gap_length = 0;
        const auto ring = capture(frames, {{solid}}, {24, 16}, scale);
        check(pixel(ring, unsigned(12 * scale), unsigned(8 * scale))[3] == 0,
              "Rounded strokes must leave their center transparent");
        solid.corner_radius = 100;
        const auto clamped = capture(frames, {{solid}}, {24, 16}, scale);
        solid.corner_radius = 6;
        check(capture(frames, {{solid}}, {24, 16}, scale) == clamped,
              "Stroke radii must clamp before inner subtraction");
        solid.width = 100;
        const auto collapsed = capture(frames, {{solid}}, {24, 16}, scale);
        solid.width = 6;
        check(capture(frames, {{solid}}, {24, 16}, scale) == collapsed,
              "Excessive stroke widths must saturate without inverted inner geometry");
    }
    thin.width = .5f;
    thin.color = {0, 0, 1, .5f};
    image = capture(frames, {{DrawRect{{{0, 0}, {16, 16}}, {1, 0, 0, 1}}, thin}}, {16, 16});
    check(pixel(image, 7, 3) == std::array<std::uint8_t, 4>{225, 0, 137, 255},
          "Stroke coverage must attenuate alpha before linear-light source-over blending");
    thin.width = 0;
    check(capture(frames, {{thin}}, {16, 16}) == capture(frames, {}, {16, 16}),
          "Zero-width strokes must paint nothing");
    thin.width = 1;
    thin.rect.size.width = 0;
    check(capture(frames, {{thin}}, {16, 16}) == capture(frames, {}, {16, 16}), "Zero-area strokes must paint nothing");
}

void analytic_fills() {
    CpuFrameCapture frames;
    DrawRect circle{{{1, 1}, {4, 4}}, {1, 1, 1, 1}, 0, {}, FillShape::ellipse, true};
    auto image = capture(frames, {{circle}});
    check(pixel(image, 1, 1)[3] == 96 && pixel(image, 2, 2)[3] == 255 && pixel(image, 0, 0)[3] == 0,
          "Circle must retain exact six-of-sixteen corner coverage");
    circle.corner_radius = 100;
    check(capture(frames, {{circle}}) == image, "Ellipse geometry must not depend on rectangle corner radius");
    circle.antialias = false;
    check(pixel(capture(frames, {{circle}}), 1, 1)[3] == 0,
          "Hard ellipse coverage must remain an explicit center-sampled alternative");
    DrawRect thin{{{1.25f, 1}, {.25f, 4}}, {1, 1, 1, 1}, 0, {}, FillShape::rectangle, true};
    check(pixel(capture(frames, {{thin}}), 1, 2)[3] == 64,
          "Quarter-pixel smooth fills must retain coverage instead of disappearing");
    thin.rect = {{1.5f, 1}, {3, 4}};
    check(pixel(capture(frames, {{thin}}), 1, 2)[3] == 128 && pixel(capture(frames, {{thin}}), 4, 2)[3] == 128,
          "Smooth fill bounds must include both half-pixel coverage fringes");
    DrawRect rounded{{{1, 1}, {4, 4}}, {1, 1, 1, 1}, 100, {}, FillShape::rectangle, true};
    circle.antialias = true;
    check(capture(frames, {{rounded}}) == image, "Clamped rounded rectangle must match its inscribed circle");
    for (auto size : {Size{4, 4}, Size{6, 2}, Size{2, 6}})
        for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
            DrawRect ellipse{{{1.25f, 1.25f}, size}, {0, 0, 1, .5f}, 0, {}, FillShape::ellipse, true};
            const auto actual = capture(frames, {{ellipse}}, {10, 10}, scale);
            for (unsigned y = 0; y < actual.extent.height; ++y)
                for (unsigned x = 0; x < actual.extent.width; ++x) {
                    unsigned count = 0;
                    for (unsigned sy = 0; sy < 4; ++sy)
                        for (unsigned sx = 0; sx < 4; ++sx) {
                            const double px = ((x + (sx + .5) / 4) / scale - 1.25) / size.width;
                            const double py = ((y + (sy + .5) / 4) / scale - 1.25) / size.height;
                            count += px >= 0 && px < 1 && py >= 0 && py < 1 &&
                                     (px - .5) * (px - .5) + (py - .5) * (py - .5) <= .25;
                        }
                    check(pixel(actual, x, y)[3] == std::uint8_t(std::lround(count * .5 / 16 * 255)),
                          "Ellipse pixels must match the independent normalized quadratic oracle at four scales");
                }
            auto gradient = ellipse;
            gradient.gradient = LinearGradient{{0, 0}, {1, 0}, {0, 0, 1, .5f}, {0, 0, 1, .5f}};
            check(capture(frames, {{gradient}}, {10, 10}, scale) == actual,
                  "Premultiplied gradient coverage must attenuate RGB and alpha exactly once");
            const auto clipped = capture(frames, {{PushClip{{{3, 0}, {7, 10}}}, ellipse, PopClip{}}}, {10, 10}, scale);
            for (unsigned y = 0; y < actual.extent.height; ++y)
                for (unsigned x = 0; x < actual.extent.width; ++x)
                    check(pixel(clipped, x, y) ==
                              ((x + .5) / scale >= 3 ? pixel(actual, x, y) : std::array<std::uint8_t, 4>{}),
                          "Clips must preserve whole-pixel center trimming and unshifted ellipse coverage");
        }
    circle.rect.size.width = 0;
    check(capture(frames, {{circle}}) == capture(frames, {}), "Empty ellipse must not divide by zero or paint");
}

void procedural_fallbacks() {
    CpuFrameCapture frames;
    DrawRect plain{{{1.25f, 1}, {5.5f, 4}}, {.2f, .6f, .4f, .75f}, 100, {}, FillShape::ellipse, true};
    auto requested = plain;
    requested.procedural = ProceduralFill{"example.wave", {Color{1, 0, 0, 1}}, 2};
    check(capture(frames, {{requested}}) == capture(frames, {{plain}}),
          "The CPU reference must draw the declared fallback, not execute or approximate the shader");
    requested.color = {};
    check(capture(frames, {{requested}}) == capture(frames, {}), "A transparent fallback must omit the fill");
    requested.gradient = LinearGradient{{0, 0}, {1, 0}, {1, 0, 0, 1}, {0, 0, 1, 1}};
    plain.color = {};
    plain.gradient = requested.gradient;
    check(capture(frames, {{requested}}) == capture(frames, {{plain}}), "A gradient fallback must match its gradient");

    CpuRenderer renderer;
    std::vector<std::uint8_t> pixels(8 * 8 * 4);
    renderer.set_target({{8, 8}, pixels});
    check(renderer.submit({1, {8, 8}, 1}, {{requested, plain, requested}}).empty() &&
              renderer.procedural_fallbacks() == 2,
          "Every procedural request must be reported as a CPU fallback");
    requested.procedural->opacity = 2;
    check(has(renderer.submit({2, {8, 8}, 1}, {{requested}}), "out_of_range", "/commands/0/procedural/opacity") &&
              renderer.procedural_fallbacks() == 2,
          "Rejected submissions must keep the last successful fallback count");
}

void linear_gradients() {
    CpuFrameCapture frames;
    DrawRect rect{{{0, 0}, {8, 8}}, {}, 3, LinearGradient{{.25f, 0}, {.75f, 0}, {1, 0, 0, 0}, {0, 0, 1, 1}}};
    const auto encode = [](double value) {
        const double srgb = value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
        return static_cast<std::uint8_t>(std::floor(srgb * 255 + .5));
    };
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto image = capture(frames, {{rect}}, {8, 8}, scale);
        check(image == capture(frames, {{rect}}, {8, 8}, scale), "Gradient capture must be deterministic");
        check(pixel(image, 0, 0)[3] == 0, "Gradient must preserve rounded coverage");
        for (unsigned x = 0; x < image.extent.width; ++x) {
            const double t = std::clamp(((x + .5) / scale - 2) / 4, 0.0, 1.0);
            const auto actual = pixel(image, x, image.extent.height / 2);
            check(actual ==
                      std::array<std::uint8_t, 4>{0, 0, encode(t), static_cast<std::uint8_t>(std::floor(t * 255 + .5))},
                  "Transparent stop RGB must not bleed; gradient sampling/clamping must use logical pixel centers");
        }
    }
    rect.corner_radius = 0;
    rect.gradient = LinearGradient{{0, 0}, {1, 0}, {1, 0, 0, 1}, {0, 0, 1, 1}};
    const auto image = capture(frames, {{rect}});
    check(pixel(image, 3, 4) == std::array<std::uint8_t, 4>{encode(.5625), 0, encode(.4375), 255},
          "Opaque stops must interpolate in linear light rather than encoded sRGB");
    const auto mirrored = capture(frames, {{PushTransform{{-1, 0, 0, 1, 8, 0}}, rect, PopTransform{}}});
    for (unsigned x = 0; x < 8; ++x)
        check(pixel(mirrored, x, 4) == pixel(image, 7 - x, 4),
              "Gradient must follow local coordinates under transforms");
    rect.rect.size = {8, 4};
    auto& gradient = std::get<LinearGradient>(*rect.gradient);
    gradient.end = {1, 1};
    const auto diagonal = capture(frames, {{rect}});
    const double t = (3.5 * 8 + 1.5 * 4) / 80;
    check(pixel(diagonal, 3, 1) == std::array<std::uint8_t, 4>{encode(1 - t), 0, encode(t), 255},
          "Diagonal gradient projection must respect the rectangle aspect ratio");
    gradient.end = gradient.start;
    const UiDrawList invalid{{rect}};
    const auto rejected = frames.capture({0, {8, 8}, 1, {8, 8}, &invalid});
    check(!rejected && has(rejected.diagnostics, "degenerate_gradient", "/commands/0/gradient/end"),
          "Invalid gradients must reject before rendering");
    gradient.end = {1, 0};
    rect.rect.size.width = 0;
    check(pixel(capture(frames, {{rect}}), 0, 0)[3] == 0, "Zero-area gradient must emit no pixels");
    rect.rect.size = {8, 8};
    const auto clipped = capture(frames, {{PushClip{{{2.25f, 0}, {3, 8}}}, rect, PopClip{}}});
    check(pixel(clipped, 1, 4)[3] == 0 && pixel(clipped, 2, 4) == pixel(image, 2, 4) && pixel(clipped, 5, 4)[3] == 0,
          "Gradient clipping must preserve the original segment rather than restart at the clip edge");
}

void multi_stop_gradients() {
    CpuFrameCapture frames;
    DrawRect rect{
        {{0, 0}, {8, 8}},
        {},
        0,
        LinearGradient{{0, 0},
                       {1, 0},
                       {},
                       {},
                       {{.25f, {1, 0, 0, 0}}, {.5f, {0, 1, 0, .5f}}, {.5f, {0, 0, 1, .75f}}, {.75f, {1, 1, 1, 1}}}}};
    const auto encode = [](double value) {
        return static_cast<std::uint8_t>(
            std::floor((value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055) * 255 + .5));
    };
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto image = capture(frames, {{rect}}, {8, 8}, scale);
        const auto mirrored =
            capture(frames, {{PushTransform{{-1, 0, 0, 1, 8, 0}}, rect, PopTransform{}}}, {8, 8}, scale);
        const auto clipped = capture(frames, {{PushClip{{{2.25f, 0}, {3, 8}}}, rect, PopClip{}}}, {8, 8}, scale);
        for (unsigned x = 0; x < image.extent.width; ++x) {
            const double t = (x + .5) / (8 * scale);
            const double a = t < .5 ? std::clamp((t - .25) * 4, 0.0, 1.0) : std::clamp((t - .5) * 4, 0.0, 1.0);
            const double alpha = t < .5 ? .5 * a : .75 + .25 * a;
            const std::array<std::uint8_t, 4> expected{
                t < .5 ? std::uint8_t{0} : encode(a), t < .5 ? encode(.5 * a) : encode(a),
                t < .5 ? std::uint8_t{0} : encode(alpha), static_cast<std::uint8_t>(std::floor(alpha * 255 + .5))};
            check(pixel(image, x, 4) == expected,
                  "Nonuniform stops must clamp outside their range and interpolate premultiplied linear-light colors");
            check(pixel(mirrored, image.extent.width - x - 1, 4) == expected,
                  "Explicit stops must follow local coordinates under reflection");
            if ((x + .5) / scale >= 2.25 && (x + .5) / scale < 5.25)
                check(pixel(clipped, x, 4) == expected, "Clipping must not restart multi-stop interpolation");
            else
                check(pixel(clipped, x, 4)[3] == 0, "Multi-stop fills must respect the clip");
        }
        check(image == capture(frames, {{rect}}, {8, 8}, scale), "Multi-stop capture must be deterministic");
    }
    std::get<LinearGradient>(*rect.gradient).stops = {{0, {1, 0, 0, 1}},      {0, {0, 1, 0, 1}}, {.4375f, {0, 1, 0, 1}},
                                                      {.4375f, {0, 0, 1, 1}}, {1, {0, 0, 1, 1}}, {1, {1, 1, 1, 1}}};
    auto image = capture(frames, {{rect}});
    check(pixel(image, 0, 4) == std::array<std::uint8_t, 4>{0, 255, 0, 255} &&
              pixel(image, 3, 4) == std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "The last coincident stop must win at its exact offset, including endpoint duplicates");
    std::get<LinearGradient>(*rect.gradient).stops = {{.4375f, {1, 0, 0, 1}}, {.4375f, {0, 0, 1, 1}}};
    image = capture(frames, {{rect}});
    check(pixel(image, 2, 4)[0] == 255 && pixel(image, 3, 4)[2] == 255,
          "All-coincident stops must form a hard transition without division by zero");
}

void radial_gradients() {
    CpuFrameCapture frames;
    DrawRect rect{{{0, 0}, {16, 8}}, {}, 0, RadialGradient{{.25f, .5f}, {.5f, .5f}, {1, 0, 0, 0}, {0, 0, 1, 1}}};
    const auto encode = [](double value) {
        return static_cast<std::uint8_t>(
            std::floor((value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055) * 255 + .5));
    };
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto image = capture(frames, {{rect}}, {16, 8}, scale);
        for (unsigned y = 0; y < image.extent.height; ++y)
            for (unsigned x = 0; x < image.extent.width; ++x) {
                const double t = std::min(std::hypot(((x + .5) / scale - 4) / 8, ((y + .5) / scale - 4) / 4), 1.0);
                check(pixel(image, x, y) ==
                          std::array<std::uint8_t, 4>{0, 0, encode(t),
                                                      static_cast<std::uint8_t>(std::floor(t * 255 + .5))},
                      "Radial interpolation must use elliptical local distance, clamp and premultiplied linear colors");
            }
        check(image == capture(frames, {{rect}}, {16, 8}, scale), "Radial captures must be deterministic");
        const auto mirrored =
            capture(frames, {{PushTransform{{-1, 0, 0, 1, 16, 0}}, rect, PopTransform{}}}, {16, 8}, scale);
        for (unsigned x = 0; x < image.extent.width; ++x)
            check(pixel(mirrored, x, 3) == pixel(image, image.extent.width - 1 - x, 3),
                  "Radial centers and radii must follow the local transform");
        const auto clipped = capture(frames, {{PushClip{{{4, 0}, {8, 8}}}, rect, PopClip{}}}, {16, 8}, scale);
        const auto cx = static_cast<unsigned>(6 * scale), cy = static_cast<unsigned>(4 * scale);
        check(pixel(clipped, cx, cy) == pixel(image, cx, cy) && pixel(clipped, 0, cy)[3] == 0,
              "Clipping must not recenter a radial fill");
    }
    rect.rect.size = {8, 8};
    rect.gradient =
        RadialGradient{.center = {.0625f, .0625f},
                       .radius = {.5f, .25f},
                       .stops = {{.25f, {1, 0, 0, 1}}, {.5f, {0, 1, 0, 1}}, {.5f, {0, 0, 1, 1}}, {.75f, {1, 1, 1, 1}}}};
    auto image = capture(frames, {{rect}});
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{255, 0, 0, 255} &&
              pixel(image, 2, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 255} &&
              pixel(image, 0, 1) == pixel(image, 2, 0) &&
              pixel(image, 7, 7) == std::array<std::uint8_t, 4>{255, 255, 255, 255},
          "Radial stops must clamp and the last coincident color must win at an exact elliptical radius");
    auto& radial = std::get<RadialGradient>(*rect.gradient);
    radial.stops = {{.5f, {1, 0, 0, 1}}, {.5f, {0, 0, 1, 1}}};
    image = capture(frames, {{rect}});
    check(pixel(image, 1, 0)[0] == 255 && pixel(image, 2, 0)[2] == 255,
          "All-coincident radial stops must form a hard transition");
    radial = RadialGradient{{.5f, .5f}, {.5f, .5f}, {1, 0, 0, 1}, {0, 0, 1, 1}};
    rect.corner_radius = 3;
    image = capture(frames, {{rect, DrawBorder{rect.rect, {1, 1, 1, 1}, {0, 1, 0, 1}, 3}}});
    check(pixel(image, 0, 0)[3] == 0 && pixel(image, 4, 0)[1] == 255 && pixel(image, 4, 4)[1] == 0,
          "Radial fills must retain rounded coverage and inside border order");
    radial.radius.width = 0;
    const UiDrawList invalid{{rect}};
    const auto rejected = frames.capture({0, {8, 8}, 1, {8, 8}, &invalid});
    check(!rejected && has(rejected.diagnostics, "out_of_range", "/commands/0/gradient/radius/width"),
          "Degenerate radial geometry must reject before rendering");
    radial.radius.width = .5f;
    rect.rect.size.width = 0;
    check(pixel(capture(frames, {{rect}}), 0, 0)[3] == 0, "Zero-area radial fills must paint nothing");
}

void box_shadows() {
    CpuFrameCapture frames;
    const DrawShadow shadow{{{6, 6}, {8, 8}}, {0, 0, 0, .5f}, 0, 4};
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto image = capture(frames, {{shadow}}, {20, 20}, scale);
        check(image == capture(frames, {{shadow}}, {20, 20}, scale), "Soft shadow captures must be deterministic");
        const auto y = image.extent.height / 2;
        for (unsigned x = 0; x < image.extent.width; ++x) {
            const double lx = (x + .5) / scale;
            const double distance = std::max({6 - lx, lx - 14, 0.0});
            const double t = std::clamp(distance / 4, 0.0, 1.0);
            const auto alpha = static_cast<std::uint8_t>(std::floor(.5 * (1 - t * t * (3 - 2 * t)) * 255 + .5));
            check(pixel(image, x, y) == std::array<std::uint8_t, 4>{0, 0, 0, alpha},
                  "Shadow alpha must use exact logical distance and finite smoothstep falloff");
        }
        const auto clipped = capture(frames, {{PushClip{{{4, 0}, {12, 20}}}, shadow, PopClip{}}}, {20, 20}, scale);
        for (unsigned x = 0; x < image.extent.width; ++x) {
            const double lx = (x + .5) / scale;
            check(pixel(clipped, x, y) == (lx >= 4 && lx < 16 ? pixel(image, x, y) : std::array<std::uint8_t, 4>{}),
                  "Clipping must not restart or rescale shadow falloff");
        }
        auto rounded = shadow;
        rounded.corner_radius = 4;
        const auto rounded_image = capture(frames, {{rounded}}, {20, 20}, scale);
        rounded.corner_radius = 100;
        check(rounded_image == capture(frames, {{rounded}}, {20, 20}, scale),
              "Shadow caster radius must clamp to half the smaller dimension");
        rounded.softness = 0;
        check(capture(frames, {{rounded}}, {20, 20}, scale) ==
                  capture(frames, {{DrawRect{rounded.rect, rounded.color, rounded.corner_radius}}}, {20, 20}, scale),
              "Zero softness must retain existing hard rounded coverage");
    }
    auto offset = shadow;
    offset.rect.origin.x = 5;
    const auto image = capture(frames, {{offset}}, {20, 20});
    const auto mirrored = capture(frames, {{PushTransform{{-1, 0, 0, 1, 20, 0}}, offset, PopTransform{}}}, {20, 20});
    for (unsigned y = 0; y < 20; ++y)
        for (unsigned x = 0; x < 20; ++x)
            check(pixel(image, x, y) == pixel(mirrored, 19 - x, y),
                  "Transforms must carry the shadow caster and its outer bounds together");
    auto blue = shadow;
    blue.color = {0, 0, 1, .5f};
    const auto blended = capture(frames, {{DrawRect{{{0, 0}, {20, 20}}, {1, 0, 0, 1}}, blue}}, {20, 20});
    check(pixel(blended, 10, 10) == std::array<std::uint8_t, 4>{188, 0, 188, 255},
          "Shadow source-over must blend straight alpha in linear light");
    const auto covered = capture(frames, {{blue, DrawRect{blue.rect, {1, 1, 1, 1}}}}, {20, 20});
    check(pixel(covered, 10, 10) == std::array<std::uint8_t, 4>{255, 255, 255, 255},
          "Later caster fills must cover their shadow");
    auto empty = shadow;
    empty.rect.size.width = 0;
    check(capture(frames, {{empty}}, {20, 20}) == capture(frames, {}, {20, 20}),
          "Zero-area casters must paint no halo");
    auto invalid = shadow;
    invalid.softness = std::numeric_limits<float>::max();
    const UiDrawList invalid_list{{invalid}};
    const auto rejected = frames.capture({0, {20, 20}, 1, {20, 20}, &invalid_list});
    check(!rejected && has(rejected.diagnostics, "geometry_overflow", "/commands/0/rect"),
          "Invalid expanded shadow geometry must reject before capture");
    check(capture(frames, {{offset}}, {20, 20}) == image, "Rejected shadow capture must permit a deterministic retry");
}

void inset_shadows() {
    CpuFrameCapture frames;
    DrawShadow shadow{{{6, 6}, {8, 8}}, {0, 0, 0, .5f}, 0, 2, true, {1, 0}};
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto image = capture(frames, {{shadow}}, {20, 20}, scale);
        check(image == capture(frames, {{shadow}}, {20, 20}, scale), "Inset captures must be deterministic");
        const auto clipped = capture(frames, {{PushClip{{{8, 0}, {4, 20}}}, shadow, PopClip{}}}, {20, 20}, scale);
        const auto y = image.extent.height / 2;
        for (unsigned x = 0; x < image.extent.width; ++x) {
            const double lx = (x + .5) / scale;
            const double distance = std::min(lx - 7, 15 - lx);
            const double t = std::clamp(distance / 2, 0.0, 1.0);
            const auto alpha = static_cast<std::uint8_t>(
                lx >= 6 && lx < 14 ? std::floor(.5 * (1 - t * t * (3 - 2 * t)) * 255 + .5) : 0);
            check(pixel(image, x, y) == std::array<std::uint8_t, 4>{0, 0, 0, alpha},
                  "Inset falloff must use negative cutout distance and never paint outside the box");
            check(pixel(clipped, x, y) == (lx >= 8 && lx < 12 ? pixel(image, x, y) : std::array<std::uint8_t, 4>{}),
                  "Ancestor clips must trim inset coverage without moving the cutout");
        }
        auto hard = shadow;
        hard.softness = 0;
        check(capture(frames, {{hard}}, {20, 20}, scale) ==
                  capture(frames, {{DrawRect{{{6, 6}, {1, 8}}, hard.color}}}, {20, 20}, scale),
              "Hard inset shadows must paint only outside the translated cutout");
        hard.offset = {};
        check(capture(frames, {{hard}}, {20, 20}, scale) == capture(frames, {}, {20, 20}, scale),
              "Unshifted zero-softness inset shadows must paint nothing");
        auto rounded = shadow;
        rounded.corner_radius = 4;
        const auto round_image = capture(frames, {{rounded}}, {20, 20}, scale);
        rounded.corner_radius = 100;
        check(round_image == capture(frames, {{rounded}}, {20, 20}, scale),
              "Both inset clipping and cutout radii must clamp");
        rounded.offset = {100, -100};
        check(capture(frames, {{rounded}}, {20, 20}, scale) ==
                  capture(frames, {{DrawRect{rounded.rect, rounded.color, 4}}}, {20, 20}, scale),
              "Distant cutouts must saturate to the rounded box, not leak outside it");
        rounded.rect.size.width = 0;
        check(capture(frames, {{rounded}}, {20, 20}, scale) == capture(frames, {}, {20, 20}, scale),
              "Empty inset clipping boxes must paint nothing");
    }
    const auto image = capture(frames, {{shadow}}, {20, 20});
    const auto mirrored = capture(frames, {{PushTransform{{-1, 0, 0, 1, 20, 0}}, shadow, PopTransform{}}}, {20, 20});
    for (unsigned y = 0; y < 20; ++y)
        for (unsigned x = 0; x < 20; ++x)
            check(pixel(image, x, y) == pixel(mirrored, 19 - x, y),
                  "Mirrors must transform inset bounds and cutout direction together");
    shadow.color = {0, 0, 1, .5f};
    const auto blended = capture(frames, {{DrawRect{shadow.rect, {1, 0, 0, 1}}, shadow}}, {20, 20});
    check(pixel(blended, 6, 10) == std::array<std::uint8_t, 4>{188, 0, 188, 255} &&
              pixel(blended, 10, 10) == std::array<std::uint8_t, 4>{255, 0, 0, 255},
          "Inset shadows must blend over the fill in linear light while preserving its unaffected center");
}

void images_and_lifetime() {
    CpuFrameCapture frames;
    std::vector<std::uint8_t> texels{0, 0, 0, 255, 255, 255, 255, 255};
    check(frames.bind_image({7}, {{2, 1}, texels}).empty(), "Bind failed");
    texels.assign(8, 0); // Binding owns its copy.
    auto image = capture(frames, {{DrawImage{{{0, 0}, {1, 1}}, {7}}}});
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{188, 188, 188, 255},
          "sRGB decode must precede bilinear sampling");
    image = capture(frames, {{DrawImage{{{0, 0}, {1, 1}}, {7}, {{0.5f, 0}, {0.5f, 1}}, {1, 0, 0, 0.5f}}}});
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{188, 0, 0, 128}, "Crop/tint/alpha differs");
    texels = {128, 128, 128, 255};
    check(frames.bind_image({8}, {{1, 1}, texels, CpuImageEncoding::rgba8_unorm}).empty(), "UNORM bind failed");
    image = capture(frames, {{DrawImage{{{0, 0}, {1, 1}}, {8}}}});
    check(pixel(image, 0, 0)[0] == 188, "Linear image data was decoded as sRGB");
    const std::vector<std::uint8_t> premultiplied{64, 32, 0, 128};
    check(frames.bind_image({9}, {{1, 1}, premultiplied, CpuImageEncoding::rgba8_unorm, ImageAlpha::premultiplied})
              .empty(),
          "Premultiplied bind failed");
    image = capture(frames, {{DrawRect{{{0, 0}, {1, 1}}, {1, 1, 1, 1}}, DrawImage{{{0, 0}, {1, 1}}, {9}}}});
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{224, 207, 187, 255},
          "Premultiplied RGB was weighted by alpha again");
    image = capture(frames, {{DrawImage{{{0, 0}, {1, 1}}, {9}, {{0, 0}, {1, 1}}, {1, 1, 1, 0.5f}}}});
    check(pixel(image, 0, 0) == std::array<std::uint8_t, 4>{99, 71, 0, 64},
          "Tint alpha did not scale premultiplied RGB");
    CpuRenderer renderer;
    std::vector<std::uint8_t> target(8 * 8 * 4, 0);
    renderer.set_target({{8, 8}, target});
    check(renderer.bind_image({1}, {{1, 1}, texels}).empty(), "Renderer bind failed");
    UiDrawList clipped{{PushClip{{{0, 0}, {0, 0}}}, DrawImage{{{0, 0}, {8, 8}}, {1}}, PopClip{}}};
    renderer.retire(100); // Cannot retire a future referencing frame.
    check(renderer.submit({1, {8, 8}, 1}, clipped).empty(), "Clipped image rejected");
    check(has(renderer.unbind_image({1}), "resource_in_use", "/image") &&
              has(renderer.bind_image({1}, {{1, 1}, texels}), "resource_in_use", "/image"),
          "Clipped resource did not pin through retirement");
    renderer.retire(1);
    check(renderer.unbind_image({1}).empty(), "Completed image remained pinned");
    for (std::uint64_t i = 1; i <= 64; ++i)
        check(renderer.bind_image({i}, {{1, 1}, texels}).empty(), "Image capacity too small");
    check(has(renderer.bind_image({65}, {{1, 1}, texels}), "image_limit", "/image"), "Image limit ignored");
}
void rejection_is_atomic() {
    CpuRenderer renderer;
    std::vector<std::uint8_t> target(8 * 8 * 4, 17), texture(4, 255);
    renderer.set_target({{8, 8}, target});
    renderer.bind_image({1}, {{1, 1}, texture});
    const auto original = target;
    const DrawRect fill{{{0, 0}, {8, 8}}, {1, 1, 1, 1}};
    check(has(renderer.submit({1, {8, 8}, 1}, {{fill, DrawImage{{{0, 0}, {1, 1}}, {2}}}}), "invalid_handle",
              "/commands/1/image"),
          "Missing image accepted");
    check(target == original && renderer.unbind_image({1}).empty(), "Rejected frame changed pixels or resource use");
    check(has(renderer.submit({1, {8, 8}, 1}, {{fill, PopClip{}}}), "unbalanced_stack", "/commands/1"),
          "Unbalanced stack accepted");
    check(has(renderer.submit({1, {8, 8}, 1},
                              {{fill, PushTransform{{1e30f, 0, 0, 1e30f, 0, 0}},
                                PushTransform{{1e30f, 0, 0, 1e30f, 0, 0}}, PopTransform{}, PopTransform{}}}),
              "geometry_overflow", "/commands/2/transform"),
          "Composed overflow accepted");
    check(has(renderer.submit({1, {8, 8}, 1}, {{fill, PushTransform{{1, 0.5f, 0, 1, 0, 0}}, PushClip{{{0, 0}, {1, 1}}},
                                                PopClip{}, PopTransform{}}}),
              "unsupported_clip", "/commands/2"),
          "Skewed clip accepted");
    check(target == original, "Late rejection modified target");
    check(renderer.submit({1, {8, 8}, 1}, {{fill}}).empty(), "Rejection consumed frame number");
    check(has(renderer.submit({1, {8, 8}, 1}, {}), "stale_frame", "/frame"), "Stale submission accepted");
    check(has(renderer.submit({2, {9, 8}, 1}, {}), "extent_mismatch", "/target/extent"), "Extent mismatch accepted");
    check(has(renderer.bind_image({0}, {{1, 1}, texture}), "invalid_handle", "/image"), "Zero image handle accepted");
    check(has(renderer.bind_image({1}, {{2, 2}, texture}), "invalid_image", "/image/pixels"),
          "Wrong image byte count accepted");
    check(has(renderer.bind_image({1}, {{1, 1}, texture, static_cast<CpuImageEncoding>(9)}), "unknown_value",
              "/image/encoding"),
          "Unknown encoding accepted");
    check(has(renderer.bind_image({1}, {{1, 1}, texture, CpuImageEncoding::rgba8_srgb, static_cast<ImageAlpha>(9)}),
              "unknown_value", "/image/alpha"),
          "Unknown image alpha accepted");
    renderer.bind_image({1}, {{1, 1}, texture});
    check(has(renderer.submit({2, {8, 8}, 1}, {{fill, DrawImage{{{0, 0}, {0, 0}}, {1}, {{0.75f, 0}, {0.5f, 1}}}}}),
              "out_of_range", "/commands/1/source"),
          "Clipped or zero-area source crop bypassed validation");
    renderer.retire(1);
    check(renderer.unbind_image({1}).empty(), "Rejected image crop pinned a resource");
    CpuFrameCapture frames;
    UiDrawList empty;
    check(has(frames.capture({0, {1, 1}, 1, {1, 1}, nullptr}).diagnostics, "missing_input", "/paint"),
          "Null paint accepted");
    check(has(frames.capture({0, {1, 1}, 1, {2, 2}, &empty}).diagnostics, "extent_mismatch", "/extent"),
          "Capture extent mismatch accepted");
    check(
        has(frames.capture({0, {100000, 100000}, 1, {100000, 100000}, &empty}).diagnostics, "capture_limit", "/extent"),
        "Capture allocation unbounded");
}
struct Rasterizer : GlyphRasterizer {
    std::uint64_t face_revision(FontId) const override { return 1; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& r) override {
        if (r.glyph == 99) return {{}, {{"fixture_failure", Severity::error, "/glyph", "Injected failure.", {}}}};
        return {GlyphBitmap{2, 2, 0, 2, {255, 128, 64, 0}}, {}};
    }
};
void glyphs_and_runner() {
    Rasterizer rasterizer;
    GlyphCache cache(rasterizer);
    CpuFrameCapture frames(&cache);
    GlyphRun run{{0}, 16, {{1, 0, {1, 3}, {0}}}, {}};
    UiDrawList list{{DrawGlyphRun{{0, 0}, run, {1, 1, 1, 1}}}};
    auto image = capture(frames, list);
    check(pixel(image, 1, 1) == std::array<std::uint8_t, 4>{255, 255, 255, 255} && pixel(image, 2, 1)[3] == 128 &&
              pixel(image, 1, 2)[3] == 64 && pixel(image, 2, 2)[3] == 0,
          "Glyph baseline or linear coverage differs");
    check(capture(frames, list) == image, "Repeated glyph capture differs");
    CpuFrameCapture no_cache;
    check(has(no_cache.capture({0, {8, 8}, 1, {8, 8}, &list}).diagnostics, "unsupported_command", "/commands/0"),
          "Glyph without cache silently substituted");
    // Explicit host image handles collide with the first atlas range; preserve them.
    std::array<std::uint8_t, 4> red{255, 0, 0, 255};
    frames.bind_image({1}, {{1, 1}, red});
    list.commands.push_back(DrawImage{{{5, 5}, {1, 1}}, {1}});
    check(pixel(capture(frames, list), 5, 5)[0] == 255, "Atlas overwrote host image");
    std::get<DrawGlyphRun>(list.commands[0]).run.glyphs[0].id = 99;
    auto failed = frames.capture({0, {8, 8}, 1, {8, 8}, &list});
    check(!failed && has(failed.diagnostics, "fixture_failure", "/commands/0/run/glyphs/0/glyph"),
          "Glyph failure location lost");
    check(frames.unbind_image({1}).empty(), "Capture did not retire host images");
    ReplayRecording recording;
    recording.viewport = {12, 12};
    recording.styles.resize(1);
    recording.styles[0].background = {1, 0, 0, 1};
    recording.steps = {ReplayResize{{16, 10}}, ReplayScale{1.25f}};
    PlaceholderTextShaper text;
    auto result = run_offscreen({&recording, &text, frames.backend(), &frames});
    check(result && result.value->frames.size() == 3 && result.value->frames[2].extent == CaptureExtent{20, 13},
          "CPU offscreen runner failed");
    check(result.value == run_offscreen({&recording, &text, frames.backend(), &frames}).value,
          "CPU runner nondeterministic");
    struct Reentrant : Rasterizer {
        CpuFrameCapture* frames = nullptr;
        Result<GlyphBitmap> rasterize(const GlyphRasterRequest& r) override {
            UiDrawList empty;
            check(has(frames->capture({0, {1, 1}, 1, {1, 1}, &empty}).diagnostics, "reentrant_capture", "/capture"),
                  "Reentrant capture accepted");
            std::array<std::uint8_t, 4> bytes{};
            check(has(frames->bind_image({1}, {{1, 1}, bytes}), "reentrant_capture", "/capture"),
                  "Binding mutated during raster callback");
            return Rasterizer::rasterize(r);
        }
    } reentrant;
    GlyphCache reentrant_cache(reentrant);
    CpuFrameCapture guarded(&reentrant_cache);
    reentrant.frames = &guarded;
    list.commands.resize(1);
    std::get<DrawGlyphRun>(list.commands[0]).run.glyphs[0].id = 1;
    check(capture(guarded, list) == image, "Rejected reentry damaged outer capture");
    struct Large : Rasterizer {
        Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) override {
            return {GlyphBitmap{254, 254, 0, 0, std::vector<std::uint8_t>(254 * 254, 255)}, {}};
        }
    } large;
    GlyphCache large_cache(large);
    CpuFrameCapture bounded(&large_cache);
    for (unsigned i = 1; i <= 63; ++i) check(bounded.bind_image({i}, {{1, 1}, red}).empty(), "Capacity setup failed");
    run.glyphs.push_back({2, 1, {1, 3}, {0}});
    UiDrawList pages{{DrawGlyphRun{{0, 0}, run, {1, 1, 1, 1}}}};
    auto rejected = bounded.capture({0, {8, 8}, 1, {8, 8}, &pages});
    check(!rejected && has(rejected.diagnostics, "image_limit", "/image"), "Atlas pages bypassed shared image limit");
    check(bounded.unbind_image({63}).empty(), "Failure pinned host resources");
    check(!capture(bounded, pages).pixels.empty(), "Rejected partial atlas leaked a temporary binding");
}
}
int main() {
    try {
        pixels_and_geometry();
        strokes();
        analytic_fills();
        procedural_fallbacks();
        linear_gradients();
        multi_stop_gradients();
        radial_gradients();
        box_shadows();
        inset_shadows();
        images_and_lifetime();
        rejection_is_atomic();
        glyphs_and_runner();
        std::cout << "CPU scalar pixels, resources, rejection, glyph capture and replay checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
