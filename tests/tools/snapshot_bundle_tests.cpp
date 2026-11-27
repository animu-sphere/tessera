#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/text/glyph_cache.hpp>
#include "../check.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

// Test-local inflater for the encoder's single fixed-Huffman block, so pixels round-trip without a codec.
class BitReader {
public:
    explicit BitReader(std::string_view data) : data_(data) {}
    unsigned get(unsigned count) {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i) value |= next() << i;
        return value;
    }
    unsigned next() {
        check(position_ < data_.size(), "Deflate stream ended early");
        const unsigned bit = (static_cast<unsigned char>(data_[position_]) >> bit_) & 1;
        if (++bit_ == 8) {
            bit_ = 0;
            ++position_;
        }
        return bit;
    }
    std::size_t consumed() const { return position_ + (bit_ ? 1 : 0); }

private:
    std::string_view data_;
    std::size_t position_ = 0;
    unsigned bit_ = 0;
};

unsigned fixed_literal(BitReader& in) {
    unsigned code = 0;
    for (int i = 0; i < 7; ++i) code = (code << 1) | in.next();
    if (code <= 23) return 256 + code;
    code = (code << 1) | in.next();
    if (code >= 0x30 && code <= 0xbf) return code - 0x30;
    if (code >= 0xc0 && code <= 0xc7) return 280 + code - 0xc0;
    code = (code << 1) | in.next();
    check(code >= 0x190 && code <= 0x1ff, "Invalid fixed literal code");
    return 144 + code - 0x190;
}

std::string inflate_fixed(std::string_view zlib) {
    check(zlib.size() > 6 && static_cast<unsigned char>(zlib[0]) == 0x78 &&
              ((static_cast<unsigned char>(zlib[0]) << 8) | static_cast<unsigned char>(zlib[1])) % 31 == 0,
          "zlib header must be valid");
    constexpr std::array<unsigned, 29> length_base{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    constexpr std::array<unsigned, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    constexpr std::array<unsigned, 30> distance_base{1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                     33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    constexpr std::array<unsigned, 30> distance_extra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    BitReader in(zlib.substr(2));
    check(in.get(1) == 1 && in.get(2) == 1, "Encoder must write one final fixed-Huffman block");
    std::string out;
    for (;;) {
        const auto symbol = fixed_literal(in);
        if (symbol < 256) {
            out.push_back(char(symbol));
            continue;
        }
        if (symbol == 256) break;
        const auto l = symbol - 257;
        check(l < 29, "Invalid length symbol");
        const auto length = length_base[l] + in.get(length_extra[l]);
        unsigned d = 0;
        for (int i = 0; i < 5; ++i) d = (d << 1) | in.next();
        check(d < 30, "Invalid distance symbol");
        const auto distance = distance_base[d] + in.get(distance_extra[d]);
        check(distance <= out.size(), "Distance must stay within the output");
        for (unsigned i = 0; i < length; ++i) out.push_back(out[out.size() - distance]);
    }
    const auto trailer = zlib.substr(2 + in.consumed());
    check(trailer.size() == 4, "zlib stream must end with its Adler-32");
    std::uint32_t a = 1, b = 0;
    for (const char c : out) {
        a = (a + static_cast<unsigned char>(c)) % 65521;
        b = (b + a) % 65521;
    }
    std::uint32_t stored = 0;
    for (const char c : trailer) stored = (stored << 8) | static_cast<unsigned char>(c);
    check(stored == ((b << 16) | a), "Adler-32 must match the inflated bytes");
    return out;
}

std::uint32_t be32(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[at + i]);
    return value;
}

// Decodes the encoder's PNG layout: IHDR, sRGB, one IDAT, IEND, filter None on every row.
tessera::CapturedImage decode_png(std::string_view png) {
    check(png.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8), "PNG signature");
    std::size_t at = 8;
    std::vector<std::string> types;
    tessera::CapturedImage image;
    std::string idat;
    while (at < png.size()) {
        const auto length = be32(png, at);
        const auto type = std::string(png.substr(at + 4, 4));
        const auto data = png.substr(at + 8, length);
        types.push_back(type);
        if (type == "IHDR") {
            image.extent = {be32(data, 0), be32(data, 4)};
            check(data.substr(8) == std::string_view("\x08\x06\x00\x00\x00", 5), "8-bit RGBA without interlace");
        }
        if (type == "IDAT") idat = std::string(data);
        at += 12 + length;
    }
    check((types == std::vector<std::string>{"IHDR", "sRGB", "IDAT", "IEND"}), "PNG chunk order");
    const auto raw = inflate_fixed(idat);
    const std::size_t row = std::size_t(image.extent.width) * 4;
    check(raw.size() == (row + 1) * image.extent.height, "Inflated size must cover every filtered row");
    for (std::uint32_t y = 0; y < image.extent.height; ++y) {
        check(raw[y * (row + 1)] == 0, "Rows use filter None");
        const auto begin = raw.begin() + std::ptrdiff_t(y * (row + 1) + 1);
        image.pixels.insert(image.pixels.end(), begin, begin + std::ptrdiff_t(row));
    }
    return image;
}

void png_round_trip() {
    tessera::CapturedImage image{{37, 23}, tessera::CaptureFormat::rgba8_srgb, {}};
    for (std::uint32_t y = 0; y < image.extent.height; ++y)
        for (std::uint32_t x = 0; x < image.extent.width; ++x) {
            const bool panel = x > 5 && x < 30 && y > 4 && y < 18;
            image.pixels.insert(image.pixels.end(),
                                {std::uint8_t(panel ? 240 : x * 7), std::uint8_t(panel ? 240 : y * 11),
                                 std::uint8_t(x ^ y), std::uint8_t(panel ? 255 : 128)});
        }
    const auto png = tessera::encode_png(image);
    check(png && png.diagnostics.empty(), "A complete frame must encode");
    check(decode_png(*png.value) == image, "Decoded PNG pixels must equal the captured bytes");
    check(*tessera::encode_png(image).value == *png.value, "Encoding must be deterministic");

    tessera::CapturedImage flat{
        {640, 400}, tessera::CaptureFormat::rgba8_srgb, std::vector<std::uint8_t>(std::size_t(640) * 400 * 4, 200)};
    const auto compact = tessera::encode_png(flat);
    check(compact && compact.value->size() * 50 < flat.pixels.size(), "Flat UI regions must compress");
    check(decode_png(*compact.value) == flat, "Long matches must round-trip");

    auto broken = image;
    broken.pixels.pop_back();
    check(has(tessera::encode_png(broken).diagnostics, "capture_mismatch", "/pixels"), "Short pixels must reject");
    check(has(tessera::encode_png({{0, 4}, tessera::CaptureFormat::rgba8_srgb, {}}).diagnostics, "capture_mismatch",
              "/pixels"),
          "Empty extents must reject");
}

// Every semantic role spelled by the bundle's tree must equal Semantic JSON v1.
void role_names() {
    using tessera::SemanticRole;
    for (unsigned r = 1; r <= unsigned(SemanticRole::text_editor); ++r) {
        const auto role = SemanticRole(r);
        tessera::SemanticRecord root{.node = 0, .role = SemanticRole::root};
        tessera::SemanticRecord entry{
            .node = 1, .parent = 0, .role = role, .name = "n", .name_source = tessera::NameSource::content};
        const bool edit =
            role == SemanticRole::text_input || role == SemanticRole::number_input || role == SemanticRole::text_editor;
        const bool combobox = role == SemanticRole::combobox || role == SemanticRole::color_picker;
        if (edit || combobox) entry.value = "v";
        if (combobox) entry.expanded = false;
        const auto saved = tessera::save_semantics({tessera::semantic_version, 1, 2, {root, entry}});
        check(saved.value.has_value(), "Role fixture " + std::to_string(r) + " must serialize");
        const auto json = *saved.value;
        const auto start = json.rfind("\"role\":");
        check(start != std::string::npos, "Serialized role");
        const auto open = json.find('"', start + 7);
        const auto close = json.find('"', open + 1);
        const auto name = json.substr(open + 1, close - open - 1);

        // The same role through a bundle tree line.
        tessera::ReplayRecording recording;
        recording.viewport = {10, 10};
        recording.document.root.children.emplace_back();
        recording.styles.resize(2);
        tessera::OffscreenRun run;
        run.output.generations.push_back({});
        run.output.generations.back().viewport = {10, 10};
        run.output.semantics.push_back({std::nullopt, 0, std::nullopt, {root, entry}});
        run.frames.push_back({});
        const auto bundle = tessera::build_snapshot_bundle(recording, run, {.producer = "roles"});
        check(bundle.value.has_value(), "Role bundle");
        const auto& tree = bundle.value->find("g0/tree.txt")->bytes;
        check(tree.find("\n  " + name + " \"n\"") != std::string::npos,
              "Tree role spelling must match Semantic JSON: " + name);
    }
}

tessera::UiNode button(std::string id, std::string caption, std::string action) {
    tessera::UiNode label;
    label.kind = tessera::NodeKind::text;
    label.properties["text"] = std::move(caption);
    tessera::UiNode result;
    result.id = std::move(id);
    result.properties["focusable"] = true;
    result.events["activate"] = std::move(action);
    result.children.push_back(std::move(label));
    return result;
}

std::vector<tessera::ResolvedStyle> menu_styles(std::size_t buttons) {
    std::vector<tessera::ResolvedStyle> styles(1 + buttons * 2);
    styles[0].height = tessera::Dimension::points(60);
    styles[0].overflow = tessera::Overflow::scroll;
    styles[0].padding = {8, 8, 8, 8};
    styles[0].gap = 8;
    styles[0].background = {0.05f, 0.05f, 0.05f, 1};
    for (std::size_t i = 1; i < styles.size(); i += 2) {
        styles[i].padding = {8, 8, 8, 8};
        styles[i].background = {0.2f, 0.3f, 0.5f, 1};
    }
    return styles;
}

// A scrolled menu that focuses, resizes, invokes, and then reloads with a third, hidden entry.
tessera::ReplayRecording recording() {
    tessera::ReplayRecording result;
    result.context = {{"start-game", "quit-game", "show-help"}};
    result.environment.scale = 1.25f;
    result.viewport = {200, 120};
    result.document.root.id = "menu";
    result.document.root.children = {button("start", "Start", "start-game"),
                                     button("quit", "Quit \"now\"", "quit-game")};
    result.styles = menu_styles(2);
    tessera::ReplayReload reload;
    reload.document = result.document;
    reload.document.root.children.push_back(button("help", "Help", "show-help"));
    reload.styles = menu_styles(3);
    reload.styles[5].display = tessera::Display::none;
    reload.styles[6].display = tessera::Display::none;
    result.steps = {tessera::InputEvent{0us, tessera::Scroll{{20, 20}, {0, 30}}},
                    tessera::ReplayFocus{tessera::AuthorIdTarget{"quit"}}, tessera::ReplayResize{{150.3f, 90}},
                    tessera::ReplaySemanticAction{tessera::AuthorIdTarget{"quit"}}, reload};
    return result;
}

struct SyntheticGlyphs final : tessera::GlyphRasterizer {
    std::uint64_t face_revision(tessera::FontId font) const override { return font.value == 0 ? 1 : 0; }
    tessera::Result<tessera::GlyphBitmap> rasterize(const tessera::GlyphRasterRequest& request) override {
        tessera::GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};

tessera::OffscreenRun run(const tessera::ReplayRecording& input, bool frames) {
    tessera::PlaceholderTextShaper text;
    SyntheticGlyphs raster;
    tessera::GlyphCache cache(raster);
    tessera::CpuFrameCapture capture(&cache);
    tessera::OffscreenInput offscreen{&input, &text};
    if (frames) {
        offscreen.capture = capture.backend();
        offscreen.frames = &capture;
    }
    auto result = tessera::run_offscreen(offscreen);
    check(result.value.has_value(), "Fixture recording must run");
    return std::move(*result.value);
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

void bundle_members() {
    const auto input = recording();
    const auto captured = run(input, true);
    const auto last = captured.output.generations.size() - 1;
    check(last >= 3, "Fixture must produce several generations");
    tessera::SnapshotOptions options{.producer = "snapshot-tests", .scenario = "menu", .glyphs = "synthetic-fixture"};
    options.diagnostics.push_back({"missing_name", tessera::Severity::warning, "/nodes/2", "Example warning."});
    const auto bundle = tessera::build_snapshot_bundle(input, captured, options);
    check(bundle && bundle.diagnostics.empty(), "A captured run must build a bundle");
    const auto g = "g" + std::to_string(last);
    std::vector<std::string> paths;
    for (const auto& file : bundle.value->files) paths.push_back(file.path);
    check((paths == std::vector<std::string>{"actions.json", "diagnostics.json", g + "/frame.png", g + "/layout.json",
                                             g + "/paint.json", g + "/semantics.json", g + "/tree.txt", "manifest.json",
                                             "recording.json"}),
          "Default bundle holds the last generation's members in path order");
    check(*tessera::build_snapshot_bundle(input, run(input, true), options).value == *bundle.value,
          "Equal inputs must produce byte-identical bundles");

    const auto& manifest = bundle.value->find("manifest.json")->bytes;
    for (const auto part :
         {"\"format\": \"tessera-snapshot-bundle\"", "\"version\": 1", "\"producer\": \"snapshot-tests\"",
          "\"scenario\": \"menu\"", "\"text\": \"placeholder\"", "\"text_service\": \"placeholder\"",
          "\"backend\": \"cpu-scalar\"", "\"glyphs\": \"synthetic-fixture\"", "\"scale\": 1.25",
          "\"format\": \"tessera-semantics\"", "\"format\": \"tessera-replay\"",
          "\"diagnostics\": {\"path\": \"diagnostics.json\", \"count\": 1}"})
        check(contains(manifest, part), std::string("Manifest must record ") + part);
    check(contains(manifest, "\"generation_count\": " + std::to_string(last + 1)), "Manifest counts generations");

    const auto replayed = tessera::load_replay(bundle.value->find("recording.json")->bytes);
    check(replayed && *replayed.value == input, "recording.json must reproduce the input recording");
    const auto semantics = tessera::load_semantics(bundle.value->find(g + "/semantics.json")->bytes);
    check(semantics && semantics.value->generation == last && semantics.value->node_count == 7 &&
              semantics.value->nodes == captured.output.semantics.back().nodes,
          "semantics.json must hold the generation's last settled observation over the reloaded document");

    const auto& layout = bundle.value->find(g + "/layout.json")->bytes;
    check(
        contains(layout, "\"id\": \"help\", \"displayed\": false}") &&
            contains(layout, "\"node\": 3, \"parent\": 0, \"kind\": \"box\", \"id\": \"quit\", \"displayed\": true") &&
            contains(layout, "\"scroll\": {\"extent\": "),
        "layout.json must list every preorder node with display-none entries explicit");
    const auto& paint = bundle.value->find(g + "/paint.json")->bytes;
    check(contains(paint, "\"glyph_run\": 2") && contains(paint, "\"time_dependent\": false"),
          "paint.json must summarize the generation's commands");
    const auto& tree = bundle.value->find(g + "/tree.txt")->bytes;
    check(contains(tree, "| focus #quit | scenario menu\n") &&
              contains(tree, "\n  button #quit \"Quit \\\"now\\\"\" [") && contains(tree, " focused") &&
              contains(tree, "\n    text \"Start\" [") && contains(tree, "\n  box #help display:none\n"),
          "tree.txt must show roles, ids, names, geometry, focus and display state");
    const auto& actions = bundle.value->find("actions.json")->bytes;
    check(contains(actions, "\"binding\": \"activate\", \"action\": \"quit-game\"") &&
              contains(actions, "\"id\": \"quit\""),
          "actions.json must list requested actions");
    check(contains(bundle.value->find("diagnostics.json")->bytes,
                   "\"code\": \"missing_name\", \"severity\": \"warning\""),
          "diagnostics.json must keep run warnings");
    const auto frame = decode_png(bundle.value->find(g + "/frame.png")->bytes);
    check(frame == *captured.frames[last].image, "frame.png must hold the same generation's completed pixels");

    // Every generation, and a frame-free run.
    auto all = options;
    for (std::size_t i = 0; i <= last; ++i) all.generations.push_back(i);
    const auto every = tessera::build_snapshot_bundle(input, captured, all);
    check(every && every.value->find("g0/tree.txt") && every.value->find("g0/frame.png") &&
              every.value->find("g0/layout.json")->bytes.find("\"id\": \"help\"") == std::string::npos,
          "Earlier generations must describe their own document");
    const auto core = run(input, false);
    const auto core_only =
        tessera::build_snapshot_bundle(input, core, {.producer = "snapshot-tests", .scenario = "menu"});
    check(
        core_only && !core_only.value->find(g + "/frame.png") &&
            contains(core_only.value->find("manifest.json")->bytes, "\"frame\": {\"unavailable\": \"not_declared\"}") &&
            contains(core_only.value->find("manifest.json")->bytes, "\"capture\": null"),
        "A core-only bundle must state the missing frame");
    check(core_only.value->find(g + "/tree.txt")->bytes == tree, "Derived tree must not depend on capture");
    const auto host_text = tessera::build_snapshot_bundle(input, core, {.producer = "p", .text_service = "app-cells"});
    check(contains(host_text.value->find("manifest.json")->bytes, "\"text_service\": \"app-cells\""),
          "A host-local text service must be declared");

    // Option rejection.
    check(has(tessera::build_snapshot_bundle(input, captured, {.producer = ""}).diagnostics, "invalid_bundle_option",
              "/options/producer"),
          "A producer identity is required");
    check(has(tessera::build_snapshot_bundle(input, captured, {.producer = "p"}).diagnostics, "invalid_bundle_option",
              "/options/glyphs"),
          "Captured frames require a glyph declaration");
    check(has(tessera::build_snapshot_bundle(input, core, {.producer = "p", .generations = {1, 1}}).diagnostics,
              "out_of_range", "/options/generations/1"),
          "Generations must strictly increase");
    check(has(tessera::build_snapshot_bundle(input, core, {.producer = "p", .generations = {last + 1}}).diagnostics,
              "out_of_range", "/options/generations/0"),
          "Generations must exist");
    check(has(tessera::build_snapshot_bundle(input, core, {.producer = "p", .scenario = "bad\nname"}).diagnostics,
              "invalid_bundle_option", "/options/scenario"),
          "Identities exclude controls");
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void writer() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "tessera_snapshot_bundle_tests";
    fs::remove_all(root);
    const auto input = recording();
    const auto core = run(input, false);
    auto options = tessera::SnapshotOptions{.producer = "snapshot-tests", .generations = {0, 1}};
    const auto first = *tessera::build_snapshot_bundle(input, core, options).value;
    const auto out = root / "bundle";
    check(tessera::write_snapshot_bundle(first, out).empty(), "An absent directory must be created");
    for (const auto& file : first.files) check(read(out / file.path) == file.bytes, "Written members must be exact");

    options.generations = {2};
    const auto second = *tessera::build_snapshot_bundle(input, core, options).value;
    check(tessera::write_snapshot_bundle(second, out).empty(), "An earlier bundle must be replaced");
    check(!fs::exists(out / "g0") && fs::exists(out / "g2" / "tree.txt"), "Replacement must drop stale generations");

    std::ofstream(out / "notes.txt") << "keep";
    check(has(tessera::write_snapshot_bundle(second, out), "bundle_directory_not_empty", "/notes.txt"),
          "Foreign entries must block replacement");
    check(fs::exists(out / "g2" / "tree.txt") && read(out / "notes.txt") == "keep", "A refused write changes nothing");

    const auto other = root / "other";
    fs::create_directories(other);
    std::ofstream(other / "data.bin") << "x";
    check(has(tessera::write_snapshot_bundle(second, other), "bundle_directory_not_empty", ""),
          "A non-bundle directory must be refused");
    fs::create_directories(root / "empty");
    check(tessera::write_snapshot_bundle(second, root / "empty").empty(), "An empty directory is accepted");
    fs::remove_all(root);
}

// The CLI smoke's committed recording is this fixture's Replay JSON; --write-fixture <path> regenerates it.
void fixture(const char* path) {
    const auto saved = tessera::save_replay(recording());
    check(saved.value.has_value(), "Fixture recording must serialize");
    check(read(path) == *saved.value, "tests/tools/fixtures/menu.replay.json must equal the fixture recording");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--write-fixture") {
            std::ofstream(argv[2], std::ios::binary) << *tessera::save_replay(recording()).value;
            return 0;
        }
        check(argc == 2, "Usage: tessera_snapshot_bundle_tests <menu.replay.json> | --write-fixture <path>");
        fixture(argv[1]);
        png_round_trip();
        role_names();
        bundle_members();
        writer();
        std::cout << "Snapshot bundle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
