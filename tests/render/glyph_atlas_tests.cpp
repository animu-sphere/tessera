#include <tessera/render/glyph_atlas.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
namespace {
struct Rasterizer : GlyphRasterizer {
    unsigned calls = 0;
    std::uint64_t revision = 1;
    std::uint64_t face_revision(FontId) const override { return revision; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        ++calls;
        if (request.glyph == 99) return {{}, {{"fixture_failure", Severity::error, "/glyph", "Injected failure", {}}}};
        if (request.glyph == 0) return {GlyphBitmap{}, {}};
        return {GlyphBitmap{2, 2, -1, 3, {std::uint8_t(revision), 64, 128, 255}}, {}};
    }
};
UiDrawList source() {
    GlyphRun run{{0}, 20, {{1,0,{2,10},{0}}, {0,1,{6,10},{0}}, {1,2,{8,10},{0}}, {1,3,{12,10},{1}}}, {}};
    return {{PushClip{{{0,0},{40,40}}}, PushTransform{{1,0,0,1,1,2}},
        DrawGlyphRun{{3,4}, run, {0.5f,1,1,0.25f}}, PopTransform{}, PopClip{},
        DrawImage{{{0,0},{1,1}},{7}}}};
}
void packing_and_lifetime() {
    Rasterizer rasterizer;
    GlyphCache cache(rasterizer, {0,0}); // Even without cache storage, placements deduplicate.
    const auto list = source();
    const auto prepared = prepare_glyph_atlas(list, cache, 1.25f, {10}, {8,2,16});
    check(prepared && prepared.diagnostics.empty(), "Atlas preparation failed");
    check(rasterizer.calls == 3, "Atlas failed to deduplicate font/glyph/size keys or empty glyphs");
    const auto saved = *prepared.value;
    check(saved.pages.size() == 1 && saved.pages[0].image.value == 10, "Unexpected page assignment");
    check(saved.draw_list.commands.size() == 8 && saved.draw_list.commands[0] == list.commands[0] &&
        saved.draw_list.commands[1] == list.commands[1] && saved.draw_list.commands.back() == list.commands.back(), "Ordering/stacks/images changed");
    const auto& a = std::get<DrawImage>(saved.draw_list.commands[2]);
    const auto& b = std::get<DrawImage>(saved.draw_list.commands[3]);
    const auto& c = std::get<DrawImage>(saved.draw_list.commands[4]);
    check(std::abs(a.rect.origin.x - 4.2f) < 1e-6f && std::abs(a.rect.origin.y - 11.6f) < 1e-6f &&
        a.rect.size == Size{1.6f,1.6f}, "Bearings/baseline/fractional scale disagree");
    check(a.source == b.source && c.source != a.source && a.tint == Color{0.5f,1,1,0.25f}, "Dedup/font identity/tint lost");
    const auto& pixels = saved.pages[0].coverage;
    check(pixels.size() == 64 && pixels[9] == 1 && pixels[10] == 64 && pixels[17] == 128 && pixels[18] == 255,
        "Coverage bytes changed during packing");
    for (unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x)
        if (!((y==1 || y==2) && ((x==1 || x==2) || (x==5 || x==6))))
            check(pixels[y*8+x] == 0, "Transparent gutter or unused area contains ink");
    check(*prepare_glyph_atlas(list, cache, 1.25f, {10}, {8,2,16}).value == saved, "Atlas nondeterministic");
    rasterizer.revision = 2; cache.clear();
    const auto replacement = prepare_glyph_atlas(list, cache, 1.25f, {20}, {4,2,16});
    check(replacement && replacement.value->pages.size() == 2 && replacement.value->pages[0].coverage[5] == 2 &&
        saved.pages[0].coverage[9] == 1, "Replacement altered retained frame or page overflow failed");
}
void failures() {
    Rasterizer rasterizer; GlyphCache cache(rasterizer);
    auto list = source();
    check(has(prepare_glyph_atlas(list, cache, 0, {10}).diagnostics, "out_of_range", "/device_scale"), "Invalid scale accepted");
    check(has(prepare_glyph_atlas(list, cache, 1, {0}).diagnostics, "invalid_handle", "/first_image"), "Zero handle accepted");
    check(has(prepare_glyph_atlas(list, cache, 1, {UINT64_MAX}).diagnostics, "invalid_handle", "/first_image"), "Handle overflow accepted");
    check(has(prepare_glyph_atlas(list, cache, 1, {7}).diagnostics, "image_collision", "/commands/5/image"), "Source image collision accepted");
    check(has(prepare_glyph_atlas(list, cache, 1, {10}, {3,1,16}).diagnostics, "out_of_range", "/limits"), "Invalid page size accepted");
    check(has(prepare_glyph_atlas(list, cache, 1, {10}, {8,1,3}).diagnostics, "glyph_limit", "/commands/2/run/glyphs"), "Occurrence limit ignored");
    check(rasterizer.calls == 0, "Invalid input called rasterizer");
    auto full = prepare_glyph_atlas(list, cache, 1, {10}, {4,1,16});
    check(!full && has(full.diagnostics, "glyph_atlas_full", "/commands/2/run/glyphs/3"), "Partial atlas returned after exhaustion");
    auto& run = std::get<DrawGlyphRun>(list.commands[2]);
    run.run.glyphs[1].id = 99;
    auto bad = prepare_glyph_atlas(list, cache, 1, {10});
    check(!bad && has(bad.diagnostics, "fixture_failure", "/commands/2/run/glyphs/1/glyph"), "Raster failure lost source location");
    run.run.size = 0.9f;
    check(has(prepare_glyph_atlas(list, cache, 1, {10}).diagnostics, "out_of_range", "/commands/2/run/size"), "Subpixel em size accepted");
    run.run.size = 513;
    check(has(prepare_glyph_atlas(list, cache, 1, {10}).diagnostics, "out_of_range", "/commands/2/run/size"), "Oversize em accepted");
    check(prepare_glyph_atlas({}, cache, 1, {10}).value->pages.empty(), "Empty list allocated a page");
    run.run.size = 20; run.run.glyphs.resize(1); run.run.glyphs[0].id = 0;
    check(prepare_glyph_atlas(list, cache, 1, {10}).value->pages.empty(), "Whitespace allocated atlas storage");
    run.run.glyphs[0].id = 1;
    run.origin.x = std::numeric_limits<float>::max();
    run.run.glyphs[0].position.x = std::numeric_limits<float>::max();
    const auto overflow = prepare_glyph_atlas(list, cache, 1, {10});
    check(!overflow && has(overflow.diagnostics, "geometry_overflow", "/commands/2/run/glyphs/0/position"), "Overflowing baseline accepted");
    struct WideRasterizer : Rasterizer {
        Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) override { return {GlyphBitmap{3,1,0,0,{1,2,3}}, {}}; }
    } wide;
    GlyphCache wide_cache(wide);
    const auto oversize = prepare_glyph_atlas(source(), wide_cache, 1, {10}, {4,1,16});
    check(!oversize && has(oversize.diagnostics, "glyph_atlas_size", "/commands/2/run/glyphs/0"), "Oversize bitmap accepted");
}
}
int main() {
    try { packing_and_lifetime(); failures(); }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
