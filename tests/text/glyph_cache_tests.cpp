#include <tessera/text/glyph_cache.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using tessera::test::check;
using tessera::test::has;
using namespace tessera;

namespace {

struct Rasterizer final : GlyphRasterizer {
    std::uint64_t revision = 1;
    std::uint32_t calls = 0;
    int mode = 0;
    std::uint64_t face_revision(FontId font) const override { return font.value < 2 ? revision : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        ++calls;
        if (mode == 1) return {std::nullopt, {}};
        if (mode == 2) return {std::nullopt, {{"fixture_failure", Severity::error, "/glyph", "Raster rejected.", {}}}};
        if (mode == 3) return {GlyphBitmap{2, 2, 0, 0, {1}}, {}};
        if (mode == 4) return {GlyphBitmap{0, 1, 0, 0, {}}, {}};
        if (mode == 5) return {GlyphBitmap{std::numeric_limits<std::uint32_t>::max(), 2, 0, 0, {}}, {}};
        if (mode == 6) return {GlyphBitmap{}, {{"fixture_failure", Severity::error, "/glyph", "Value with error.", {}}}};
        auto diagnostics = std::vector<Diagnostic>{{"fixture_warning", Severity::warning, "/glyph", "Coverage warning.", {}}};
        if (request.glyph == 0) return {GlyphBitmap{}, diagnostics};
        return {GlyphBitmap{2, 2, -1, 2, {static_cast<std::uint8_t>(request.glyph), 64, 128, 255}}, diagnostics};
    }
};

void cache_and_lifetime() {
    Rasterizer raster;
    GlyphCache cache(raster, {8, 2});
    const GlyphRasterRequest a{{0}, 1, 20 * 64}, b{{0}, 2, 20 * 64}, c{{0}, 3, 20 * 64};
    auto first = cache.get(a);
    check(first && raster.calls == 1 && cache.usage() == GlyphCacheUsage{4, 1}, "First glyph was not cached");
    const auto pinned = *first.value;
    const auto again = cache.get(a);
    check(again && *again.value == pinned && again.diagnostics == first.diagnostics && raster.calls == 1,
          "Hit rerasterized or lost warning/identity");
    check(bool(cache.get(b)), "Second glyph failed");
    check(bool(cache.get(a)), "Touch failed");
    check(bool(cache.get(c)) && raster.calls == 3 && cache.usage() == GlyphCacheUsage{8, 2}, "Budget/LRU insertion failed");
    check(bool(cache.get(a)) && raster.calls == 3, "Most recently used glyph was evicted");
    check(bool(cache.get(b)) && raster.calls == 4, "Least recently used glyph was retained");
    cache.clear();
    check(cache.usage() == GlyphCacheUsage{} && pinned->coverage[0] == 1, "Clear invalidated pinned coverage");
    std::shared_ptr<const GlyphBitmap> detached;
    {
        Rasterizer temporary;
        GlyphCache local(temporary);
        detached = *local.get(a).value;
    }
    check(*detached == *pinned, "Bitmap did not survive source/cache destruction");
    GlyphCache moved(std::move(cache));
    check(bool(moved.get(a)), "Moved cache failed");
    GlyphCache assigned(raster);
    assigned = std::move(moved);
    check(assigned.usage() == GlyphCacheUsage{4, 1}, "Move assignment lost cache");
}

void keys_and_revisions() {
    Rasterizer raster;
    GlyphCache cache(raster);
    const GlyphRasterRequest a{{0}, 1, 20 * 64};
    const auto pinned = *cache.get(a).value;
    check(bool(cache.get({{1}, 1, 20 * 64})) && bool(cache.get({{0}, 1, 20 * 64 + 1})) && raster.calls == 3,
          "Font or fractional pixel size omitted from key");
    ++raster.revision;
    const auto replaced = cache.get(a);
    check(replaced && *replaced.value != pinned && raster.calls == 4 && cache.usage() == GlyphCacheUsage{8, 2},
          "Replacement retained old revision entries or dropped another font");
    raster.revision = 0;
    const auto removed = cache.get(a);
    check(!removed && has(removed.diagnostics, "unknown_font", "/font") && cache.usage() == GlyphCacheUsage{4, 1},
          "Removed face returned stale raster");
    check(pinned->coverage[0] == 1, "Replacement changed pinned raster");
}

void budgets() {
    Rasterizer raster;
    GlyphCache small(raster, {3, 2});
    const GlyphRasterRequest a{{0}, 1, 64};
    auto first = small.get(a), second = small.get(a);
    check(first && second && *first.value != *second.value && small.usage() == GlyphCacheUsage{} && raster.calls == 2,
          "Oversized raster was rejected or cached");
    for (const auto limits : {GlyphCacheLimits{0, 2}, GlyphCacheLimits{8, 0}}) {
        GlyphCache disabled(raster, limits);
        check(bool(disabled.get(a)) && disabled.usage() == GlyphCacheUsage{}, "Zero budget did not disable storage");
    }
    GlyphCache empty(raster, {8, 1});
    const auto space = empty.get({{0}, 0, 64});
    check(space && empty.usage() == GlyphCacheUsage{0, 1}, "Empty glyph not cached");
    check(bool(empty.get({{1}, 0, 64})) && empty.usage() == GlyphCacheUsage{0, 1}, "Empty glyph metadata unbounded");
    GlyphCache bytes_only(raster, {4, 10});
    check(bool(bytes_only.get(a)) && bool(bytes_only.get({{0}, 2, 64})) && bytes_only.usage() == GlyphCacheUsage{4, 1},
          "Byte limit was not enforced independently of entry limit");
}

void failures() {
    Rasterizer raster;
    GlyphCache cache(raster);
    for (auto size : {0u, 63u, 512u * 64 + 1, std::numeric_limits<std::uint32_t>::max()}) {
        const auto result = cache.get({{0}, 1, size});
        check(!result && has(result.diagnostics, "out_of_range", "/pixel_size_64") && raster.calls == 0,
              "Invalid request called rasterizer");
    }
    const auto unknown = cache.get({{2}, 1, 64});
    check(!unknown && has(unknown.diagnostics, "unknown_font", "/font") && raster.calls == 0, "Unknown face called rasterizer");
    for (int mode = 1; mode <= 6; ++mode) {
        raster.mode = mode;
        const auto result = cache.get({{0}, 1, 64});
        const auto code = mode == 1 ? "glyph_raster_failed" : mode == 2 || mode == 6 ? "fixture_failure" : "invalid_glyph_bitmap";
        check(!result && has(result.diagnostics, code, mode >= 3 && mode <= 5 ? "/bitmap" : "/glyph") &&
              cache.usage() == GlyphCacheUsage{}, "Failed or malformed raster was stored");
    }
    raster.mode = 0;
    check(bool(cache.get({{0}, 1, 64})) && raster.calls == 7, "Failures prevented later retry");
    check(!validate(GlyphBitmap{1025, 1024, 0, 0, {}}).empty(), "Oversized bitmap accepted");
}

} // namespace

int main() {
    try {
        cache_and_lifetime(); keys_and_revisions(); budgets(); failures();
        std::cout << "Glyph cache checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
