#include <tessera/text/glyph_cache.hpp>
#include <algorithm>
#include <iterator>
#include <list>
#include <utility>

namespace tessera {

std::vector<Diagnostic> validate(const GlyphRasterRequest& request) {
    if (request.pixel_size_64 < 64 || request.pixel_size_64 > 512 * 64)
        return {{"out_of_range", Severity::error, "/pixel_size_64",
                 "Glyph size must be between 1 and 512 physical pixels per em, in 1/64-pixel units.", {}}};
    return {};
}

std::vector<Diagnostic> validate(const GlyphBitmap& bitmap) {
    const auto area = static_cast<std::uint64_t>(bitmap.width) * bitmap.height;
    if ((bitmap.width == 0) != (bitmap.height == 0) || area > 1024 * 1024 || area != bitmap.coverage.size())
        return {{"invalid_glyph_bitmap", Severity::error, "/bitmap",
                 "Glyph coverage must be tightly packed width * height bytes, at most 1 MiB; empty glyphs need both dimensions zero.", {}}};
    return {};
}

struct GlyphCache::Impl {
    struct Entry {
        GlyphRasterRequest request;
        std::uint64_t revision;
        std::shared_ptr<const GlyphBitmap> bitmap;
        std::vector<Diagnostic> diagnostics;
    };
    GlyphRasterizer& rasterizer;
    GlyphCacheLimits limits;
    GlyphCacheUsage usage;
    // Most recently used at front. Bounded entry count keeps linear lookup bounded;
    // this reference cache has no throughput claim.
    std::list<Entry> entries;

    Impl(GlyphRasterizer& source, GlyphCacheLimits bounds) : rasterizer(source), limits(bounds) {}
    auto erase(std::list<Entry>::iterator at) {
        usage.bytes -= at->bitmap->coverage.size();
        --usage.entries;
        return entries.erase(at);
    }
};

GlyphCache::GlyphCache(GlyphRasterizer& source, GlyphCacheLimits limits) : impl_(std::make_unique<Impl>(source, limits)) {}
GlyphCache::~GlyphCache() = default;
GlyphCache::GlyphCache(GlyphCache&&) noexcept = default;
GlyphCache& GlyphCache::operator=(GlyphCache&&) noexcept = default;

Result<std::shared_ptr<const GlyphBitmap>> GlyphCache::get(const GlyphRasterRequest& request) {
    auto errors = validate(request);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto revision = impl_->rasterizer.face_revision(request.font);
    // Invalidate lazily, including when a service removes a face.
    for (auto at = impl_->entries.begin(); at != impl_->entries.end();) {
        if (at->request.font == request.font && at->revision != revision)
            at = impl_->erase(at);
        else
            ++at;
    }
    if (revision == 0)
        return {std::nullopt, {{"unknown_font", Severity::error, "/font", "Register this FontId before requesting a glyph.", {}}}};
    const auto hit = std::find_if(impl_->entries.begin(), impl_->entries.end(), [&](const auto& entry) {
        return entry.request == request && entry.revision == revision;
    });
    if (hit != impl_->entries.end()) {
        impl_->entries.splice(impl_->entries.begin(), impl_->entries, hit);
        return {hit->bitmap, hit->diagnostics};
    }
    auto raster = impl_->rasterizer.rasterize(request);
    const auto has_error = [](const auto& diagnostics) {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.severity == Severity::error; });
    };
    if (!raster || has_error(raster.diagnostics)) {
        if (!has_error(raster.diagnostics))
            raster.diagnostics.push_back({"glyph_raster_failed", Severity::error, "/glyph", "Rasterizer returned no glyph bitmap.", {}});
        return {std::nullopt, std::move(raster.diagnostics)};
    }
    errors = validate(*raster.value);
    if (!errors.empty()) {
        raster.diagnostics.insert(raster.diagnostics.end(), errors.begin(), errors.end());
        return {std::nullopt, std::move(raster.diagnostics)};
    }
    auto bitmap = std::make_shared<const GlyphBitmap>(std::move(*raster.value));
    const auto bytes = bitmap->coverage.size();
    if (impl_->limits.entries != 0 && impl_->limits.bytes != 0 && bytes <= impl_->limits.bytes) {
        while (impl_->usage.entries >= impl_->limits.entries || bytes > impl_->limits.bytes - impl_->usage.bytes)
            impl_->erase(std::prev(impl_->entries.end()));
        impl_->entries.push_front({request, revision, bitmap, raster.diagnostics});
        impl_->usage.bytes += bytes;
        ++impl_->usage.entries;
    }
    return {std::move(bitmap), std::move(raster.diagnostics)};
}

void GlyphCache::clear() { impl_->entries.clear(); impl_->usage = {}; }
GlyphCacheUsage GlyphCache::usage() const { return impl_->usage; }

} // namespace tessera
