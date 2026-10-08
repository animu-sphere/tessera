#pragma once

#include <tessera/render/draw_list.hpp>
#include <tessera/text/glyph_cache.hpp>

namespace tessera {

struct GlyphAtlasLimits {
    std::uint32_t page_size = 256; // Square physical pixels, inclusive [4, 2048].
    std::uint32_t pages = 16;      // Inclusive [1, 64].
    std::size_t glyphs = 4096;     // Maximum glyph occurrences, including whitespace.
};

// Immutable after preparation. Upload as white RGB with coverage in straight alpha,
// one mip level, linear filtering and clamp-to-edge. One transparent texel surrounds ink.
struct GlyphAtlasPage {
    ImageHandle image;
    std::uint32_t size = 0;
    std::vector<std::uint8_t> coverage;
    bool operator==(const GlyphAtlasPage&) const = default;
};
struct GlyphAtlasFrame {
    UiDrawList draw_list;
    std::vector<GlyphAtlasPage> pages;
    bool operator==(const GlyphAtlasFrame&) const = default;
};

// Frame-local shelf packing, independent of any backend. Replaces each DrawGlyphRun
// with DrawImages, preserving order, clips, transforms, baselines and tint. Empty
// bitmaps draw nothing. The host supplies a nonzero consecutive handle range unused
// by the source list and binds/uploads pages before submission. Do not replace faces
// during preparation; reshape old runs after face replacement. No baseline snapping.
// Output owns all bytes and can outlive cache/font services; GPU copies and bindings
// must remain alive through their last referencing frame's completion and retirement.
Result<GlyphAtlasFrame> prepare_glyph_atlas(const UiDrawList&, GlyphCache&,
    float device_scale, ImageHandle first_image, GlyphAtlasLimits = {});

} // namespace tessera
