#include <tessera/inspection/offscreen_runner.hpp>
#include <string>
#include <utility>

namespace tessera {
namespace {

void relocate(std::vector<Diagnostic>& out, std::vector<Diagnostic> diagnostics, const std::string& prefix) {
    for (auto& diagnostic : diagnostics) {
        diagnostic.path = prefix + diagnostic.path;
        out.push_back(std::move(diagnostic));
    }
}

// Viewport in effect for each generation: the recorded one, changed by every resize up to its producing step.
std::vector<Size> viewports(const ReplayRecording& recording, const ReplayOutput& output) {
    std::vector<Size> result;
    result.reserve(output.generations.size());
    auto viewport = recording.viewport;
    std::size_t next = 0;
    for (const auto& generation : output.generations) {
        for (; generation.step && next <= *generation.step; ++next) {
            if (const auto* resize = std::get_if<ReplayResize>(&recording.steps[next])) viewport = resize->viewport;
        }
        result.push_back(viewport);
    }
    return result;
}

} // namespace

std::vector<Diagnostic> validate(const OffscreenInput& input) {
    std::vector<Diagnostic> errors;
    const auto missing = [&](const char* path, const char* message) {
        errors.push_back({"missing_input", Severity::error, path, message, {}});
    };
    if (!input.recording) missing("/recording", "Supply the replay recording to run.");
    if (!input.text) missing("/text", "Supply the text service the recording's environment declares.");
    if (input.capture) {
        relocate(errors, validate(*input.capture), "/capture");
        if (!input.frames) missing("/frames", "Supply the adapter for the declared capture backend; none is substituted.");
        else if (input.frames->backend() != *input.capture)
            errors.push_back({"capture_mismatch", Severity::error, "/capture",
                              "The adapter's backend configuration differs from the declared one.", {}});
    } else if (input.frames) {
        missing("/capture", "Declare the capture backend configuration of the supplied adapter.");
    }
    return errors;
}

Result<OffscreenRun> run_offscreen(const OffscreenInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto& recording = *input.recording;
    auto played = play_replay(recording, *input.text);
    std::vector<Diagnostic> warnings;
    relocate(played.value ? warnings : errors, std::move(played.diagnostics), "/recording");
    if (!played) return {std::nullopt, std::move(errors)};

    OffscreenRun run{{recording.version, recording.environment, input.capture}, std::move(*played.value), {}};
    const auto scale = recording.environment.scale;
    const auto sizes = viewports(recording, run.output);
    std::size_t bytes = 0;
    run.frames.reserve(sizes.size());
    for (std::size_t g = 0; g < sizes.size(); ++g) {
        const auto at = "/generations/" + std::to_string(g) + "/capture";
        OffscreenFrame frame{g, sizes[g], scale, {}, FrameStatus::not_declared, std::nullopt};
        auto extent = capture_extent(frame.logical_size, scale);
        if (!extent) {
            relocate(errors, std::move(extent.diagnostics), at);
            return {std::nullopt, std::move(errors)};
        }
        frame.extent = *extent.value;
        if (input.capture) {
            const auto size = std::size_t(frame.extent.width) * frame.extent.height * 4;
            if (size > max_capture_bytes - bytes) {
                errors.push_back({"out_of_range", Severity::error, at,
                                  "Captured images would exceed " + std::to_string(max_capture_bytes) +
                                      " bytes; capture fewer or smaller generations.", {}});
                return {std::nullopt, std::move(errors)};
            }
            auto image = input.frames->capture({g, frame.logical_size, scale, frame.extent,
                                                &run.output.generations[g].paint});
            if (!image) {
                if (image.diagnostics.empty())
                    image.diagnostics.push_back({"capture_failed", Severity::error, "",
                                                 "The capture adapter returned no image.", {}});
                relocate(errors, std::move(image.diagnostics), at);
                return {std::nullopt, std::move(errors)};
            }
            if (image.value->extent != frame.extent || image.value->format != input.capture->format ||
                image.value->pixels.size() != size) {
                errors.push_back({"capture_mismatch", Severity::error, at,
                                  "The captured image must have the requested extent and declared format, with " +
                                      std::to_string(size) + " pixel bytes.", {}});
                return {std::nullopt, std::move(errors)};
            }
            relocate(warnings, std::move(image.diagnostics), at);
            bytes += size;
            frame.status = FrameStatus::captured;
            frame.image = std::move(*image.value);
        }
        run.frames.push_back(std::move(frame));
    }
    return {std::move(run), std::move(warnings)};
}

} // namespace tessera
