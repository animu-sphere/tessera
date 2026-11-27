// tessera_snapshot: captures a snapshot bundle from a Replay JSON recording without a window.
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/text/glyph_cache.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace tessera;

constexpr std::string_view usage = R"(Usage:
  tessera_snapshot capture <recording.json> --out <directory> [--generation last|all|<g>[,<g>...]] [--core-only]

Plays a Replay JSON v1/v2 recording offscreen and writes a snapshot bundle:
  manifest.json, recording.json, actions.json, diagnostics.json and, per generation,
  g<g>/semantics.json, layout.json, paint.json, tree.txt and frame.png.
Frames come from the scalar CPU reference renderer with synthetic placeholder glyph marks;
--core-only captures no frames. Only placeholder text recordings are reproducible here.

Exit status: 0 written, 1 rejected input or capture failure, 2 usage error.
)";

// Deliberately synthetic marks for placeholder glyph IDs, not a real font.
struct SyntheticGlyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};

void print(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        std::cerr << (d.severity == Severity::error ? "error " : "warning ") << d.code << " at "
                  << (d.path.empty() ? "/" : d.path);
        if (d.byte_offset) std::cerr << " (byte " << *d.byte_offset << ")";
        std::cerr << ": " << d.message << '\n';
    }
}

struct Arguments {
    std::string recording;
    std::string out;
    std::string generation = "last";
    bool core_only = false;
};

std::optional<Arguments> parse(int argc, char** argv) {
    if (argc < 3 || std::string_view(argv[1]) != "capture") return std::nullopt;
    Arguments result;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
            result.out = argv[++i];
        else if (arg == "--generation" && i + 1 < argc)
            result.generation = argv[++i];
        else if (arg == "--core-only")
            result.core_only = true;
        else if (!arg.starts_with("--") && result.recording.empty())
            result.recording = std::string(arg);
        else
            return std::nullopt;
    }
    if (result.recording.empty() || result.out.empty()) return std::nullopt;
    return result;
}

// "last" -> empty selection; "all" -> every generation; otherwise comma-separated indices.
std::optional<std::vector<std::size_t>> generations(std::string_view spec, std::size_t count) {
    std::vector<std::size_t> result;
    if (spec == "last") return result;
    if (spec == "all") {
        for (std::size_t g = 0; g < count; ++g) result.push_back(g);
        return result;
    }
    while (!spec.empty()) {
        const auto comma = spec.find(',');
        const auto token = spec.substr(0, comma);
        std::size_t value = 0;
        const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (token.empty() || ec != std::errc{} || end != token.data() + token.size()) return std::nullopt;
        result.push_back(value);
        spec = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
    }
    return result;
}

int capture(const Arguments& arguments) {
    std::ifstream in(arguments.recording, std::ios::binary);
    if (!in) {
        std::cerr << "error: cannot read " << arguments.recording << '\n';
        return 1;
    }
    std::string json;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || std::size_t(size) > max_serialized_replay_bytes) {
        std::cerr << "error: " << arguments.recording << " exceeds the 64 MiB Replay JSON bound\n";
        return 1;
    }
    json.resize(std::size_t(size));
    in.seekg(0);
    in.read(json.data(), size);
    auto recording = load_replay(json);
    print(recording.diagnostics);
    if (!recording) return 1;
    if (recording.value->environment.text.kind != ReplayText::Kind::placeholder) {
        print({{"text_service_unavailable", Severity::error, "/environment/text",
                "This CLI provides only the placeholder text service; run a font-profile recording in its host."}});
        return 1;
    }
    PlaceholderTextShaper text;
    SyntheticGlyphs raster;
    GlyphCache cache(raster);
    CpuFrameCapture frames(&cache);
    OffscreenInput input{&*recording.value, &text};
    if (!arguments.core_only) {
        input.capture = frames.backend();
        input.frames = &frames;
    }
    auto run = run_offscreen(input);
    print(run.diagnostics);
    if (!run) return 1;
    SnapshotOptions options;
    options.producer = "tessera_snapshot";
    if (!arguments.core_only) options.glyphs = "synthetic-fixture";
    options.diagnostics = run.diagnostics;
    const auto selection = generations(arguments.generation, run.value->output.generations.size());
    if (!selection) {
        std::cerr << "error: --generation takes last, all or comma-separated generation indices\n";
        return 2;
    }
    options.generations = *selection;
    auto bundle = build_snapshot_bundle(*recording.value, *run.value, options);
    print(bundle.diagnostics);
    if (!bundle) return 1;
    if (const auto written = write_snapshot_bundle(*bundle.value, arguments.out); !written.empty()) {
        print(written);
        return 1;
    }
    std::cout << "Snapshot bundle v" << snapshot_bundle_version << " written to " << arguments.out << " ("
              << run.value->output.generations.size() << " generations):\n";
    for (const auto& file : bundle.value->files)
        std::cout << "  " << file.path << " (" << file.bytes.size() << " bytes)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
            std::cout << usage;
            return 0;
        }
        const auto arguments = parse(argc, argv);
        if (!arguments) {
            std::cerr << usage;
            return 2;
        }
        return capture(*arguments);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
