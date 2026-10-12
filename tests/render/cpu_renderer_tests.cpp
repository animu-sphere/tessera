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
std::array<std::uint8_t,4> pixel(const CapturedImage& image, unsigned x, unsigned y) {
    const auto i=(std::size_t(y)*image.extent.width+x)*4;
    return {image.pixels[i],image.pixels[i+1],image.pixels[i+2],image.pixels[i+3]};
}
CapturedImage capture(CpuFrameCapture& renderer, const UiDrawList& list, Size size={8,8}, float scale=1) {
    auto extent=capture_extent(size,scale);
    auto result=renderer.capture({0,size,scale,*extent.value,&list});
    if (!result) for (const auto& d:result.diagnostics) std::cerr<<d.code<<' '<<d.path<<' '<<d.message<<'\n';
    check(bool(result),"CPU capture rejected valid fixture"); return std::move(*result.value);
}
void pixels_and_geometry() {
    CpuFrameCapture frames;
    UiDrawList list{{DrawRect{{{0,0},{8,8}},{1,0,0,1}},DrawRect{{{2,2},{4,4}},{0,0,1,0.5f}}}};
    auto image=capture(frames,list);
    check(pixel(image,0,0)==std::array<std::uint8_t,4>{255,0,0,255},"Fill pixels differ");
    check(pixel(image,3,3)==std::array<std::uint8_t,4>{188,0,188,255},"Source-over must blend in linear light");
    check(capture(frames,list)==image,"Repeated CPU rendering differs");
    image=capture(frames,{{DrawRect{{{0,0},{8,8}},{1,1,1,1},4}}});
    check(pixel(image,0,0)[3]==0 && pixel(image,4,0)[3]==255,"Circular rounded corner coverage differs");
    image=capture(frames,{{DrawBorder{{{0,0},{8,8}},{1,2,3,1},{0,1,0,1},0}}});
    check(pixel(image,0,3)[1]==255 && pixel(image,5,3)[3]==0 && pixel(image,6,3)[1]==255 &&
          pixel(image,4,4)[3]==0 && pixel(image,4,5)[1]==255,"Unequal inside border widths differ");
    image=capture(frames,{{DrawBorder{{{0,0},{8,8}},{5,5,5,5},{1,1,1,1},4}}});
    check(pixel(image,4,4)[3]==255 && pixel(image,0,0)[3]==0,"Collapsed inner border or rounded edge differs");
    list={{PushClip{{{0.6f,0},{2,8}}},PushTransform{{-1,0,0,1,8,0}},
        PushClip{{{5.4f,0},{2,8}}},DrawRect{{{0,0},{8,8}},{1,1,1,1}},PopClip{},PopTransform{},PopClip{}}};
    image=capture(frames,list);
    check(pixel(image,0,3)[3]==0 && pixel(image,1,3)[3]==255 && pixel(image,2,3)[3]==255 && pixel(image,3,3)[3]==0,
        "Mirrored nested fractional clips disagree with pixel centers");
    image=capture(frames,{{PushTransform{{1,0,0,1,3,0}},PushTransform{{2,0,0,1,0,2}},
        DrawRect{{{0,0},{1,1}},{1,1,1,1}},PopTransform{},PopTransform{},DrawRect{{{0,0},{1,1}},{1,0,0,1}}}});
    check(pixel(image,3,2)[3]==255 && pixel(image,4,2)[3]==255 && pixel(image,0,0)[0]==255 && pixel(image,2,2)[3]==0,
        "Transform composition/restoration differs");
    image=capture(frames,{{PushTransform{{0,1,-1,0,4,0}},DrawRect{{{0,0},{2,1}},{1,1,1,1}},PopTransform{}}});
    check(pixel(image,3,0)[3]==255 && pixel(image,3,1)[3]==255 && pixel(image,4,0)[3]==0,"Rotated primitive coverage differs");
    image=capture(frames,{{PushTransform{{0,1,-1,0,4,0}},DrawRect{{{0,0},{2,2}},{1,1,1,1}},PopTransform{}}},{8,8},1.25f);
    check(pixel(image,2,0)[3]==255 && pixel(image,5,0)[3]==0,"Rotated edge at pixel center violates framebuffer top/left rule");
    image=capture(frames,{{PushTransform{{-1,0,0,1,4,0}},DrawRect{{{0,0},{2,2}},{1,1,1,1}},PopTransform{}}},{8,8},1.25f);
    check(pixel(image,2,0)[3]==255 && pixel(image,5,0)[3]==0,"Mirrored edge at pixel center violates framebuffer top/left rule");
    image=capture(frames,{{PushTransform{{1,0,0.5f,1,0,0}},DrawRect{{{0,0},{2,2}},{1,1,1,1}},PopTransform{}}});
    check(pixel(image,0,0)[3]==255 && pixel(image,0,1)[3]==0 && pixel(image,2,1)[3]==255,"Skewed primitive coverage differs");
    image=capture(frames,{{DrawRect{{{0,0},{1,1}},{1,1,1,1}}}},{2.1f,2.1f},1.5f);
    check(image.extent==CaptureExtent{4,4} && pixel(image,0,0)[3]==255 && pixel(image,1,0)[3]==0,"Fractional scale/half-open edges differ");
}
void images_and_lifetime() {
    CpuFrameCapture frames;
    std::vector<std::uint8_t> texels{0,0,0,255,255,255,255,255};
    check(frames.bind_image({7},{{2,1},texels}).empty(),"Bind failed");
    texels.assign(8,0); // Binding owns its copy.
    auto image=capture(frames,{{DrawImage{{{0,0},{1,1}},{7}}}});
    check(pixel(image,0,0)==std::array<std::uint8_t,4>{188,188,188,255},"sRGB decode must precede bilinear sampling");
    image=capture(frames,{{DrawImage{{{0,0},{1,1}},{7},{{0.5f,0},{0.5f,1}},{1,0,0,0.5f}}}});
    check(pixel(image,0,0)==std::array<std::uint8_t,4>{188,0,0,128},"Crop/tint/alpha differs");
    texels={128,128,128,255};
    check(frames.bind_image({8},{{1,1},texels,CpuImageEncoding::rgba8_unorm}).empty(),"UNORM bind failed");
    image=capture(frames,{{DrawImage{{{0,0},{1,1}},{8}}}});
    check(pixel(image,0,0)[0]==188,"Linear image data was decoded as sRGB");
    CpuRenderer renderer;
    std::vector<std::uint8_t> target(8*8*4,0);
    renderer.set_target({{8,8},target});
    check(renderer.bind_image({1},{{1,1},texels}).empty(),"Renderer bind failed");
    UiDrawList clipped{{PushClip{{{0,0},{0,0}}},DrawImage{{{0,0},{8,8}},{1}},PopClip{}}};
    renderer.retire(100); // Cannot retire a future referencing frame.
    check(renderer.submit({1,{8,8},1},clipped).empty(),"Clipped image rejected");
    check(has(renderer.unbind_image({1}),"resource_in_use","/image") &&
          has(renderer.bind_image({1},{{1,1},texels}),"resource_in_use","/image"),"Clipped resource did not pin through retirement");
    renderer.retire(1); check(renderer.unbind_image({1}).empty(),"Completed image remained pinned");
    for (std::uint64_t i=1;i<=64;++i) check(renderer.bind_image({i},{{1,1},texels}).empty(),"Image capacity too small");
    check(has(renderer.bind_image({65},{{1,1},texels}),"image_limit","/image"),"Image limit ignored");
}
void rejection_is_atomic() {
    CpuRenderer renderer;
    std::vector<std::uint8_t> target(8*8*4,17), texture(4,255);
    renderer.set_target({{8,8},target}); renderer.bind_image({1},{{1,1},texture});
    const auto original=target;
    const DrawRect fill{{{0,0},{8,8}},{1,1,1,1}};
    check(has(renderer.submit({1,{8,8},1},{{fill,DrawImage{{{0,0},{1,1}},{2}}}}),"invalid_handle","/commands/1/image"),"Missing image accepted");
    check(target==original && renderer.unbind_image({1}).empty(),"Rejected frame changed pixels or resource use");
    check(has(renderer.submit({1,{8,8},1},{{fill,PopClip{}}}),"unbalanced_stack","/commands/1"),"Unbalanced stack accepted");
    check(has(renderer.submit({1,{8,8},1},{{fill,PushTransform{{1e30f,0,0,1e30f,0,0}},PushTransform{{1e30f,0,0,1e30f,0,0}},PopTransform{},PopTransform{}}}),
        "geometry_overflow","/commands/2/transform"),"Composed overflow accepted");
    check(has(renderer.submit({1,{8,8},1},{{fill,PushTransform{{1,0.5f,0,1,0,0}},PushClip{{{0,0},{1,1}}},PopClip{},PopTransform{}}}),
        "unsupported_clip","/commands/2"),"Skewed clip accepted");
    check(target==original,"Late rejection modified target");
    check(renderer.submit({1,{8,8},1},{{fill}}).empty(),"Rejection consumed frame number");
    check(has(renderer.submit({1,{8,8},1},{}),"stale_frame","/frame"),"Stale submission accepted");
    check(has(renderer.submit({2,{9,8},1},{}),"extent_mismatch","/target/extent"),"Extent mismatch accepted");
    check(has(renderer.bind_image({0},{{1,1},texture}),"invalid_handle","/image"),"Zero image handle accepted");
    check(has(renderer.bind_image({1},{{2,2},texture}),"invalid_image","/image/pixels"),"Wrong image byte count accepted");
    check(has(renderer.bind_image({1},{{1,1},texture,static_cast<CpuImageEncoding>(9)}),"unknown_value","/image/encoding"),"Unknown encoding accepted");
    renderer.bind_image({1},{{1,1},texture});
    check(has(renderer.submit({2,{8,8},1},{{fill,DrawImage{{{0,0},{0,0}},{1},{{0.75f,0},{0.5f,1}}}}}),
        "out_of_range","/commands/1/source"),"Clipped or zero-area source crop bypassed validation");
    renderer.retire(1);
    check(renderer.unbind_image({1}).empty(),"Rejected image crop pinned a resource");
    CpuFrameCapture frames;
    UiDrawList empty;
    check(has(frames.capture({0,{1,1},1,{1,1},nullptr}).diagnostics,"missing_input","/paint"),"Null paint accepted");
    check(has(frames.capture({0,{1,1},1,{2,2},&empty}).diagnostics,"extent_mismatch","/extent"),"Capture extent mismatch accepted");
    check(has(frames.capture({0,{100000,100000},1,{100000,100000},&empty}).diagnostics,"capture_limit","/extent"),"Capture allocation unbounded");
}
struct Rasterizer : GlyphRasterizer {
    std::uint64_t face_revision(FontId) const override { return 1; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& r) override {
        if (r.glyph==99) return {{},{{"fixture_failure",Severity::error,"/glyph","Injected failure.",{}}}};
        return {GlyphBitmap{2,2,0,2,{255,128,64,0}},{}};
    }
};
void glyphs_and_runner() {
    Rasterizer rasterizer; GlyphCache cache(rasterizer); CpuFrameCapture frames(&cache);
    GlyphRun run{{0},16,{{1,0,{1,3},{0}}},{}};
    UiDrawList list{{DrawGlyphRun{{0,0},run,{1,1,1,1}}}};
    auto image=capture(frames,list);
    check(pixel(image,1,1)==std::array<std::uint8_t,4>{255,255,255,255} && pixel(image,2,1)[3]==128 &&
          pixel(image,1,2)[3]==64 && pixel(image,2,2)[3]==0,"Glyph baseline or linear coverage differs");
    check(capture(frames,list)==image,"Repeated glyph capture differs");
    CpuFrameCapture no_cache;
    check(has(no_cache.capture({0,{8,8},1,{8,8},&list}).diagnostics,"unsupported_command","/commands/0"),"Glyph without cache silently substituted");
    // Explicit host image handles collide with the first atlas range; preserve them.
    std::array<std::uint8_t,4> red{255,0,0,255}; frames.bind_image({1},{{1,1},red});
    list.commands.push_back(DrawImage{{{5,5},{1,1}},{1}});
    check(pixel(capture(frames,list),5,5)[0]==255,"Atlas overwrote host image");
    std::get<DrawGlyphRun>(list.commands[0]).run.glyphs[0].id=99;
    auto failed=frames.capture({0,{8,8},1,{8,8},&list});
    check(!failed && has(failed.diagnostics,"fixture_failure","/commands/0/run/glyphs/0/glyph"),"Glyph failure location lost");
    check(frames.unbind_image({1}).empty(),"Capture did not retire host images");
    ReplayRecording recording;
    recording.viewport={12,12}; recording.styles.resize(1); recording.styles[0].background={1,0,0,1};
    recording.steps={ReplayResize{{16,10}},ReplayScale{1.25f}};
    PlaceholderTextShaper text;
    auto result=run_offscreen({&recording,&text,frames.backend(),&frames});
    check(result && result.value->frames.size()==3 && result.value->frames[2].extent==CaptureExtent{20,13},"CPU offscreen runner failed");
    check(result.value==run_offscreen({&recording,&text,frames.backend(),&frames}).value,"CPU runner nondeterministic");
    struct Reentrant : Rasterizer {
        CpuFrameCapture* frames=nullptr;
        Result<GlyphBitmap> rasterize(const GlyphRasterRequest& r) override {
            UiDrawList empty;
            check(has(frames->capture({0,{1,1},1,{1,1},&empty}).diagnostics,"reentrant_capture","/capture"),"Reentrant capture accepted");
            std::array<std::uint8_t,4> bytes{};
            check(has(frames->bind_image({1},{{1,1},bytes}),"reentrant_capture","/capture"),"Binding mutated during raster callback");
            return Rasterizer::rasterize(r);
        }
    } reentrant;
    GlyphCache reentrant_cache(reentrant); CpuFrameCapture guarded(&reentrant_cache); reentrant.frames=&guarded;
    list.commands.resize(1); std::get<DrawGlyphRun>(list.commands[0]).run.glyphs[0].id=1;
    check(capture(guarded,list)==image,"Rejected reentry damaged outer capture");
    struct Large : Rasterizer {
        Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) override {
            return {GlyphBitmap{254,254,0,0,std::vector<std::uint8_t>(254*254,255)},{}};
        }
    } large;
    GlyphCache large_cache(large); CpuFrameCapture bounded(&large_cache);
    for (unsigned i=1;i<=63;++i) check(bounded.bind_image({i},{{1,1},red}).empty(),"Capacity setup failed");
    run.glyphs.push_back({2,1,{1,3},{0}});
    UiDrawList pages{{DrawGlyphRun{{0,0},run,{1,1,1,1}}}};
    auto rejected=bounded.capture({0,{8,8},1,{8,8},&pages});
    check(!rejected && has(rejected.diagnostics,"image_limit","/image"),"Atlas pages bypassed shared image limit");
    check(bounded.unbind_image({63}).empty(),"Failure pinned host resources");
    check(!capture(bounded,pages).pixels.empty(),"Rejected partial atlas leaked a temporary binding");
}
}
int main() {
    try { pixels_and_geometry(); images_and_lifetime(); rejection_is_atomic(); glyphs_and_runner();
        std::cout<<"CPU scalar pixels, resources, rejection, glyph capture and replay checks passed.\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
