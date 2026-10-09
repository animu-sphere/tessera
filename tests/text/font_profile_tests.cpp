#include <tessera/fonts/font_profile.hpp>
#include <tessera/replay/replay.hpp>
#include <tessera/render/glyph_atlas.hpp>
#include "../check.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {

std::vector<std::byte> bytes(const char* name) {
    std::ifstream file(std::string(TESSERA_FONT_DIR) + "/" + name, std::ios::binary);
    check(file.good(), "Missing explicit font fixture");
    const std::vector<char> input{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> result(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) result[i] = static_cast<std::byte>(input[i]);
    return result;
}

const FontProfile& fixture() {
    static const FontProfile profile = [] {
        FontProfile result;
        result.assets = {{"noto-latin", "2.015", bytes("NotoSans-Regular.ttf")},
                         {"noto-jp", "2.004", bytes("NotoSansJP-Regular.otf")}};
        // Declarations need not follow ID or dependency order.
        result.faces = {{{1}, "noto-jp"}, {{0}, "noto-latin"}};
        result.fallbacks = {{{0}, {{1}}}};
        result.families = {{"cjk", {{{1}, 400}}}, {"latin", {{{0}, 400}}}};
        result.aliases = {{{2}, "ui-sans", {"latin", "cjk"}}};
        return result;
    }();
    return profile;
}

ProfileFontShaper service(FontProfile profile = fixture()) {
    auto result = ProfileFontShaper::create(std::move(profile));
    check(result && result.diagnostics.empty(), "Valid profile rejected");
    return std::move(*result.value);
}

FontShaper reference() {
    FontShaper result;
    check(result.set_face({0}, fixture().assets[0].bytes).empty(), "Reference Latin rejected");
    check(result.set_face({1}, fixture().assets[1].bytes).empty(), "Reference Japanese rejected");
    check(result.set_fallback({0}, {{1}}).empty(), "Reference fallback rejected");
    check(result.set_family("latin", {{{0}, 400}}).empty(), "Reference Latin family rejected");
    check(result.set_family("cjk", {{{1}, 400}}).empty(), "Reference Japanese family rejected");
    check(result.set_alias({2}, "ui-sans", {"latin", "cjk"}).empty(), "Reference alias rejected");
    return result;
}

void owned_selection_and_rasters() {
    auto input = fixture();
    auto text = service(input);
    input.assets[0].bytes.clear();
    input.aliases[0].families = {"cjk"};
    check(text.profile() == fixture(), "Service must own exact declarations and bytes");
    check(text.find_alias("ui-sans") == FontId{2} && !text.find_alias("UI-SANS"), "Alias lookup differs");
    auto ref = reference();
    const TextStyle style{{2}, 20};
    const auto run = text.shape("Hello あい、う", style, {80.0f});
    const auto expected = ref.shape("Hello あい、う", style, {80.0f});
    check(run && expected && run.value == expected.value && run.diagnostics == expected.diagnostics,
          "Profile selection/wrapping differs from explicit registration");
    const auto measure = text.measure("Hello あい、う", style, {80.0f});
    check(measure && *measure.value == run.value->metrics, "Profile measurement and paint shaping differ");
    check(std::any_of(run.value->glyphs.begin(), run.value->glyphs.end(), [](const auto& g) { return g.font == FontId{0}; }) &&
          std::any_of(run.value->glyphs.begin(), run.value->glyphs.end(), [](const auto& g) { return g.font == FontId{1}; }),
          "Mixed fixture must use both concrete faces");
    check(text.face_revision({0}) != 0 && text.face_revision({1}) != 0 && text.face_revision({2}) == 0,
          "Aliases must not become raster faces");
    for (const auto request : {GlyphRasterRequest{{0}, 36, 1280}, GlyphRasterRequest{{1}, 1362, 1600}}) {
        const auto actual = text.rasterize(request);
        const auto expected_raster = ref.rasterize(request);
        check(actual && expected_raster && actual.value == expected_raster.value, "Profile raster bytes differ");
    }
    const auto unknown = text.rasterize({{2}, 36, 1280});
    check(!unknown && has(unknown.diagnostics, "unknown_font", "/font"), "Alias rasterization must fail");
    // A face collection may be shared by several concrete IDs; exact source bytes remain owned once in the profile.
    auto shared = fixture();
    shared.faces.push_back({{3}, "noto-latin"});
    shared.families[1].faces.push_back({{3}, 700});
    auto weighted = service(shared);
    const auto selected = weighted.shape("A", {{2}, 20, {}, 700});
    check(selected && selected.value->glyphs.size() == 1 && selected.value->glyphs[0].font == FontId{3},
          "Declared weight was lost");
    auto moved = std::move(text);
    check(moved.profile() == fixture() && moved.shape("Hello あい、う", style, {80.0f}).value == run.value,
          "Moving the service lost owned inputs");
    auto different = fixture();
    different.assets[0].bytes.back() ^= std::byte{1};
    check(different != fixture(), "Equal metadata cannot conceal different asset bytes");
}

ReplayRecording menu() {
    ReplayRecording result;
    result.viewport = {80, 180};
    result.context.actions = {"start"};
    result.document.root.id = "menu";
    UiNode button;
    button.id = "start";
    button.properties["focusable"] = true;
    button.events["activate"] = "start";
    UiNode label;
    label.kind = NodeKind::text;
    label.properties["text"] = std::string("Hello あい、う");
    button.children.push_back(std::move(label));
    result.document.root.children.push_back(std::move(button));
    result.styles.resize(3);
    result.styles[2].text = {{2}, 20};
    result.steps = {ReplayFocus{AuthorIdTarget{"start"}}, ReplaySemanticAction{AuthorIdTarget{"start"}},
                    ReplayResize{{120, 180}}};
    return result;
}

void replay_and_capture() {
    auto first = service();
    auto second = service(first.profile());
    auto ref = reference();
    const auto recording = menu();
    const auto expected = play_replay(recording, ref);
    const auto actual = play_replay(recording, first);
    const auto repeated = play_replay(recording, second);
    check(expected && actual && repeated && actual.value == expected.value && actual.value == repeated.value &&
          actual.diagnostics == repeated.diagnostics, "Fresh profile services must reproduce replay output");
    check(actual.value->actions.size() == 1 && actual.value->actions[0].action == "start" &&
          actual.value->semantics[1].focused == 1u, "Profile replay must preserve semantic focus/actions");
    auto session = ReplaySession::open(recording, first);
    check(session && session.value->capture() && session.value->output() == *actual.value,
          "Profile shaper must support coherent windowless session capture");
    GlyphCache cache_a(first), cache_b(second);
    for (const float scale : {1.0f, 1.25f, 2.0f}) {
        const auto atlas_a = prepare_glyph_atlas(actual.value->generations[0].paint, cache_a, scale, {10});
        const auto atlas_b = prepare_glyph_atlas(repeated.value->generations[0].paint, cache_b, scale, {10});
        check(atlas_a && atlas_b && !atlas_a.value->pages.empty() && atlas_a.value == atlas_b.value,
              "Controlled scale must reproduce atlas coverage and positioned images");
    }
}

template<class Edit>
void reject(Edit edit, std::string_view code, std::string_view path) {
    auto profile = fixture();
    edit(profile);
    const auto result = ProfileFontShaper::create(std::move(profile));
    check(!result && has(result.diagnostics, code, path), std::string("Missing located rejection: ") + std::string(path));
}

void invalid_profiles() {
    auto valid = service();
    auto session = ReplaySession::open(menu(), valid);
    check(session && session.value->capture(), "Valid session rejected");
    const auto before = session.value->output();
    reject([](auto& p) { p.version = 0; }, "unsupported_version", "/version");
    reject([](auto& p) { p.assets.clear(); }, "out_of_range", "/assets");
    reject([](auto& p) { p.faces.clear(); }, "out_of_range", "/faces");
    reject([](auto& p) { p.aliases.resize(65); }, "out_of_range", "/aliases");
    reject([](auto& p) { p.assets[0].id.clear(); }, "invalid_font_name", "/assets/0/id");
    reject([](auto& p) { p.assets[0].id = std::string(257, 'a'); }, "invalid_font_name", "/assets/0/id");
    reject([](auto& p) { p.assets[0].version = "2\n015"; }, "invalid_font_name", "/assets/0/version");
    reject([](auto& p) { p.assets[0].version = std::string(1, '\xff'); }, "invalid_utf8", "/assets/0/version");
    reject([](auto& p) { p.assets[1].id = p.assets[0].id; }, "duplicate_font_asset", "/assets/1/id");
    reject([](auto& p) { p.assets[0].bytes.clear(); }, "invalid_font", "/assets/0/bytes");
    reject([](auto& p) { p.assets[0].bytes = {std::byte{0}}; }, "invalid_font", "/faces/1");
    reject([](auto& p) { p.faces[0].face_index = 100; }, "invalid_font", "/faces/0");
    reject([](auto& p) { p.faces[1].font = p.faces[0].font; }, "duplicate_font", "/faces/1/font");
    reject([](auto& p) { p.faces[0].asset = "missing"; }, "unknown_font_asset", "/faces/0/asset");
    reject([](auto& p) {
        p.faces.clear();
        for (std::uint32_t i = 0; i < 64; ++i) p.faces.push_back({{i}, "noto-jp"});
    }, "out_of_range", "/faces/59/asset");
    reject([](auto& p) { p.fallbacks.push_back(p.fallbacks[0]); }, "duplicate_font", "/fallbacks/1/font");
    reject([](auto& p) { p.fallbacks[0].font = {7}; }, "unknown_font", "/fallbacks/0/font");
    reject([](auto& p) { p.fallbacks[0].faces = {{7}}; }, "unknown_font", "/fallbacks/0/faces/0");
    reject([](auto& p) { p.fallbacks[0].faces = {{1}, {1}}; }, "duplicate_font", "/fallbacks/0/faces/1");
    reject([](auto& p) { p.families.push_back(p.families[0]); }, "duplicate_font_family", "/families/2/name");
    reject([](auto& p) { p.families[0].faces[0].weight = 0; }, "out_of_range", "/families/0/faces/0/weight");
    reject([](auto& p) { p.families[0].faces[0].font = {7}; }, "unknown_font", "/families/0/faces/0/font");
    reject([](auto& p) { p.aliases[0].font = {0}; }, "font_id_conflict", "/aliases/0/font");
    reject([](auto& p) { p.aliases[0].families = {"absent"}; }, "unknown_font_family", "/aliases/0/families/0");
    reject([](auto& p) { p.aliases.push_back(p.aliases[0]); }, "font_alias_conflict", "/aliases/1");
    reject([](auto& p) { auto alias = p.aliases[0]; alias.font = {3}; p.aliases.push_back(alias); },
           "font_alias_conflict", "/aliases/1");
    // A failed independent creation cannot disturb an already borrowed service/session.
    check(session.value->capture() && session.value->output() == before,
          "Rejected independent profiles disturbed a borrowed service/session");
}

} // namespace

int main() {
    try {
        owned_selection_and_rasters();
        replay_and_capture();
        invalid_profiles();
        std::cout << "Owned font profile, sealed services, replay/capture, raster and rejection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
