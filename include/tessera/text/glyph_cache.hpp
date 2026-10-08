#pragma once

#include <tessera/text/text.hpp>
#include <cstddef>
#include <memory>

namespace tessera {

// Physical pixels per em in 1/64-pixel units (26.6), inclusive range [64, 32768].
// Hosts round logical size * device scale * 64 after checking this range.
struct GlyphRasterRequest {
    FontId font;
    std::uint32_t glyph = 0; // Face-specific glyph index, including .notdef (0).
    std::uint32_t pixel_size_64 = 16 * 64;
    bool operator==(const GlyphRasterRequest&) const = default;
};

// Owned top-down, tightly packed 8-bit linear coverage. No color or backend resource.
// left/top are physical pixel bearings from the baseline: x + left, y - top.
// Empty glyphs have width = height = 0 and no coverage. Maximum area is 1 MiB.
struct GlyphBitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::vector<std::uint8_t> coverage;
    bool operator==(const GlyphBitmap&) const = default;
};

std::vector<Diagnostic> validate(const GlyphRasterRequest&);
std::vector<Diagnostic> validate(const GlyphBitmap&);

// Single-threaded, non-reentrant service. A revision is nonzero for a registered face,
// changes whenever that face or raster policy changes, and is never reused by this service.
// Zero means unknown font. Returned images own their bytes; they can outlive the service.
class GlyphRasterizer {
public:
    virtual ~GlyphRasterizer() = default;
    virtual std::uint64_t face_revision(FontId) const = 0;
    virtual Result<GlyphBitmap> rasterize(const GlyphRasterRequest&) = 0;
};

struct GlyphCacheLimits {
    std::size_t bytes = 8 * 1024 * 1024; // Resident coverage only, excluding external owners.
    std::size_t entries = 4096;         // Bounds metadata, including empty glyphs.
};
struct GlyphCacheUsage {
    std::size_t bytes = 0;
    std::size_t entries = 0;
    bool operator==(const GlyphCacheUsage&) const = default;
};

// Backend-independent LRU cache; the borrowed rasterizer must outlive it. Keys include
// font, face revision, glyph, and pixel size. A request drops old revisions of its font.
// Zero limits disable storage. A bitmap larger than the budget is returned uncached.
// Shared immutable results remain valid after eviction/clear/cache or face destruction;
// hosts may retain them through render completion. No atlas region is reused here.
class GlyphCache {
public:
    explicit GlyphCache(GlyphRasterizer&, GlyphCacheLimits = {});
    ~GlyphCache();
    GlyphCache(const GlyphCache&) = delete;
    GlyphCache& operator=(const GlyphCache&) = delete;
    GlyphCache(GlyphCache&&) noexcept;
    GlyphCache& operator=(GlyphCache&&) noexcept;

    Result<std::shared_ptr<const GlyphBitmap>> get(const GlyphRasterRequest&);
    void clear();
    GlyphCacheUsage usage() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tessera
