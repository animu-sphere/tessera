#pragma once

// Development tooling above tessera::core: snapshot bundle v1 members and a PNG encoder for completed frames.
// Not part of the core API; hosts and the tessera_snapshot CLI link it explicitly.
#include <tessera/inspection/offscreen_runner.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tessera {

// Snapshot bundle versioning is independent of UI document, replay, and semantic versions.
inline constexpr std::uint32_t snapshot_bundle_version = 1;
inline constexpr std::size_t max_snapshot_generations = 256;

// Host-declared bundle conditions. Identities are fixture names (1-256 UTF-8 bytes without ASCII controls).
struct SnapshotOptions {
    std::string producer;                // Tool or application that drove the run, e.g. "tessera_snapshot".
    std::optional<std::string> scenario; // Named host scenario, when the producer has one.
    // Text service the run actually used. Absent: the recording environment's declaration ("placeholder" or
    // "font-profile:<id>"). A host-local service names itself so consumers can refuse to reproduce it.
    std::optional<std::string> text_service;
    // Glyph coverage source behind captured frames, e.g. "synthetic-fixture"; required when frames exist.
    std::optional<std::string> glyphs;
    // Replay generations to emit, strictly increasing. Empty: only the last generation.
    std::vector<std::size_t> generations;
    // Warnings that accompanied the run; written to diagnostics.json in order.
    std::vector<Diagnostic> diagnostics;
};

// One bundle member: a relative '/'-separated path and its exact bytes.
struct SnapshotFile {
    std::string path;
    std::string bytes;
    bool operator==(const SnapshotFile&) const = default;
};
// Members in path order. Equal inputs produce equal bundles, byte for byte.
struct SnapshotBundle {
    std::vector<SnapshotFile> files;
    const SnapshotFile* find(std::string_view path) const;
    bool operator==(const SnapshotBundle&) const = default;
};

// Builds manifest.json, recording.json, diagnostics.json and, per selected generation g, g<g>/semantics.json
// (Semantic JSON v1), g<g>/layout.json, g<g>/paint.json, g<g>/tree.txt and g<g>/frame.png when the run
// captured it. `run` must come from run_offscreen(recording). Missing frames are recorded with their reason.
// Option errors are invalid_bundle_option at /options/...; an out-of-range generation is out_of_range at
// /options/generations/<i>.
Result<SnapshotBundle> build_snapshot_bundle(const ReplayRecording& recording, const OffscreenRun& run,
                                             const SnapshotOptions& options);

// Writes every member below `directory`. The directory must be absent, empty, or hold only members of an
// earlier snapshot bundle, which are replaced; anything else is bundle_directory_not_empty and nothing is
// changed. Filesystem failures are bundle_io.
std::vector<Diagnostic> write_snapshot_bundle(const SnapshotBundle&, const std::filesystem::path& directory);

// Deterministic PNG (8-bit RGBA, sRGB chunk, fixed-Huffman deflate) of a completed rgba8_srgb frame.
// Rejects a pixel buffer that disagrees with its extent as capture_mismatch at /pixels.
Result<std::string> encode_png(const CapturedImage&);

} // namespace tessera
