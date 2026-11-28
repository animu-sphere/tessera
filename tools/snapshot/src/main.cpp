// tessera_snapshot: captures, verifies and compares snapshot bundles without a window.
#include <tessera/inspection/semantic_diff.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/snapshot/snapshot_diff.hpp>
#include <tessera/text/glyph_cache.hpp>
#include <charconv>
#include <cmath>
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
  tessera_snapshot verify <directory>
  tessera_snapshot diff <before> <after> [--geometry-tolerance <units>] [--pixel-threshold <0-255>]
                        [--pixel-count <n>] [--out <report.json>]
  tessera_snapshot semantic-diff <before.json> <after.json> [--scope <id>] [--kind <kind>[,<kind>...]]
                                 [--out <report.json>]

capture plays a Replay JSON v1/v2 recording offscreen and writes a snapshot bundle:
  manifest.json, recording.json, actions.json, diagnostics.json and, per generation,
  g<g>/semantics.json, layout.json, paint.json, tree.txt and frame.png.
  Frames come from the scalar CPU reference renderer with synthetic placeholder glyph marks;
  --core-only captures no frames. Only placeholder text recordings are reproducible here.
verify loads a bundle and checks every member against bundle v1.
diff verifies two bundles, reports mismatched conditions and otherwise their semantic, layout,
  action, paint and pixel differences as tessera-snapshot-diff JSON on stdout (or in --out).
  Layout numbers within --geometry-tolerance logical units are equal (default 0). A pixel differs
  when a channel changes by more than --pixel-threshold (default 0), and frames differ when more
  than --pixel-count pixels differ (default 0).
semantic-diff compares two Semantic JSON v1 files, such as a bundle's g<g>/semantics.json, and
  writes what changed in meaning as tessera-semantic-diff JSON on stdout (or in --out): focus,
  added, removed and moved entries, and role, name, state, value and action changes, matched by
  author ID. --scope compares only the subtree of one author ID; --kind reports only the listed
  kinds, such as value_changed,enabled_changed.

Exit status:
  capture: 0 written, 1 rejected input or capture failure, 2 usage error.
  verify:  0 valid, 1 invalid bundle, 2 usage error.
  diff:    0 equal, 1 different, 2 invalid bundle or usage error, 3 incompatible conditions.
  semantic-diff: 0 equal, 1 different, 2 invalid input or usage error.
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

void print(const std::vector<Diagnostic>& diagnostics, std::string_view prefix = {}) {
    for (const auto& d : diagnostics) {
        std::cerr << prefix << (d.severity == Severity::error ? "error " : "warning ") << d.code << " at "
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

std::optional<Arguments> parse_capture(int argc, char** argv) {
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

template<class T> std::optional<T> parse_number(std::string_view text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
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
        const auto value = parse_number<std::size_t>(spec.substr(0, comma));
        if (!value) return std::nullopt;
        result.push_back(*value);
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

int verify(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << usage;
        return 2;
    }
    const auto loaded = verify_snapshot_bundle(argv[2]);
    print(loaded.diagnostics);
    if (!loaded) {
        std::cerr << argv[2] << " is not a valid snapshot bundle v" << snapshot_bundle_version << " ("
                  << loaded.diagnostics.size() << " problems).\n";
        return 1;
    }
    std::cout << argv[2] << " is a valid snapshot bundle v" << snapshot_bundle_version << " with generation";
    std::cout << (loaded.value->generations.size() == 1 ? "" : "s");
    for (const auto& g : loaded.value->generations) std::cout << " g" << g.generation;
    std::cout << ".\n";
    return 0;
}

int diff(int argc, char** argv) {
    std::vector<std::string> bundles;
    std::string out;
    SnapshotDiffOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool value = i + 1 < argc;
        if (arg == "--geometry-tolerance" && value) {
            const auto number = parse_number<double>(argv[++i]);
            if (!number || !std::isfinite(*number) || *number < 0) {
                std::cerr << "error: --geometry-tolerance takes a nonnegative number of logical units\n";
                return 2;
            }
            options.geometry_tolerance = *number;
        } else if (arg == "--pixel-threshold" && value) {
            const auto number = parse_number<unsigned>(argv[++i]);
            if (!number || *number > 255) {
                std::cerr << "error: --pixel-threshold takes a channel delta from 0 to 255\n";
                return 2;
            }
            options.pixel_threshold = std::uint8_t(*number);
        } else if (arg == "--pixel-count" && value) {
            const auto number = parse_number<std::uint64_t>(argv[++i]);
            if (!number) {
                std::cerr << "error: --pixel-count takes a pixel count\n";
                return 2;
            }
            options.pixel_count = *number;
        } else if (arg == "--out" && value) {
            out = argv[++i];
        } else if (!arg.starts_with("--") && bundles.size() < 2) {
            bundles.emplace_back(arg);
        } else {
            std::cerr << usage;
            return 2;
        }
    }
    if (bundles.size() != 2) {
        std::cerr << usage;
        return 2;
    }
    const auto before = verify_snapshot_bundle(bundles[0]);
    print(before.diagnostics, "before: ");
    const auto after = verify_snapshot_bundle(bundles[1]);
    print(after.diagnostics, "after: ");
    if (!before || !after) {
        std::cerr << "error: both inputs must be valid snapshot bundles\n";
        return 2;
    }
    const auto result = diff_snapshot_bundles(*before.value, *after.value, options);
    print(result.diagnostics);
    if (!result) return 2;
    const auto report = save_snapshot_diff(*result.value);
    if (out.empty()) {
        std::cout << report;
    } else {
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file << report;
        file.close();
        if (!file) {
            std::cerr << "error: cannot write " << out << '\n';
            return 2;
        }
    }
    const auto& d = *result.value;
    std::ostream& summary = out.empty() ? std::cerr : std::cout;
    if (d.status == SnapshotDiffStatus::incompatible) {
        summary << "incompatible:";
        for (const auto& c : d.conditions) summary << ' ' << c.condition;
        summary << '\n';
        return 3;
    }
    if (d.status == SnapshotDiffStatus::equal) {
        summary << "equal\n";
        return 0;
    }
    summary << "different (first: " << *d.first_difference << "): " << d.semantic.size() << " semantic, "
            << d.layout.size() << " layout, " << d.actions.size() << " action, " << d.paint.size()
            << " paint changes\n";
    return 1;
}

std::optional<SemanticSnapshot> read_semantics(const std::string& path, std::string_view label) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << label << ": error: cannot read " << path << '\n';
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || std::size_t(size) > max_serialized_semantic_bytes) {
        std::cerr << label << ": error: " << path << " exceeds the 16 MiB Semantic JSON bound\n";
        return std::nullopt;
    }
    std::string json(std::size_t(size), '\0');
    in.seekg(0);
    in.read(json.data(), size);
    auto loaded = load_semantics(json);
    print(loaded.diagnostics, std::string(label) + ": ");
    return loaded ? std::move(loaded.value) : std::nullopt;
}

std::optional<SemanticChangeKind> change_kind(std::string_view name) {
    for (std::size_t k = 0; k < semantic_change_kind_count; ++k)
        if (semantic_change_name(SemanticChangeKind(k)) == name) return SemanticChangeKind(k);
    return std::nullopt;
}

int semantic_diff(int argc, char** argv) {
    std::vector<std::string> files;
    std::string out;
    SemanticDiffOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool value = i + 1 < argc;
        if (arg == "--scope" && value) {
            options.scope = argv[++i];
        } else if (arg == "--kind" && value) {
            std::string_view spec = argv[++i];
            while (true) {
                const auto comma = spec.find(',');
                const auto kind = change_kind(spec.substr(0, comma));
                if (!kind) {
                    std::cerr << "error: unknown change kind '" << spec.substr(0, comma) << "'\n";
                    return 2;
                }
                options.kinds.push_back(*kind);
                if (comma == std::string_view::npos) break;
                spec = spec.substr(comma + 1);
            }
        } else if (arg == "--out" && value) {
            out = argv[++i];
        } else if (!arg.starts_with("--") && files.size() < 2) {
            files.emplace_back(arg);
        } else {
            std::cerr << usage;
            return 2;
        }
    }
    if (files.size() != 2) {
        std::cerr << usage;
        return 2;
    }
    const auto before = read_semantics(files[0], "before");
    const auto after = read_semantics(files[1], "after");
    if (!before || !after) {
        std::cerr << "error: both inputs must be valid Semantic JSON v" << semantic_version << '\n';
        return 2;
    }
    const auto result = diff_semantics(*before, *after, options);
    print(result.diagnostics);
    if (!result) return 2;
    const auto report = save_semantic_diff(*result.value);
    print(report.diagnostics);
    if (!report) return 2;
    if (out.empty()) {
        std::cout << *report.value;
    } else {
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file << *report.value;
        file.close();
        if (!file) {
            std::cerr << "error: cannot write " << out << '\n';
            return 2;
        }
    }
    const auto& d = *result.value;
    std::ostream& summary = out.empty() ? std::cerr : std::cout;
    if (d.empty()) {
        summary << "equal (generation " << d.from_generation() << " -> " << d.to_generation() << ")\n";
        return 0;
    }
    summary << "different (generation " << d.from_generation() << " -> " << d.to_generation()
            << "): " << d.changes().size() << " semantic changes\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view command = argc > 1 ? argv[1] : "";
        if (argc == 2 && (command == "--help" || command == "-h")) {
            std::cout << usage;
            return 0;
        }
        if (command == "verify") return verify(argc, argv);
        if (command == "diff") return diff(argc, argv);
        if (command == "semantic-diff") return semantic_diff(argc, argv);
        const auto arguments = command == "capture" ? parse_capture(argc, argv) : std::nullopt;
        if (!arguments) {
            std::cerr << usage;
            return 2;
        }
        return capture(*arguments);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        // For diff, 1 would claim a difference.
        return argc > 1 && std::string_view(argv[1]) == "capture" ? 1 : 2;
    }
}
