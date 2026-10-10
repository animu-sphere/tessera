#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <string>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

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

// A scrolled two-button menu at a fractional scale, resized between generations.
tessera::ReplayRecording recording() {
    tessera::ReplayRecording result;
    result.context = {{"start-game", "quit-game"}};
    result.environment.scale = 1.25f;
    result.viewport = {200, 120};
    result.document.root.id = "menu";
    result.document.root.children = {button("start", "Start", "start-game"), button("quit", "Quit", "quit-game")};
    result.styles.resize(5);
    result.styles[0].height = tessera::Dimension::points(60);
    result.styles[0].overflow = tessera::Overflow::scroll;
    result.styles[0].padding = {8, 8, 8, 8};
    result.styles[0].gap = 8;
    result.styles[0].background = {0.05f, 0.05f, 0.05f, 1};
    for (const std::size_t i : {1, 3}) {
        result.styles[i].padding = {8, 8, 8, 8};
        result.styles[i].background = {0.2f, 0.3f, 0.5f, 1};
    }
    result.steps = {tessera::InputEvent{0us, tessera::Scroll{{20, 20}, {0, 30}}},
                    tessera::ReplayFocus{tessera::AuthorIdTarget{"quit"}},
                    tessera::ReplayResize{{150.3f, 90}},
                    tessera::ReplaySemanticAction{tessera::AuthorIdTarget{"quit"}}};
    return result;
}

const tessera::CaptureBackend declared{"fixture-backend", "fixture-device"};

// Deterministic fixture adapter: every pixel records the generation and paint command count, so association
// and extents are observable without a renderer.
class FixtureCapture final : public tessera::FrameCapture {
public:
    tessera::CaptureBackend backend() const override { return declaration; }
    tessera::Result<tessera::CapturedImage> capture(const tessera::CaptureRequest& request) override {
        requests.push_back({request.generation, request.logical_size, request.device_scale, request.extent,
                            *request.paint});
        if (fail_at && *fail_at == request.generation) return {std::nullopt, failure};
        tessera::CapturedImage image{request.extent, tessera::CaptureFormat::rgba8_srgb, {}};
        image.pixels.resize(std::size_t(request.extent.width) * request.extent.height * 4);
        for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
            image.pixels[i] = std::uint8_t(request.generation);
            image.pixels[i + 1] = std::uint8_t(request.paint->commands.size());
            image.pixels[i + 3] = 255;
        }
        if (wrong_extent) ++image.extent.width;
        return {std::move(image), warnings};
    }

    struct Request {
        std::size_t generation;
        tessera::Size logical_size;
        float device_scale;
        tessera::CaptureExtent extent;
        tessera::UiDrawList paint;
    };
    tessera::CaptureBackend declaration = declared;
    std::vector<Request> requests;
    std::optional<std::size_t> fail_at;
    std::vector<tessera::Diagnostic> failure;
    std::vector<tessera::Diagnostic> warnings;
    bool wrong_extent = false;
};

tessera::Result<tessera::OffscreenRun> run(const tessera::ReplayRecording& replay, FixtureCapture* frames) {
    tessera::PlaceholderTextShaper text;
    return tessera::run_offscreen({&replay, &text, frames ? std::optional(declared) : std::nullopt, frames});
}

void extents() {
    const auto exact = tessera::capture_extent({32, 32}, 1.25f);
    check(exact && *exact.value == tessera::CaptureExtent{40, 40}, "Exact fractional extent differs");
    const auto rounded = tessera::capture_extent({150.3f, 90}, 1.25f);
    check(rounded && *rounded.value == tessera::CaptureExtent{188, 113}, "Extent must round up per axis");
    check(has(tessera::capture_extent({32, 0}, 1).diagnostics, "out_of_range", "/logical_size/height"),
          "Zero logical size accepted");
    check(has(tessera::capture_extent({32, 32}, std::numeric_limits<float>::quiet_NaN()).diagnostics,
              "invalid_number", "/device_scale"), "Non-finite scale accepted");
    check(has(tessera::capture_extent({3e38f, 1}, 2).diagnostics, "out_of_range", "/logical_size"),
          "Extent beyond 32 bits accepted");
    check(tessera::validate(declared).empty(), "Valid backend declaration rejected");
    check(has(tessera::validate(tessera::CaptureBackend{"", "d"}), "invalid_identifier", "/backend") &&
              has(tessera::validate(tessera::CaptureBackend{"b", "\xff"}), "invalid_utf8", "/device") &&
              has(tessera::validate(tessera::CaptureBackend{"b", "d", tessera::CaptureFormat{7}}), "unknown_value",
                  "/format"),
          "Invalid backend declarations accepted");
}

// Without a declared backend the run is core-only: replay observations and frame conditions, no images.
void core_only() {
    const auto replay = recording();
    tessera::PlaceholderTextShaper text;
    const auto expected = tessera::play_replay(replay, text);
    const auto result = run(replay, nullptr);
    check(result && result.diagnostics.empty() && expected, "Core-only run rejected");
    const auto& value = *result.value;
    check(value.output == *expected.value, "Runner output differs from replay playback");
    check(value.manifest == tessera::OffscreenManifest{1, replay.environment, std::nullopt},
          "Manifest must record the recording's version and environment without a backend");
    // Initial, scroll, and resize settle generations; the focus and semantic action steps do not.
    check(value.frames.size() == 3 && value.output.generations.size() == 3, "One frame per generation expected");
    const tessera::Size sizes[]{{200, 120}, {200, 120}, {150.3f, 90}};
    const tessera::CaptureExtent pixels[]{{250, 150}, {250, 150}, {188, 113}};
    for (std::size_t g = 0; g < 3; ++g) {
        const auto& frame = value.frames[g];
        check(frame.generation == g && frame.logical_size == sizes[g] && frame.device_scale == 1.25f &&
                  frame.extent == pixels[g], "Frame conditions differ from the generation's viewport and scale");
        check(frame.status == tessera::FrameStatus::not_declared && !frame.image,
              "A core-only frame must state that capture was not declared");
    }
    check(value.output.actions.size() == 1 && value.output.actions[0].action == "quit-game",
          "Runner lost replay actions");
    // A huge viewport needs no image bytes when nothing is captured.
    auto large = replay;
    large.viewport = {9000, 9000};
    large.steps.clear();
    check(bool(run(large, nullptr)), "Core-only run applied the capture byte bound");
}

void captured() {
    auto replay = recording();
    FixtureCapture frames;
    frames.warnings = {{"slow_readback", tessera::Severity::warning, "/readback", "Fixture warning.", {}}};
    const auto result = run(replay, &frames);
    check(result && result.value->manifest.capture == declared, "Captured run rejected or lost its declaration");
    check(result.diagnostics.size() == 3 && result.diagnostics[2].path == "/generations/2/capture/readback" &&
              result.diagnostics[2].severity == tessera::Severity::warning,
          "Adapter warnings must be relocated under their generation");
    check(frames.requests.size() == 3, "One request per generation expected");
    for (std::size_t g = 0; g < 3; ++g) {
        const auto& request = frames.requests[g];
        const auto& frame = result.value->frames[g];
        check(request.generation == g && request.logical_size == frame.logical_size &&
                  request.device_scale == 1.25f && request.extent == frame.extent &&
                  request.paint == result.value->output.generations[g].paint,
              "Request must carry the generation's own paint, viewport, scale, and extent");
        check(frame.status == tessera::FrameStatus::captured && frame.image &&
                  frame.image->extent == frame.extent && frame.image->pixels[0] == g &&
                  frame.image->pixels[1] == request.paint.commands.size(),
              "Captured image is not associated with its generation");
    }
    check(result.value->output.generations[1].paint != result.value->output.generations[0].paint,
          "Scroll must change the captured paint");

    // Repeated runs and a Replay JSON v1 round trip reproduce the whole run, images included.
    FixtureCapture again;
    again.warnings = frames.warnings;
    const auto saved = tessera::save_replay(replay);
    const auto loaded = tessera::load_replay(*saved.value);
    check(saved && loaded, "Replay JSON round trip failed");
    const auto restored = run(*loaded.value, &again);
    check(restored && restored.value == result.value && restored.diagnostics == result.diagnostics,
          "Restored recording must reproduce the captured run");
}

void rejected() {
    const auto replay = recording();
    tessera::PlaceholderTextShaper text;
    FixtureCapture frames;
    check(has(tessera::run_offscreen({nullptr, &text}).diagnostics, "missing_input", "/recording") &&
              has(tessera::run_offscreen({&replay, nullptr}).diagnostics, "missing_input", "/text"),
          "Missing runner inputs accepted");
    // Declarations and adapters must agree; nothing is substituted.
    check(has(tessera::run_offscreen({&replay, &text, declared, nullptr}).diagnostics, "missing_input", "/frames"),
          "Declared backend without adapter accepted");
    check(has(tessera::run_offscreen({&replay, &text, std::nullopt, &frames}).diagnostics, "missing_input",
              "/capture"), "Undeclared adapter accepted");
    frames.declaration.device = "other-device";
    check(has(tessera::run_offscreen({&replay, &text, declared, &frames}).diagnostics, "capture_mismatch",
              "/capture"), "Mismatched backend declaration accepted");
    check(has(tessera::run_offscreen({&replay, &text, tessera::CaptureBackend{"", "d"}, &frames}).diagnostics,
              "invalid_identifier", "/capture/backend"), "Invalid declaration accepted");
    check(frames.requests.empty(), "Rejected inputs must not capture");
    frames.declaration = declared;

    // Replay diagnostics keep their recording location under /recording.
    auto invalid = replay;
    invalid.version = 2;
    check(has(run(invalid, &frames).diagnostics, "unsupported_version", "/recording/version"),
          "Replay version diagnostic not relocated");
    invalid = replay;
    invalid.steps[2] = tessera::ReplayResize{{-1, 90}};
    check(has(run(invalid, &frames).diagnostics, "out_of_range", "/recording/steps/2/viewport/width"),
          "Replay step diagnostic not relocated");
    check(frames.requests.empty(), "A failed replay must not capture");
    auto unnamed = replay;
    unnamed.document.root.children[1].children.clear();
    unnamed.styles.resize(4);
    const auto warned = run(unnamed, nullptr);
    check(warned && !warned.diagnostics.empty() && warned.diagnostics[0].code == "missing_name" &&
              warned.diagnostics[0].path == "/recording/nodes/3",
          "Replay warnings must be relocated under /recording");

    // Adapter failures, wrong images, zero extents, and the byte bound return no partial run.
    frames.fail_at = 1;
    frames.failure = {{"device_lost", tessera::Severity::error, "/device", "Fixture failure.", {}}};
    check(has(run(replay, &frames).diagnostics, "device_lost", "/generations/1/capture/device") &&
              frames.requests.size() == 2, "Adapter failure must stop at its generation");
    frames.failure.clear();
    check(has(run(replay, &frames).diagnostics, "capture_failed", "/generations/1/capture"),
          "Silent adapter failure accepted");
    frames.fail_at.reset();
    frames.wrong_extent = true;
    check(has(run(replay, &frames).diagnostics, "capture_mismatch", "/generations/0/capture"),
          "Wrong image extent accepted");
    frames.wrong_extent = false;
    auto empty = replay;
    empty.steps = {tessera::ReplayResize{{0, 90}}};
    check(has(run(empty, &frames).diagnostics, "out_of_range", "/generations/1/capture/logical_size/width"),
          "Zero-extent generation accepted for capture");
    frames.requests.clear();
    auto large = replay;
    large.viewport = {9000, 9000};
    large.environment.scale = 1;
    large.steps.clear();
    check(has(run(large, &frames).diagnostics, "out_of_range", "/generations/0/capture") && frames.requests.empty(),
          "Capture byte bound must be checked before capturing");
}

} // namespace

int main() {
    try {
        extents();
        core_only();
        captured();
        rejected();
        std::cout << "Offscreen runner core-only, declared capture association, determinism, and rejection checks "
                     "passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
