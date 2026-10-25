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

std::string canonical(double value) {
    if (value == 0) return "0";
    char buffer[1100]; // Fixed notation also covers the smallest finite binary64.
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
    return {buffer, result.ptr};
}
Result<double> parse_numeric(std::string_view value) {
    auto invalid = []() -> Result<double> { return {std::nullopt, {{"direct_value", Severity::error,
        "/controls/direct", "Expected decimal -?(0|[1-9][0-9]*)(.[0-9]+)? in inclusive [0,4]"}}}; };
    if (value.size() > 32) return invalid();
    std::size_t i = value.starts_with('-') ? 1 : 0;
    if (i == value.size()) return invalid();
    if (value[i] == '0') ++i;
    else {
        if (value[i] < '1' || value[i] > '9') return invalid();
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
    }
    if (i < value.size() && value[i] == '.') {
        const auto start = ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
        if (i == start) return invalid();
    }
    double parsed = 0;
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (i != value.size() || converted.ec != std::errc{} || converted.ptr != value.data() + value.size() ||
        !std::isfinite(parsed) || parsed < 0 || parsed > 4) return invalid();
    return {parsed == 0 ? 0 : parsed, {}};
}
ui::Element NumericControl(const Numeric& value) {
    return ui::Column({
        ui::TextInput("Direct strength", value.draft, "direct.edit", {.node = {.id = "direct-input"}}),
        ui::Row({ui::Button("Decrease direct strength", "direct.decrease", {.node = {.id = "direct-decrease"}}),
            ui::Button("Increase direct strength", "direct.increase", {.node = {.id = "direct-increase"}})}, {.style = {.gap = 12.f}}),
        ui::Row({ui::Button("Apply direct strength", "direct.apply", {.node = {.id = "direct-apply"}}),
            ui::Button("Cancel direct strength", "direct.cancel", {.node = {.id = "direct-cancel"}})}, {.style = {.gap = 12.f}}),
        ui::Text("Accepted direct strength: " + canonical(value.accepted), {.id = "direct-accepted"})},
        {.id = "direct-control", .style = {.gap = 12.f}});
}
ui::Element MsaaControl(const Msaa& value) {
    std::vector<ui::Element> choices;
    for (std::size_t i = 0; i < sample_counts.size(); ++i) {
        const auto samples = std::to_string(sample_counts[i]);
        choices.push_back(ui::Button("MSAA " + samples + (value.accepted == sample_counts[i] ? " (selected)" : ""),
            msaa_actions[i], {.node = {.id = "msaa-" + samples}}));
    }
    return ui::Column({ui::Text("MSAA", {.id = "msaa-label"}),
        ui::Row(std::move(choices), {.style = {.gap = 12.f}}),
        ui::Text("Accepted MSAA: " + std::to_string(value.accepted), {.id = "msaa-accepted"})},
        {.id = "msaa-control", .style = {.gap = 12.f}});
}
ui::Element Panel(const Snapshot& snapshot, const Disclosure& disclosure, const Numeric& numeric, const Msaa& msaa,
    const Outlines& outlines, const SceneLights& scene_lights) {
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
        } else if (i == 2) for (const auto& counter : snapshot.counters)
            rows.push_back(row("counter-" + counter.id, counter.label, std::to_string(counter.value) + " " + counter.unit));
        else {
            rows.push_back(NumericControl(numeric));
            rows.push_back(MsaaControl(msaa));
            rows.push_back(ui::Checkbox("Outlines", outlines.accepted, "outlines.toggle",
                {.node = {.id = "outlines-toggle"}}));
            rows.push_back(ui::Checkbox("Scene lights", scene_lights.accepted, "scene_lights.toggle",
                {.node = {.id = "scene-lights-toggle"}}));
        }
    }
    return ui::Column(std::move(rows), {.id = "renderer-diagnostics", .classes = {"panel"}});
}

StyleSheet panel_styles() {
    return {{{StyleSelector::of_class("panel"), {.overflow = Overflow::scroll,
        .padding = Edges{12, 12, 12, 12}, .gap = 12.f, .background = Color{0.95f, 0.95f, 0.98f, 1}}},
        {StyleSelector::of_class("disclosure"), {.padding = Edges{4, 8, 4, 8}, .background = Color{0.75f, 0.85f, 0.95f, 1}}}}};
}
} // namespace diagnostics
