#include "panel.hpp"
#include <charconv>
#include <cmath>
#include <set>

namespace diagnostics {
using namespace tessera;
namespace ui = tessera::authoring;
namespace {
std::string number(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return {buffer, result.ptr};
}
ui::Element row(std::string id, std::string label, std::string value) {
    return ui::Row({ui::Text(std::move(label), {.style = {.width = Dimension::points(200)}}),
        ui::Text(std::move(value), {.id = "value-" + id, .style = {.width = Dimension::points(484)}})},
        {.id = id, .style = {.gap = 12.f}});
}
std::string timing_text(const Timing& timing) {
    if (!timing.milliseconds) return timing.unavailable;
    const auto& v = *timing.milliseconds;
    return number(v.mean) + " / " + number(v.p95) + " / " + number(v.maximum) + " / " + number(v.stddev) + " ms";
}
ui::Element history(const Snapshot& snapshot) {
    std::vector<ui::Element> bars;
    double minimum = 0, maximum = 0, mean = 0;
    for (const auto& sample : snapshot.history) {
        if (bars.empty()) minimum = sample.milliseconds;
        minimum = std::min(minimum, sample.milliseconds);
        maximum = std::max(maximum, sample.milliseconds);
        // Online mean avoids overflowing the sum of finite nonnegative samples.
        mean += (sample.milliseconds - mean) / (bars.size() + 1);
        bars.push_back(ui::Column({}, {.id = "history-frame-" + std::to_string(sample.frame),
            .style = {.width = Dimension::points(6),
                .height = Dimension::points(static_cast<float>(64 * (std::min(sample.milliseconds, 40.0) / 40))),
                .background = Color{0.2f, 0.6f, 0.8f, 1}}}));
    }
    const auto caption = snapshot.history.empty() ? "History unavailable: no samples" :
        std::to_string(snapshot.history.size()) + " samples; frames " + std::to_string(snapshot.history.front().frame) +
        "-" + std::to_string(snapshot.history.back().frame) + "; min / max / mean " +
        number(minimum) + " / " + number(maximum) + " / " + number(mean) + " ms";
    return ui::Column({ui::Text(caption, {.id = "history-summary"}),
        ui::Row(std::move(bars), {.id = "history-bars", .style = {.align = Align::end,
            .width = Dimension::points(254), .height = Dimension::points(64), .gap = 2.f}})},
        {.id = "history", .style = {.gap = 12.f}});
}
}

std::vector<Diagnostic> validate(const Snapshot& snapshot) {
    std::vector<Diagnostic> errors;
    auto reject = [&](std::string path, std::string message) {
        errors.push_back({"telemetry_snapshot", Severity::error, std::move(path), std::move(message)});
    };
    auto text = [&](const std::string& value, const std::string& path, std::size_t bound, bool empty = false) {
        if ((!empty && value.empty()) || value.size() > bound || value.find_first_of("\r\n") != std::string::npos)
            reject(path, "Expected bounded single-line text (maximum " + std::to_string(bound) + " bytes)");
        else if (!validate_text_input(value, {}).empty()) reject(path, "Expected valid UTF-8 text");
    };
    auto numeric = [&](double value, const std::string& path) {
        if (!std::isfinite(value) || value < 0) reject(path, "Expected a finite nonnegative measurement");
    };
    text(snapshot.scene, "/scene", 64); text(snapshot.device, "/device", 64);
    text(snapshot.presentation, "/presentation", 48);
    if (!snapshot.width || !snapshot.height) reject("/extent", "Width and height must be nonzero");
    numeric(snapshot.frame_ms, "/frame_ms"); numeric(snapshot.fps, "/fps"); numeric(snapshot.p99_ms, "/p99_ms");
    auto records = [&](const auto& rows, const std::string& group) {
        if (rows.size() > 32) { reject("/" + group, "At most 32 rows are allowed"); return; }
        std::set<std::string> names;
        for (const auto& record : rows) {
            const auto path = "/" + group + "/" + record.id;
            if (record.id.empty() || record.id.size() > 48 ||
                record.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
                reject(path + "/id", "Expected a stable lowercase ASCII row identity (maximum 48 bytes)");
            if (!names.insert(record.id).second) reject(path + "/id", "Duplicate row identity: " + record.id);
            text(record.label, path + "/label", 25);
        }
    };
    records(snapshot.cpu, "cpu"); records(snapshot.gpu, "gpu"); records(snapshot.counters, "counter");
    for (const auto& [rows, group] : {std::pair{&snapshot.cpu, "cpu"}, std::pair{&snapshot.gpu, "gpu"}}) {
        if (rows->size() > 32) continue;
        for (const auto& timing : *rows) {
            const auto path = "/" + std::string(group) + "/" + timing.id;
            if (timing.milliseconds) {
                const auto& v = *timing.milliseconds;
                numeric(v.mean, path + "/mean_ms"); numeric(v.p95, path + "/p95_ms");
                numeric(v.maximum, path + "/max_ms"); numeric(v.stddev, path + "/stddev_ms");
                if (!timing.unavailable.empty()) reject(path + "/unavailable", "Available timing must not carry an unavailable reason");
            } else text(timing.unavailable, path + "/unavailable", 48);
        }
    }
    if (snapshot.counters.size() <= 32)
        for (const auto& counter : snapshot.counters) text(counter.unit, "/counter/" + counter.id + "/unit", 16);
    if (snapshot.history.size() > 32) reject("/history", "At most 32 history samples are allowed");
    else {
        std::uint64_t previous = 0;
        for (const auto& sample : snapshot.history) {
            const auto path = "/history/" + std::to_string(sample.frame);
            if (!sample.frame || sample.frame <= previous) reject(path + "/frame", "Frame IDs must be unique, nonzero and oldest first");
            previous = sample.frame;
            numeric(sample.milliseconds, path + "/ms");
        }
    }
    return errors;
}

ui::Element Panel(const Snapshot& snapshot, const Disclosure& disclosure) {
    std::vector<ui::Element> rows{
        ui::Text("Renderer diagnostics", {.id = "heading"}),
        row("scene", "Scene", snapshot.scene), row("device", "Device", snapshot.device),
        row("extent", "Extent", std::to_string(snapshot.width) + " x " + std::to_string(snapshot.height)),
        row("presentation", "Presentation", snapshot.presentation),
        row("frame-summary", "Frame / FPS / p99", number(snapshot.frame_ms) + " ms / " + number(snapshot.fps) +
            " FPS / " + number(snapshot.p99_ms) + " ms"), history(snapshot)};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        rows.push_back(ui::Button(std::string(groups[i]) + (disclosure.expanded[i] ? " (expanded)" : " (collapsed)"), actions[i],
            {.node = {.id = "disclosure-" + std::string(groups[i]), .classes = {"disclosure"}}}));
        if (!disclosure.expanded[i]) continue;
        if (i < 2) {
            rows.push_back(ui::Text("mean / p95 / max / stddev (ms)", {.id = "units-" + std::string(groups[i])}));
            for (const auto& timing : i == 0 ? snapshot.cpu : snapshot.gpu)
                rows.push_back(row(std::string(groups[i]) + "-" + timing.id, timing.label, timing_text(timing)));
        } else for (const auto& counter : snapshot.counters)
            rows.push_back(row("counter-" + counter.id, counter.label, std::to_string(counter.value) + " " + counter.unit));
    }
    return ui::Column(std::move(rows), {.id = "renderer-diagnostics", .classes = {"panel"}});
}

StyleSheet panel_styles() {
    return {{{StyleSelector::of_class("panel"), {.overflow = Overflow::scroll,
        .padding = Edges{12, 12, 12, 12}, .gap = 12.f, .background = Color{0.95f, 0.95f, 0.98f, 1}}},
        {StyleSelector::of_class("disclosure"), {.padding = Edges{4, 8, 4, 8}, .background = Color{0.75f, 0.85f, 0.95f, 1}}}}};
}
} // namespace diagnostics
