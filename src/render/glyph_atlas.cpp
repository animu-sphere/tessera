#include <tessera/render/glyph_atlas.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace tessera {
namespace {
Diagnostic error(std::string code, std::string path, std::string message) {
    return {std::move(code), Severity::error, std::move(path), std::move(message), {}};
}
struct Placement {
    std::shared_ptr<const GlyphBitmap> bitmap;
    std::size_t page = 0;
    std::uint32_t x = 0, y = 0;
};
bool representable(double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}
}

Result<GlyphAtlasFrame> prepare_glyph_atlas(const UiDrawList& list, GlyphCache& cache,
    float scale, ImageHandle first, GlyphAtlasLimits limits) {
    Result<GlyphAtlasFrame> result;
    result.diagnostics = validate(list);
    auto fail = [&](std::string code, std::string path, std::string message) {
        result.diagnostics.push_back(error(std::move(code), std::move(path), std::move(message)));
    };
    if (!std::isfinite(scale) || scale <= 0)
        fail("out_of_range", "/device_scale", "Provide a finite positive physical pixel scale.");
    if (limits.page_size < 4 || limits.page_size > 2048 || !limits.pages || limits.pages > 64 || !limits.glyphs)
        fail("out_of_range", "/limits", "Use page_size 4..2048, pages 1..64 and a positive glyph occurrence limit.");
    if (!first.value || !limits.pages || first.value > UINT64_MAX - (limits.pages - 1))
        fail("invalid_handle", "/first_image", "Reserve a nonzero consecutive image handle range without overflow.");
    std::size_t occurrences = 0;
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto path = "/commands/" + std::to_string(i);
        if (const auto* image = std::get_if<DrawImage>(&list.commands[i])) {
            if (image->image.value >= first.value && image->image.value - first.value < limits.pages)
                fail("image_collision", path + "/image", "Reserve atlas handles outside the source list's image handles.");
        }
        if (const auto* run = std::get_if<DrawGlyphRun>(&list.commands[i])) {
            if (run->run.glyphs.size() > limits.glyphs - std::min(occurrences, limits.glyphs))
                fail("glyph_limit", path + "/run/glyphs", "Reduce glyph occurrences or increase the declared limit.");
            else occurrences += run->run.glyphs.size();
            const double size = double(run->run.size) * scale * 64;
            if (!std::isfinite(size) || size < 64 || size > 32768)
                fail("out_of_range", path + "/run/size", "Logical font size times scale must be 1..512 physical pixels per em.");
        }
    }
    if (!result.diagnostics.empty()) return result;

    GlyphAtlasFrame output;
    using Key = std::tuple<std::uint64_t, std::uint32_t, std::uint32_t>;
    std::map<Key, Placement> placements;
    std::uint32_t x = 0, y = 0, row_height = 0;
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto* run = std::get_if<DrawGlyphRun>(&list.commands[i]);
        if (!run) { output.draw_list.commands.push_back(list.commands[i]); continue; }
        const auto pixel_size = std::uint32_t(std::round(double(run->run.size) * scale * 64));
        for (std::size_t g = 0; g < run->run.glyphs.size(); ++g) {
            const auto& glyph = run->run.glyphs[g];
            const auto path = "/commands/" + std::to_string(i) + "/run/glyphs/" + std::to_string(g);
            const Key key{glyph.font.value, glyph.id, pixel_size};
            auto found = placements.find(key);
            if (found == placements.end()) {
                auto raster = cache.get({glyph.font, glyph.id, pixel_size});
                for (auto diagnostic : raster.diagnostics) {
                    diagnostic.path = path + diagnostic.path;
                    result.diagnostics.push_back(std::move(diagnostic));
                }
                if (!raster) return result;
                Placement placement; placement.bitmap = *raster.value;
                const auto& bitmap = *placement.bitmap;
                if (bitmap.width) {
                    if (bitmap.width > limits.page_size - 2 || bitmap.height > limits.page_size - 2) {
                        fail("glyph_atlas_size", path, "Glyph plus transparent gutter exceeds page_size; increase the page size.");
                        return result;
                    }
                    const auto width = bitmap.width + 2, height = bitmap.height + 2;
                    if (x + width > limits.page_size) { x = 0; y += row_height; row_height = 0; }
                    if (output.pages.empty() || y + height > limits.page_size) {
                        if (output.pages.size() == limits.pages) {
                            fail("glyph_atlas_full", path, "Increase atlas page count/size or reduce the prepared frame.");
                            return result;
                        }
                        output.pages.push_back({{first.value + output.pages.size()}, limits.page_size,
                            std::vector<std::uint8_t>(std::size_t(limits.page_size) * limits.page_size, 0)});
                        x = y = row_height = 0;
                    }
                    placement.page = output.pages.size() - 1; placement.x = x + 1; placement.y = y + 1;
                    auto& page = output.pages.back();
                    for (std::uint32_t row = 0; row < bitmap.height; ++row)
                        std::copy_n(bitmap.coverage.begin() + std::size_t(row) * bitmap.width, bitmap.width,
                            page.coverage.begin() + std::size_t(placement.y + row) * page.size + placement.x);
                    x += width; row_height = std::max(row_height, height);
                }
                found = placements.emplace(key, std::move(placement)).first;
            }
            const auto& placement = found->second;
            const auto& bitmap = *placement.bitmap;
            const double left = double(run->origin.x) + glyph.position.x + double(bitmap.left) / scale;
            const double top = double(run->origin.y) + glyph.position.y - double(bitmap.top) / scale;
            const double width = double(bitmap.width) / scale, height = double(bitmap.height) / scale;
            if (!representable(left) || !representable(top) || !representable(width) || !representable(height) ||
                !representable(left + width) || !representable(top + height)) {
                fail("geometry_overflow", path + "/position", "Baseline plus raster bearings/extent exceeds representable coordinates.");
                return result;
            }
            if (!bitmap.width) continue;
            const auto& page = output.pages[placement.page];
            const float edge = float(page.size);
            output.draw_list.commands.push_back(DrawImage{
                {{float(left), float(top)}, {float(width), float(height)}}, page.image,
                {{placement.x / edge, placement.y / edge}, {bitmap.width / edge, bitmap.height / edge}}, run->color});
        }
    }
    auto diagnostics = validate(output.draw_list);
    result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
    if (!diagnostics.empty()) return result;
    result.value = std::move(output);
    return result;
}
} // namespace tessera
