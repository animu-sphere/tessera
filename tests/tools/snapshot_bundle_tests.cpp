#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/snapshot/snapshot_diff.hpp>
#include <tessera/text/glyph_cache.hpp>
#include "../check.hpp"
#include <algorithm>
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

// Independently encoded PNGs (Python zlib): split IDATs, an ancillary tEXt chunk and rows cycling through
// the Sub, Up, Average and Paeth filters, compressed with dynamic Huffman (24x10) and stored (4x3) blocks.
constexpr std::string_view dynamic_png_hex =
    "89504e470d0a1a0a0000000d49484452000000180000000a0806000000ae699e680000000e744558746e6f746500616e63696c6c61727918"
    "783624000000cb4944415478dab5d1314b020118c6f1cc74b07338ee06d1411292be40cbbbd52a3808bd2019880ee2118742a283e8100685"
    "05a143d82016849b5b9bb548cb51834424351c36880d62c36183584f05d517b8dff86eefffb1ccad2af56513cd6f420c9290822c14a00465"
    "a8400d1ad084165c421b3aa041177aa0c3004660c014acfbe7578f81801608de0583dd5028b401e170b817893c47a2ba1e7d7989c7078944"
    "42519457551d8dd26f6f69c3c864dedf73b9693e3f2d163f3e76ad56abdd6edf73380e0e9c87874ef15814abb22c990b217f000000cc4944"
    "4154bb60e1eb038a1125e95796a84054222affdd6a440d7a6a12b588fa44c3e1783c6e4f261d9a6934eb92cd4682204892a4bbdd03faff81"
    "65bbf1b0b86622f337b8eeed1ca1793882e85140f278e2bbf9aba28e54350d998c61e47e9ae78b888ee6bb48be87e40e24778aa2782c57ab"
    "f2c989cb75eaf19c7abdf5baefcce7bbf0fbfddf1b30738c39c9a9147fb9e1db42e1bec45c667ee27ea532ac313798c73c993467ad165f32"
    "db5810da92d4614d63372f2d7557567aacebfcef03fed9e0d1b99edd3291e91b7c027379c171fddaf64e0000000049454e44ae426082";
constexpr std::string_view stored_png_hex =
    "89504e470d0a1a0a0000000d4948445200000004000000030806000000b4f4aec60000000e744558746e6f746500616e63696c6c61727918"
    "7836240000001f494441547801013300ccff0100356a9f252525252525252525252525025b5b5b5b6262638fafe20000001f494441546262"
    "69696969707070700389a3bed84b4bcb4b4ece4e4ed25252526b5b1147040062660000000049454e44ae426082";

std::string unhex(std::string_view hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(char(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    return out;
}

tessera::CapturedImage pattern(std::uint32_t width, std::uint32_t height) {
    tessera::CapturedImage image{{width, height}, tessera::CaptureFormat::rgba8_srgb, {}};
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            for (std::uint32_t c = 0; c < 4; ++c)
                image.pixels.push_back(std::uint8_t((x * 37 + y * 91 + c * 53 + x * y * 7) & 255));
    return image;
}

// Rewrites a chunk's CRC after its data was changed in place.
void reseal(std::string& png, std::size_t chunk) {
    const auto length = be32(png, chunk);
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = chunk + 4; i < chunk + 8 + length; ++i) {
        crc ^= static_cast<unsigned char>(png[i]);
        for (int k = 0; k < 8; ++k) crc = crc & 1 ? 0xedb88320u ^ (crc >> 1) : crc >> 1;
    }
    crc = ~crc;
    for (int i = 0; i < 4; ++i) png[chunk + 8 + length + std::size_t(i)] = char((crc >> (24 - 8 * i)) & 0xff);
}

void png_decoding() {
    const auto dynamic = tessera::decode_png(unhex(dynamic_png_hex));
    check(dynamic && *dynamic.value == pattern(24, 10), "Dynamic Huffman, filtered rows and split IDATs must decode");
    const auto stored = tessera::decode_png(unhex(stored_png_hex));
    check(stored && *stored.value == pattern(4, 3), "Stored blocks must decode");
    const auto image = pattern(37, 23);
    const auto encoded = *tessera::encode_png(image).value;
    check(*tessera::decode_png(encoded).value == image, "The decoder must read the encoder's output");

    const auto rejects = [](std::string png, std::string_view what) {
        check(has(tessera::decode_png(png).diagnostics, "invalid_png", ""), std::string(what) + " must reject");
    };
    auto crc = encoded;
    crc[crc.size() - 20] ^= 1; // Inside the IDAT data, so its CRC no longer matches.
    rejects(crc, "A corrupted chunk");
    auto adler = encoded;
    const auto idat = adler.find("IDAT") - 4;
    adler[idat + 8 + be32(adler, idat) - 1] ^= 1; // Last Adler-32 byte.
    reseal(adler, idat);
    rejects(adler, "A wrong Adler-32");
    auto gray = encoded;
    gray[8 + 8 + 9] = 0; // IHDR color type: grayscale.
    reseal(gray, 8);
    rejects(gray, "A non-RGBA image");
    auto huge = encoded;
    for (int i = 0; i < 4; ++i) huge[16 + std::size_t(i)] = char(i == 2 ? 0x40 : i == 3 ? 0x01 : 0); // Width 16385.
    reseal(huge, 8);
    rejects(huge, "An extent above the bound");
    rejects(encoded.substr(0, encoded.size() - 12), "A missing IEND");
    rejects(encoded + "x", "Data after IEND");
    rejects("not a png", "A missing signature");
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

tessera::SnapshotBundle built(const tessera::ReplayRecording& input, bool frames,
                              std::vector<std::size_t> generations = {}) {
    tessera::SnapshotOptions options{.producer = "snapshot-tests", .generations = std::move(generations)};
    if (frames) options.glyphs = "synthetic-fixture";
    auto bundle = tessera::build_snapshot_bundle(input, run(input, frames), options);
    check(bundle.value.has_value(), "Fixture bundle must build");
    return std::move(*bundle.value);
}

tessera::LoadedSnapshotBundle loaded(const tessera::SnapshotBundle& bundle) {
    auto result = tessera::load_snapshot_bundle(bundle);
    for (const auto& d : result.diagnostics) std::cerr << d.code << " at " << d.path << ": " << d.message << '\n';
    check(result && result.diagnostics.empty(), "A built bundle must load");
    return std::move(*result.value);
}

// A copy of `bundle` whose member at `path` has its first `from` replaced by `to`.
tessera::SnapshotBundle edited(tessera::SnapshotBundle bundle, std::string_view path, std::string_view from,
                               std::string_view to) {
    for (auto& file : bundle.files)
        if (file.path == path) {
            const auto at = file.bytes.find(from);
            check(at != std::string::npos, "Edit target must exist in " + std::string(path));
            file.bytes.replace(at, from.size(), to);
        }
    return bundle;
}

std::vector<tessera::Diagnostic> problems(const tessera::SnapshotBundle& bundle) {
    const auto result = tessera::load_snapshot_bundle(bundle);
    check(!result.value, "A damaged bundle must not load");
    return result.diagnostics;
}

void bundle_loading() {
    const auto input = recording();
    const auto captured = run(input, true);
    const auto last = captured.output.generations.size() - 1;
    const auto g = "g" + std::to_string(last);
    const auto every = built(input, true, {0, 1, 2, last});
    const auto bundle = loaded(every);
    check(bundle.recording == input && bundle.generations.size() == 4 && bundle.generations.back().generation == last,
          "Loading must keep the recording and every listed generation");
    check(bundle.generations.back().frame == captured.frames[last].image && bundle.generations[0].semantics &&
              std::get<double>(
                  std::get<tessera::JsonValue::Object>(bundle.generations.back().paint.value).at("commands").value) ==
                  double(captured.output.generations[last].paint.commands.size()),
          "Loaded members must hold the generation's frame, semantics and paint summary");
    const auto core = loaded(built(input, false));
    check(!core.generations[0].frame && core.generations[0].semantics, "A core-only bundle loads without frames");

    // The same bundle through the filesystem.
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "tessera_snapshot_verify_tests";
    fs::remove_all(root);
    check(tessera::write_snapshot_bundle(every, root / "bundle").empty(), "Bundle must write");
    const auto read = tessera::read_snapshot_bundle(root / "bundle");
    check(read && read.diagnostics.empty() && *read.value == every, "Reading must return the written members");
    check(tessera::verify_snapshot_bundle(root / "bundle").value.has_value(), "A written bundle must verify");
    fs::create_directories(root / "bundle" / g / "nested");
    const auto nested = tessera::verify_snapshot_bundle(root / "bundle");
    check(!nested && has(nested.diagnostics, "unlisted_member", "/" + g + "/nested"), "Nested directories must reject");
    check(has(tessera::verify_snapshot_bundle(root / "absent").diagnostics, "bundle_io", ""),
          "A missing directory is bundle_io");
    fs::remove_all(root);

    // Versions, members and schemas.
    check(has(problems(edited(every, "manifest.json", "\"version\": 1", "\"version\": 2")), "unsupported_version",
              "/manifest.json/version"),
          "An unsupported bundle version must reject");
    check(has(problems(edited(every, g + "/layout.json", "\"version\": 1", "\"version\": 7")), "unsupported_version",
              "/" + g + "/layout.json/version"),
          "An unsupported member version must reject");
    check(has(problems(edited(every, "manifest.json", "\"format\": \"tessera-paint-summary\", \"version\": 1",
                              "\"format\": \"tessera-paint-summary\", \"version\": 3")),
              "unsupported_version", "/manifest.json/generations/0/members/paint/version"),
          "An unsupported listed member version must reject");
    auto missing = every;
    std::erase_if(missing.files, [&](const auto& file) { return file.path == g + "/paint.json"; });
    check(has(problems(missing), "missing_member", "/" + g + "/paint.json"), "A listed member must exist");
    auto extra = every;
    extra.files.push_back({"notes.txt", "x"});
    extra.files.push_back({"g1/frame.png.bak", "x"});
    const auto unlisted = problems(extra);
    check(has(unlisted, "unlisted_member", "/notes.txt") && has(unlisted, "unlisted_member", "/g1/frame.png.bak"),
          "Unlisted members must reject");
    const auto truncated = problems(edited(every, g + "/layout.json", "\"nodes\": [", "\"nodes\": [[,"));
    check(has(truncated, "invalid_member", "/" + g + "/layout.json"), "Unparsable JSON must reject");
    check(std::any_of(truncated.begin(), truncated.end(), [](const auto& d) { return d.byte_offset.has_value(); }),
          "Parse failures keep their byte offset");
    check(has(problems(edited(every, "recording.json", "\"version\":1,\"viewport\"", "\"version\":9,\"viewport\"")),
              "unsupported_version", "/recording.json/version"),
          "Replay JSON diagnostics must be relocated below recording.json");
    check(has(problems(edited(every, g + "/semantics.json", "\"generation\":\"" + std::to_string(last) + "\"",
                              "\"generation\":\"0\"")),
              "invalid_member", "/" + g + "/semantics.json/generation"),
          "Semantic JSON must belong to its generation");
    check(has(problems(edited(every, "manifest.json", "\"viewport\": [150.3, 90]", "\"viewport\": [150, 90]")),
              "invalid_member", "/g2/layout.json/viewport"),
          "Layout must agree with the manifest viewport");
    check(has(problems(edited(every, g + "/paint.json", "\"rect\": ", "\"rect\": 1")), "invalid_member",
              "/" + g + "/paint.json/kinds"),
          "Paint kinds must add up to the command count");
    check(has(problems(edited(every, g + "/layout.json", "\"id\": \"quit\"", "\"id\": \"start\"")), "invalid_member",
              "/" + g + "/layout.json/nodes/3/id"),
          "Author IDs are unique in a layout");
    check(has(problems(edited(every, "manifest.json", "\"producer\": \"snapshot-tests\"",
                              "\"producer\": \"snapshot-tests\", \"comment\": 1")),
              "invalid_member", "/manifest.json/comment"),
          "Unknown manifest fields must reject");
    check(has(problems(edited(every, "actions.json", "\"binding\": \"activate\", ", "")), "invalid_member",
              "/actions.json/actions/0/binding"),
          "Actions need every field");
    auto frame = every;
    for (auto& file : frame.files)
        if (file.path == g + "/frame.png") file.bytes[file.bytes.size() - 20] ^= 1;
    check(has(problems(frame), "invalid_png", "/" + g + "/frame.png"), "PNG diagnostics are relocated to the frame");
    auto empty = every;
    for (auto& file : empty.files)
        if (file.path == "manifest.json") file.bytes = std::string(5u << 20, ' ');
    check(has(problems(empty), "bundle_limit", "/manifest.json"), "Members are bounded");
}

// Layer counts appear in paint.json only when a generation paints layers, and load like the other kinds.
void layer_summaries() {
    const tessera::SnapshotOptions options{.producer = "snapshot-tests", .scenario = "menu"};
    const auto plain = recording();
    const auto unlayered = tessera::build_snapshot_bundle(plain, run(plain, false), options);
    check(bool(unlayered), "A run without layers must build a bundle");
    for (const auto& file : unlayered.value->files)
        check(!contains(file.bytes, "push_layer"), "Summaries without layers keep their bytes");
    auto input = plain;
    // The last generation shows the reloaded styles.
    for (auto* styles : {&input.styles, &std::get<tessera::ReplayReload>(input.steps.back()).styles}) {
        (*styles)[1].composition = tessera::Composition::layer;
        (*styles)[1].opacity = .5f;
    }
    const auto played = run(input, false);
    const auto bundle = tessera::build_snapshot_bundle(input, played, options);
    check(bool(bundle), "A layered run must build a bundle");
    const auto g = "g" + std::to_string(played.output.generations.size() - 1);
    check(contains(bundle.value->find(g + "/paint.json")->bytes, "\"push_layer\": 1, \"pop_layer\": 1"),
          "paint.json must count layers when present");
    loaded(*bundle.value);
    check(has(problems(edited(*bundle.value, g + "/paint.json", "\"push_layer\": 1", "\"push_layer\": 2")),
              "invalid_member", "/" + g + "/paint.json/kinds"),
          "Layer counts must add up with the other kinds");
}

tessera::ReplayRecording with_caption(std::string_view id, std::string caption) {
    auto result = recording();
    for (auto* document : {&result.document, &std::get<tessera::ReplayReload>(result.steps.back()).document})
        for (auto& child : document->root.children)
            if (child.id == id) child.children[0].properties["text"] = caption;
    return result;
}

const tessera::SnapshotNodeChange* change(const std::vector<tessera::SnapshotNodeChange>& changes,
                                          const tessera::SnapshotNodeKey& key) {
    for (const auto& c : changes)
        if (c.node == key) return &c;
    return nullptr;
}
bool field(const tessera::SnapshotNodeChange* c, std::string_view name) {
    return c && std::any_of(c->fields.begin(), c->fields.end(), [&](const auto& f) { return f.field == name; });
}

void bundle_diffs() {
    const auto input = recording();
    const auto base = loaded(built(input, true));
    const auto same = tessera::diff_snapshot_bundles(base, loaded(built(input, true)));
    check(same && same.value->status == tessera::SnapshotDiffStatus::equal && !same.value->first_difference &&
              same.value->pixels.size() == 1 && same.value->pixels[0].compared > 0 && !same.value->pixels[0].different,
          "Equal bundles compare equal, frames included");
    const auto all = loaded(built(input, true, {0, 1, 2, 3}));
    const auto paired = tessera::diff_snapshot_bundles(all, all);
    check(paired.value->pairs.size() == 4 && paired.value->status == tessera::SnapshotDiffStatus::equal,
          "Equal generation lists pair every generation");
    const auto last = tessera::diff_snapshot_bundles(all, base);
    check(last.value->pairs.size() == 1 && last.value->pairs[0].before == 3 && last.value->pairs[0].after == 3,
          "Different generation lists pair their last generations");

    // A renamed button changes semantics first, then its text layout and pixels.
    const auto renamed = loaded(built(with_caption("quit", "Exit"), true));
    const auto rename = tessera::diff_snapshot_bundles(base, renamed);
    check(rename.value->status == tessera::SnapshotDiffStatus::different &&
              rename.value->first_difference == "semantic",
          "A name change is a semantic difference first");
    const auto* quit = change(rename.value->semantic, {.id = "quit"});
    check(quit && quit->change == tessera::SnapshotChange::changed && field(quit, "name") && quit->fields.size() == 1,
          "The semantic change names only the changed field of #quit");
    check(rename.value->paint.size() == 1 && rename.value->paint[0].fields.size() == 1 &&
              rename.value->paint[0].fields[0].field == "glyphs" && !rename.value->pixels[0].different,
          "The scrolled-out caption changes the glyph count but no pixels");
    const auto json = tessera::save_snapshot_diff(*rename.value);
    check(contains(json, "\"format\": \"tessera-snapshot-diff\"") && contains(json, "\"status\": \"different\"") &&
              contains(json, "\"node\": {\"id\": \"quit\"}") &&
              contains(json, "\"name\": {\"after\": \"Exit\", \"before\": \"Quit \\\"now\\\"\"}"),
          "The report spells node keys and field changes");

    // Unnamed nodes match within their nearest identified ancestor, so an inserted entry leaves others intact.
    auto inserted_input = input;
    for (auto* document :
         {&inserted_input.document, &std::get<tessera::ReplayReload>(inserted_input.steps.back()).document})
        document->root.children.insert(document->root.children.begin(), button("intro", "Intro", "show-help"));
    inserted_input.styles = menu_styles(3);
    auto& reload = std::get<tessera::ReplayReload>(inserted_input.steps.back());
    reload.styles = menu_styles(4);
    reload.styles[7].display = tessera::Display::none;
    reload.styles[8].display = tessera::Display::none;
    const auto inserted = tessera::diff_snapshot_bundles(base, loaded(built(inserted_input, true)));
    const auto* intro = change(inserted.value->layout, {.id = "intro"});
    const auto* intro_text = change(inserted.value->layout, {.within = "intro"});
    const auto* start_text = change(inserted.value->layout, {.within = "start"});
    check(intro && intro->change == tessera::SnapshotChange::added && !intro->before && intro->after == 1u &&
              intro_text && intro_text->change == tessera::SnapshotChange::added &&
              (!start_text || start_text->change == tessera::SnapshotChange::changed),
          "Inserted nodes are added and unnamed nodes keep their scoped identity");
    const auto* intro_entry = change(inserted.value->semantic, {.id = "intro"});
    check(intro_entry && intro_entry->change == tessera::SnapshotChange::added,
          "The inserted button is a semantic addition");

    // Geometry within tolerance is equal; the remaining difference is reported at the next level.
    auto padded_input = input;
    padded_input.styles[0].padding = {8.25f, 8, 8, 8};
    std::get<tessera::ReplayReload>(padded_input.steps.back()).styles[0].padding = {8.25f, 8, 8, 8};
    const auto padded = loaded(built(padded_input, true));
    const auto strict = tessera::diff_snapshot_bundles(base, padded);
    check(strict.value->first_difference == "layout" && strict.value->semantic.empty(),
          "A geometry change without semantic change is a layout difference");
    const auto tolerant = tessera::diff_snapshot_bundles(base, padded, {.geometry_tolerance = 0.5});
    check(tolerant.value->layout.empty() && tolerant.value->first_difference != "layout",
          "Geometry within the tolerance is equal");
    const auto forgiving =
        tessera::diff_snapshot_bundles(base, padded, {.geometry_tolerance = 0.5, .pixel_threshold = 255});
    check(forgiving.value->status == tessera::SnapshotDiffStatus::equal && !forgiving.value->pixels[0].different,
          "A pixel threshold of 255 accepts every pixel");

    // Only pixels change with a background color.
    auto tinted_input = input;
    tinted_input.styles[0].background = {0.1f, 0.05f, 0.05f, 1};
    std::get<tessera::ReplayReload>(tinted_input.steps.back()).styles[0].background = {0.1f, 0.05f, 0.05f, 1};
    const auto tinted = loaded(built(tinted_input, true));
    const auto tint = tessera::diff_snapshot_bundles(base, tinted);
    const auto& tint_pixels = tint.value->pixels[0];
    check(tint.value->first_difference == "pixels" && tint_pixels.different && tint_pixels.bounds &&
              tint_pixels.differing <= tint_pixels.compared && tint_pixels.max_delta > 0,
          "A color change is a pixel difference with its bounds");
    const auto counted = tessera::diff_snapshot_bundles(base, tinted, {.pixel_count = tint_pixels.differing});
    check(counted.value->status == tessera::SnapshotDiffStatus::equal && counted.value->pixels[0].differing > 0,
          "Differing pixels up to the count are tolerated but still counted");
    const auto threshold =
        tessera::diff_snapshot_bundles(base, tinted, {.pixel_threshold = std::uint8_t(tint_pixels.max_delta - 1)});
    check(threshold.value->pixels[0].different, "Deltas above the threshold differ");

    // Requested actions compare by position.
    auto action_input = input;
    std::get<tessera::ReplaySemanticAction>(action_input.steps[3]).target = tessera::AuthorIdTarget{"start"};
    const auto acted = tessera::diff_snapshot_bundles(base, loaded(built(action_input, true)));
    check(acted.value->first_difference == "actions" && acted.value->actions.size() == 1 &&
              acted.value->actions[0].change == tessera::SnapshotChange::changed,
          "A different requested action is an action difference");

    // Conditions are reported before judging.
    const auto core = tessera::diff_snapshot_bundles(base, loaded(built(input, false)));
    check(core.value->status == tessera::SnapshotDiffStatus::incompatible && core.value->semantic.empty() &&
              core.value->conditions.size() == 2 && core.value->conditions[0].condition == "capture_backend" &&
              core.value->conditions[1].condition == "glyphs",
          "Different capture conditions are incompatible");
    check(contains(tessera::save_snapshot_diff(*core.value), "\"semantic\": null"), "Unjudged levels are null");
    auto resized_input = input;
    std::get<tessera::ReplayResize>(resized_input.steps[2]).viewport = {160, 90};
    const auto resized = tessera::diff_snapshot_bundles(base, loaded(built(resized_input, true)));
    check(resized.value->status == tessera::SnapshotDiffStatus::incompatible &&
              resized.value->conditions[0].condition == "viewport" && resized.value->conditions[0].pair == 0u,
          "A different viewport is incompatible");
    auto scaled_input = input;
    scaled_input.environment.scale = 2;
    const auto scaled = tessera::diff_snapshot_bundles(base, loaded(built(scaled_input, true)));
    check(scaled.value->conditions[0].condition == "environment", "A different environment is incompatible");
    check(has(tessera::diff_snapshot_bundles(base, base, {.geometry_tolerance = -1}).diagnostics, "invalid_diff_option",
              "/options/geometry_tolerance"),
          "Negative tolerances reject");
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
        png_decoding();
        role_names();
        bundle_members();
        writer();
        bundle_loading();
        layer_summaries();
        bundle_diffs();
        std::cout << "Snapshot bundle checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
