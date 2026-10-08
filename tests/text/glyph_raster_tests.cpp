#include <tessera/fonts/font_shaper.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

using tessera::test::check;
using tessera::test::has;
using namespace tessera;

namespace {

std::vector<std::byte> read_font(const char* name) {
    std::ifstream file(std::string(TESSERA_FONT_DIR) + "/" + name, std::ios::binary);
    check(file.good(), "Missing fixture font");
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> data(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) data[i] = static_cast<std::byte>(bytes[i]);
    return data;
}

FontShaper fixture_shaper() {
    FontShaper shaper;
    check(shaper.set_face({0}, read_font("NotoSans-Regular.ttf")).empty(), "Latin fixture rejected");
    check(shaper.set_face({1}, read_font("NotoSansJP-Regular.otf")).empty(), "Japanese fixture rejected");
    check(shaper.set_fallback({0}, {{1}}).empty(), "Fallback rejected");
    return shaper;
}

void write_pgm(const char* name, const GlyphBitmap& bitmap) {
    std::filesystem::create_directories(TESSERA_GLYPH_ARTIFACT_DIR);
    std::ofstream file(std::string(TESSERA_GLYPH_ARTIFACT_DIR) + "/" + name + ".pgm", std::ios::binary);
    file << "P5\n" << bitmap.width << ' ' << bitmap.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(bitmap.coverage.data()), static_cast<std::streamsize>(bitmap.coverage.size()));
    check(file.good(), "Could not write glyph artifact");
}

void image_references() {
    auto shaper = fixture_shaper();
    GlyphCache cache(shaper);
    // Independent FreeType 2.13.3 references: see fixtures/glyphs/generate_reference.py.
    struct Expected { const char* name; GlyphRasterRequest request; std::uint32_t width, height; std::int32_t left, top; };
    const Expected cases[] = {
        {"latin-A-20", {{0}, 36, 1280}, 13, 15, 0, 15},
        {"latin-j-20", {{0}, 77, 1280}, 6, 20, -2, 15},
        {"latin-ffi-20", {{0}, 1656, 1280}, 18, 16, 0, 16},
        {"latin-mark-20", {{0}, 2665, 1280}, 5, 4, -7, 16},
        {"latin-A-20.25", {{0}, 36, 1296}, 13, 15, 0, 15},
        {"japanese-me-25", {{1}, 1362, 1600}, 20, 20, 2, 19},
        {"latin-notdef-20", {{0}, 0, 1280}, 10, 15, 1, 15},
    };
    for (const auto& expected : cases) {
        const auto raster = cache.get(expected.request);
        check(raster && raster.diagnostics.empty(), "Reference raster failed");
        const auto& actual = **raster.value;
        write_pgm(expected.name, actual);
        check(actual.width == expected.width && actual.height == expected.height && actual.left == expected.left &&
              actual.top == expected.top, std::string(expected.name) + ": raster bounds/bearings differ");
        std::ifstream file(std::string(TESSERA_GLYPH_DIR) + "/" + expected.name + ".pgm", std::ios::binary);
        std::string magic;
        std::uint32_t width = 0, height = 0, max = 0;
        file >> magic >> width >> height >> max;
        check(file.good() && magic == "P5" && width == expected.width && height == expected.height && max == 255 &&
              file.get() == '\n', "Invalid reference header");
        const std::vector<char> pixels{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        check(pixels.size() == actual.coverage.size(), "Reference coverage size differs");
        for (std::size_t i = 0; i < pixels.size(); ++i)
            check(static_cast<std::uint8_t>(pixels[i]) == actual.coverage[i], std::string(expected.name) + ": coverage differs");
        const auto direct = shaper.rasterize(expected.request);
        check(direct && *direct.value == actual, "Direct and cached rasters differ");
    }
    const auto space = shaper.rasterize({{0}, 3, 1280});
    check(space && *space.value == GlyphBitmap{}, "Whitespace produced ink or invalid empty dimensions");
    const auto tiny = shaper.rasterize({{0}, 36, 64});
    const auto large = shaper.rasterize({{1}, 1362, 512 * 64});
    check(tiny && large && validate(*tiny.value).empty() && validate(*large.value).empty(), "Boundary sizes failed");
}

void replacement_and_failures() {
    auto shaper = fixture_shaper();
    GlyphCache cache(shaper);
    const GlyphRasterRequest request{{0}, 36, 1280};
    const auto old = *cache.get(request).value;
    const auto saved = *old;
    const auto revision = shaper.face_revision({0});
    const auto other_revision = shaper.face_revision({1});
    for (auto data : {std::vector<std::byte>{}, std::vector<std::byte>(32, std::byte{1})})
        check(has(shaper.set_face({0}, std::move(data)), "invalid_font", "/font"), "Malformed replacement accepted");
    check(has(shaper.set_face({0}, read_font("NotoSans-Regular.ttf"), 1), "invalid_font", "/font"), "Invalid face index accepted");
    check(shaper.face_revision({0}) == revision && *cache.get(request).value == old, "Rejected face changed revision or raster");
    check(shaper.set_face({0}, read_font("NotoSansJP-Regular.otf")).empty(), "Replacement failed");
    check(shaper.face_revision({0}) != revision && shaper.face_revision({1}) == other_revision, "Replacement revision not isolated");
    const auto changed = cache.get(request);
    check(changed && **changed.value != *old && *changed.value != old && cache.usage().entries == 1,
          "Old face was reused or left resident after replacement");
    check(*old == saved, "Pinned image changed on replacement");
    for (auto size : {0u, 63u, 512u * 64 + 1, std::numeric_limits<std::uint32_t>::max()}) {
        const auto result = shaper.rasterize({{0}, 36, size});
        check(!result && has(result.diagnostics, "out_of_range", "/pixel_size_64"), "Invalid raster size accepted");
    }
    const auto unknown = shaper.rasterize({{99}, 36, 1280});
    check(shaper.face_revision({99}) == 0 && !unknown && has(unknown.diagnostics, "unknown_font", "/font"), "Unknown font accepted");
    const auto bad_glyph = shaper.rasterize({{1}, std::numeric_limits<std::uint32_t>::max(), 1280});
    check(!bad_glyph && has(bad_glyph.diagnostics, "invalid_glyph", "/glyph"), "Invalid glyph accepted");
    TextStyle style;
    const auto before = shaper.shape("メニュー", style);
    FontShaper moved(std::move(shaper));
    const auto after = moved.shape("メニュー", style);
    check(before && after && *before.value == *after.value && moved.face_revision({0}) != 0 &&
          bool(moved.rasterize(request)), "Moving service lost shared shaping/raster ownership");
}

void wrapped_paint_rasters() {
    auto shaper = fixture_shaper();
    GlyphCache cache(shaper);
    UiDocument document;
    document.root.kind = NodeKind::text;
    document.root.properties.emplace("text", std::string("Start スタート"));
    const auto tree = UiTree::create(document);
    check(bool(tree), "Text document failed");
    ResolvedStyle style;
    style.text.size = 20;
    style.padding = {4, 4, 4, 4};
    const std::vector<ResolvedStyle> styles{style};
    const auto layout = compute_layout({tree.value->get(), styles, {78.1f, 100}, &shaper});
    check(bool(layout), "Wrapped layout failed");
    const auto paint = build_paint_list({tree.value->get(), styles, &*layout.value, &shaper});
    check(bool(paint), "Wrapped paint failed");
    const DrawGlyphRun* draw = nullptr;
    for (const auto& command : paint.value->commands)
        if (const auto* text = std::get_if<DrawGlyphRun>(&command)) draw = text;
    check(draw && draw->run.glyphs.size() == 10 && draw->run.metrics.lines == 2 && draw->origin == Point{4, 4},
          "Wrapped paint run/origin differs");
    check(std::fabs(draw->run.glyphs[6].position.x - 50.1f) < 1e-4f &&
          std::fabs(draw->run.glyphs[7].position.y - 48.62f) < 1e-4f && draw->run.glyphs[6].font == FontId{1},
          "Wrapped fallback baseline/face differs");
    constexpr float scale = 1.25f;
    GlyphBitmap image{110, 90, 0, 0, std::vector<std::uint8_t>(110 * 90, 255)};
    for (const auto& glyph : draw->run.glyphs) {
        const GlyphRasterRequest request{glyph.font, glyph.id, 1600}; // 20 logical units at scale 1.25.
        const auto raster = cache.get(request);
        check(bool(raster), "Paint glyph failed to rasterize its selected face");
        const auto& bitmap = **raster.value;
        const auto x = static_cast<int>(std::lround((draw->origin.x + glyph.position.x) * scale)) + bitmap.left;
        const auto y = static_cast<int>(std::lround((draw->origin.y + glyph.position.y) * scale)) - bitmap.top;
        // Diagnostic contact image only: nearest integer baseline phase, no renderer/clip claim.
        for (std::uint32_t row = 0; row < bitmap.height; ++row)
            for (std::uint32_t col = 0; col < bitmap.width; ++col) {
                const auto px = x + static_cast<int>(col), py = y + static_cast<int>(row);
                check(px >= 0 && py >= 0 && px < 110 && py < 90, "Glyph contact image bounds differ");
                auto& target = image.coverage[static_cast<std::size_t>(py) * image.width + px];
                target = static_cast<std::uint8_t>((target * (255u - bitmap.coverage[row * bitmap.width + col]) + 127u) / 255u);
            }
    }
    write_pgm("mixed-wrapped", image);
    // Raster operations change FreeType's size/slot only, never the HarfBuzz metrics or pens.
    const auto reshaped = shaper.shape("Start スタート", style.text, {70.1f});
    check(reshaped && *reshaped.value == draw->run, "Rasterization changed measurement/shaping");
    const auto retained = cache.usage();
    check(shaper.set_fallback({0}, {}).empty(), "Clearing fallback failed");
    check(bool(cache.get({{1}, 1362, 1600})) && cache.usage().entries >= retained.entries,
          "Fallback changes invalidated face rasters");
}

} // namespace

int main() {
    try {
        image_references(); replacement_and_failures(); wrapped_paint_rasters();
        std::cout << "Grayscale glyph images, ownership, replacement, and wrapped paint checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
