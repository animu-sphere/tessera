#include <tessera/fonts/font_shaper.hpp>
#include <tessera/render/paint.hpp>
#include "../check.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

using tessera::test::check;
using tessera::test::has;

namespace {

// Expected glyph IDs, clusters, and font-unit advances were taken from the fixture fonts
// independently of this shaper (fontTools 4.60.1 tables, uharfbuzz shaping with language "und").
constexpr tessera::FontId latin{0};
constexpr tessera::FontId japanese{1};

std::vector<std::byte> read_font(const char* name) {
    std::ifstream file(std::string(TESSERA_FONT_DIR) + "/" + name, std::ios::binary);
    check(file.good(), std::string("Missing font fixture ") + name);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> data(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) data[i] = static_cast<std::byte>(bytes[i]);
    return data;
}

tessera::FontShaper fixture_shaper() {
    tessera::FontShaper shaper;
    check(shaper.set_face(latin, read_font("NotoSans-Regular.ttf")).empty(), "Noto Sans rejected");
    check(shaper.set_face(japanese, read_font("NotoSansJP-Regular.otf")).empty(), "Noto Sans JP rejected");
    return shaper;
}

bool near(float actual, float expected) { return std::fabs(actual - expected) <= 1e-4f; }
bool near(tessera::Point actual, tessera::Point expected) { return near(actual.x, expected.x) && near(actual.y, expected.y); }
bool near(const tessera::TextMetrics& actual, const tessera::TextMetrics& expected) {
    return near(actual.size.width, expected.size.width) && near(actual.size.height, expected.size.height) &&
           near(actual.baseline, expected.baseline) && near(actual.line_height, expected.line_height) &&
           actual.lines == expected.lines;
}

struct Expected {
    std::uint32_t id;
    std::uint32_t cluster;
    float x;
};

tessera::GlyphRun shaped(tessera::FontShaper& shaper, std::string_view text, const tessera::TextStyle& style,
                         std::size_t warnings = 0) {
    auto run = shaper.shape(text, style);
    check(run && run.diagnostics.size() == warnings, "Valid text rejected or warned: " + std::string(text));
    const auto repeated = shaper.shape(text, style);
    check(repeated && *repeated.value == *run.value, "Repeated shaping differs");
    const auto measured = shaper.measure(text, style);
    check(measured && *measured.value == run.value->metrics && measured.diagnostics == run.diagnostics,
          "Measurement and shaping disagree");
    return std::move(*run.value);
}

void check_glyphs(const tessera::GlyphRun& run, std::initializer_list<Expected> expected, float y, std::string_view what) {
    check(run.glyphs.size() == expected.size(), std::string(what) + ": glyph count differs");
    std::size_t i = 0;
    for (const auto& glyph : expected) {
        const auto& actual = run.glyphs[i++];
        check(actual.id == glyph.id && actual.cluster == glyph.cluster && near(actual.position, {glyph.x, y}),
              std::string(what) + ": glyph " + std::to_string(i - 1) + " differs");
    }
}

void latin_shaping() {
    auto shaper = fixture_shaper();
    tessera::TextStyle style; // Default FontId 0: Noto Sans, upem 1000, ascender 1069, descender -293.
    style.size = 20;
    const auto hello = shaped(shaper, "Hello", style);
    check(hello.font == latin && hello.size == 20, "Run identity differs");
    check(near(hello.metrics, {{48.52f, 27.24f}, 21.38f, 27.24f, 1}), "Latin metrics differ");
    check_glyphs(hello, {{43, 0, 0}, {72, 1, 14.82f}, {79, 2, 26.1f}, {79, 3, 31.26f}, {82, 4, 36.42f}}, 21.38f, "Hello");

    // GPOS kerning moves V left by 40 units relative to the hmtx advance of A (639).
    const auto kerned = shaped(shaper, "AV", style);
    check_glyphs(kerned, {{36, 0, 0}, {57, 1, 11.98f}}, 21.38f, "AV");
    check(near(kerned.metrics.size.width, 23.98f), "Kerned width differs");

    // Default ligatures: "fi" and "ffi" each become one glyph clustered at their first byte.
    const auto ligatures = shaped(shaper, "fi office", style);
    check_glyphs(ligatures, {{1654, 0, 0}, {3, 2, 12.04f}, {82, 3, 17.24f}, {1656, 4, 29.34f}, {70, 7, 48.26f},
                             {72, 8, 57.86f}}, 21.38f, "fi office");
    check(near(ligatures.metrics.size.width, 69.14f), "Ligature width differs");

    // GPOS mark attachment: the zero-advance acute is offset (-118, +178 up) from the pen after Q,
    // and shares the grapheme's cluster.
    const auto mark = shaped(shaper, "Q\xcc\x81", style);
    check(mark.glyphs.size() == 2 && mark.glyphs[0].id == 52 && mark.glyphs[1].id == 2665 &&
          mark.glyphs[1].cluster == 0 && near(mark.glyphs[1].position, {13.26f, 17.82f}), "Mark position differs");
    check(near(mark.metrics.size.width, 15.62f), "Mark width differs");
}

void japanese_shaping() {
    auto shaper = fixture_shaper();
    tessera::TextStyle style; // Noto Sans JP: upem 1000, hhea ascender 1160, descender -288.
    style.font = japanese;
    style.size = 20;
    const auto menu = shaped(shaper, "メニュー", style);
    check(near(menu.metrics, {{80, 28.96f}, 23.2f, 28.96f, 1}), "Japanese metrics differ");
    check_glyphs(menu, {{1362, 0, 0}, {1340, 3, 20}, {1366, 6, 40}, {1389, 9, 60}}, 23.2f, "メニュー");

    const auto mixed = shaped(shaper, "Start スタート", style);
    check_glyphs(mixed, {{52, 0, 0}, {85, 1, 11.38f}, {66, 2, 18.44f}, {83, 3, 29.7f}, {85, 4, 37.46f},
                         {1, 5, 45}, {1322, 6, 49.48f}, {1328, 9, 69.48f}, {1389, 12, 89.48f}, {1337, 15, 109.48f}},
                 23.2f, "Start スタート");
    check(near(mixed.metrics.size.width, 129.48f), "Mixed width differs");

    // LF starts a line, emits no glyph, and keeps clusters relative to the whole text.
    const auto lines = shaped(shaper, "Start\nメニュー", style);
    check(near(lines.metrics, {{80, 57.92f}, 23.2f, 28.96f, 2}), "Multiline metrics differ");
    check(lines.glyphs.size() == 9 && lines.glyphs[5].cluster == 6 && near(lines.glyphs[5].position, {0, 52.16f}),
          "Second-line glyph differs");

    const auto empty = shaped(shaper, "", style);
    check(empty.glyphs.empty() && near(empty.metrics, {{0, 28.96f}, 23.2f, 28.96f, 1}),
          "Empty text should be one empty line");
    const auto blank = shaped(shaper, "\n", style);
    check(blank.glyphs.empty() && blank.metrics.lines == 2, "Empty lines must count");

    style.line_height = 40.0f; // Half-leading centers the 28.96 extent.
    const auto tall = shaped(shaper, "メ", style);
    check(near(tall.metrics, {{20, 40}, 28.72f, 40, 1}) && near(tall.glyphs[0].position, {0, 28.72f}),
          "Explicit line height must center the extent");
}

void missing_glyphs() {
    auto shaper = fixture_shaper();
    tessera::TextStyle style;
    style.size = 20;
    // Noto Sans has no Katakana: the missing cluster keeps glyph 0 and its 600-unit advance.
    const auto run = shaper.shape("AVメ", style);
    check(run && run.value->glyphs.size() == 3, "Missing glyphs must still shape");
    const auto& missing = run.value->glyphs[2];
    check(missing.id == 0 && missing.cluster == 2 && near(missing.position.x, 23.98f), "Missing glyph differs");
    check(near(run.value->metrics.size.width, 35.98f), "Missing glyph width differs");
    check(run.diagnostics.size() == 1 && run.diagnostics[0].code == "missing_glyph" &&
          run.diagnostics[0].severity == tessera::Severity::warning && run.diagnostics[0].path == "/text" &&
          run.diagnostics[0].message.find("byte 2") != std::string::npos && !run.diagnostics[0].byte_offset,
          "Missing glyph warning differs");
    const auto measured = shaper.measure("AVメ", style);
    check(measured && measured.diagnostics == run.diagnostics, "Measurement must report missing glyphs");
}

void faces_and_failures() {
    auto shaper = fixture_shaper();
    check(has(shaper.set_face(latin, {}), "invalid_font", "/font"), "Empty font accepted");
    std::vector<std::byte> garbage(64, std::byte{0x41});
    check(has(shaper.set_face(latin, garbage), "invalid_font", "/font"), "Non-font data accepted");
    check(has(shaper.set_face(latin, read_font("NotoSans-Regular.ttf"), 1), "invalid_font", "/font"),
          "Out-of-range face index accepted");
    tessera::TextStyle style;
    style.size = 20;
    check(near(shaper.measure("Hello", style).value->size.width, 48.52f), "Rejected face replaced the previous one");

    // Replacing the default face changes subsequent results.
    check(shaper.set_face(latin, read_font("NotoSansJP-Regular.otf")).empty(), "Replacement rejected");
    const auto replaced = shaper.measure("メ", style);
    check(replaced && replaced.diagnostics.empty() && near(replaced.value->size.width, 20), "Replacement not used");

    tessera::FontShaper moved(std::move(shaper));
    check(moved.has_face(latin) && moved.has_face(japanese) && !moved.has_face({7}), "Moved shaper lost faces");
    style.font = {7};
    check(has(moved.shape("x", style).diagnostics, "unknown_font", "/style/font"), "Unknown font accepted");
    style.font = latin;
    check(has(moved.shape("\xff", style).diagnostics, "invalid_utf8", "/text"), "Invalid UTF-8 accepted");
    style.size = 0;
    style.weight = 0;
    const auto errors = moved.measure("x", style);
    check(!errors && has(errors.diagnostics, "out_of_range", "/style/size") &&
          has(errors.diagnostics, "out_of_range", "/style/weight"), "Invalid text style accepted");
}

tessera::UiNode text(std::string content) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.properties["text"] = std::move(content);
    return result;
}

void layout_and_paint_agree() {
    auto shaper = fixture_shaper();
    tessera::UiDocument document;
    document.root.children = {text("Start スタート"), text("メニュー\nQuit")};
    auto created = tessera::UiTree::create(document);
    check(static_cast<bool>(created), "Fixture tree rejected");
    const auto tree = std::move(*created.value);
    std::vector<tessera::ResolvedStyle> styles(tree->size());
    for (auto& style : styles) {
        style.text.font = japanese;
        style.text.size = 20;
    }
    styles[0].align = tessera::Align::start; // Text boxes keep their measured width.
    const auto layout = tessera::compute_layout({tree.get(), styles, {400, 300}, &shaper});
    check(static_cast<bool>(layout), "Layout rejected");
    const auto paint = tessera::build_paint_list({tree.get(), styles, &*layout.value, &shaper});
    check(paint && paint.diagnostics.empty(), "Paint rejected");
    std::size_t runs = 0;
    for (const auto& box : layout.value->boxes) {
        const auto& node = *tree->get(box.node);
        if (node.kind != tessera::NodeKind::text) continue;
        const auto& content = std::get<std::string>(node.properties.at("text"));
        const auto run = shaper.shape(content, styles[box.node.index].text);
        check(box.content_box().size == run.value->metrics.size, "Layout did not use the measured size");
        bool painted = false;
        for (const auto& command : paint.value->commands) {
            const auto* glyphs = std::get_if<tessera::DrawGlyphRun>(&command);
            painted = painted || (glyphs && glyphs->run == *run.value && glyphs->origin == box.content_box().origin);
        }
        check(painted, "Paint did not use the shaped run at the content origin");
        ++runs;
    }
    check(runs == 2, "Expected two Text boxes");
}

} // namespace

int main() {
    try {
        latin_shaping();
        japanese_shaping();
        missing_glyphs();
        faces_and_failures();
        layout_and_paint_agree();
        std::cout << "Font shaping, metrics, missing-glyph, failure, and layout/paint agreement checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
