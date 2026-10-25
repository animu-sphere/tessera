#include "../../examples/renderer-diagnostics/host.hpp"
#include "../../examples/renderer-diagnostics/sample_data.hpp"
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <algorithm>

using namespace tessera;
using namespace std::chrono_literals;
namespace panel = diagnostics;
namespace {
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message;
        for (const auto& error : result.diagnostics)
            message += error.code + " at " + error.path + ": " + error.message + "\n";
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
const LayoutBox& box(const panel::View& view, const std::string& id) {
    const auto owner = view.tree->find(id);
    require(owner.has_value(), "Missing layout owner " + id);
    return view.layout.boxes[owner->index];
}
std::string name(const panel::View& view, const std::string& id) {
    const auto target = take(resolve_target(view.inspection, AuthorIdTarget{id}));
    const auto semantic = view.inspection.elements[target.index].semantic;
    require(semantic.has_value(), "Missing semantics " + id);
    return view.inspection.semantics.nodes[*semantic].name;
}
bool visible(const panel::View& view, const std::string& id) {
    const auto& bounds = box(view, id);
    return bounds.clip && intersect(bounds.border_box, *bounds.clip) == bounds.border_box;
}
std::vector<ReplayBox> geometry(const panel::View& view) {
    std::vector<ReplayBox> boxes;
    for (const auto& b : view.layout.boxes)
        boxes.push_back({b.node.index, b.parent, b.border_box, b.border, b.padding, b.visible, b.clip, b.scroll});
    return boxes;
}
struct Glyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};
struct RejectingText final : TextShaper {
    bool paint = false;
    PlaceholderTextShaper real;
    explicit RejectingText(bool paint) : paint(paint) {}
    Result<TextMetrics> measure(std::string_view text, const TextStyle& style, const TextConstraints& constraints) override {
        if (!paint) return {std::nullopt, {{"injected_measure", Severity::error, "/text", "Injected telemetry measurement failure"}}};
        return real.measure(text, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view, const TextStyle&, const TextConstraints&) override {
        return {std::nullopt, {{"injected_paint", Severity::error, "/text", "Injected telemetry paint failure"}}};
    }
};

struct Fixture {
    PlaceholderTextShaper text;
    std::unique_ptr<panel::Host> host = take(panel::Host::open(panel::sample_a(), text));
    Glyphs raster;
    GlyphCache cache{raster};
    CpuFrameCapture frames{&cache};
    std::int64_t tick = 0;
    unsigned observations = 0;

    auto stable() const {
        const auto& view = host->view();
        return std::tuple{host->snapshot(), host->disclosure(), host->offset(), host->focused(),
            host->numeric(), host->msaa(), host->outlines(), host->scene_lights(), view.styles, view.commands.discover(), view.tree->document(), view.commands.generation(), view.inspection, geometry(view), view.paint};
    }
    void verify() {
        const auto& view = host->view();
        const auto& root = box(view, "renderer-diagnostics");
        require(root.border_box == Rect{{0, 0}, panel::viewport} && root.scroll && root.scroll->offset == host->offset() &&
            host->offset().y >= 0 && host->offset().y <= root.scroll_limit().y, "Root viewport/offset differs");
        for (const auto& bounds : view.layout.boxes) {
            if (bounds.node.index != 0) require(bounds.clip == std::optional<Rect>{root.padding_box()}, "Descendant clip differs");
            const auto& inspected = view.inspection.elements[bounds.node.index].geometry;
            require(inspected && inspected->border_box == bounds.border_box && inspected->clip == bounds.clip,
                "Inspection and layout clipping differ");
        }
        const auto& snapshot = host->snapshot();
        if (host->disclosure().expanded[3]) {
            require(name(view, "scene-lights-toggle") == (host->scene_lights().accepted ? "[x] Scene lights" : "[ ] Scene lights"),
                "Scene lights marker/name differs");
            require(static_cast<bool>(request_semantic_action(view.semantic(), *view.tree->find("scene-lights-toggle"), "activate")) ==
                host->scene_lights().enabled, "Scene lights semantic eligibility differs");
            require(name(view, "outlines-toggle") == (host->outlines().accepted ? "[x] Outlines" : "[ ] Outlines"),
                "Outlines marker/name differs");
            require(static_cast<bool>(request_semantic_action(view.semantic(), *view.tree->find("outlines-toggle"), "activate")) ==
                host->outlines().enabled, "Outlines semantic eligibility differs");
            require(name(view, "msaa-accepted") == "Accepted MSAA: " + std::to_string(host->msaa().accepted), "Accepted MSAA text differs");
            for (const auto samples : panel::sample_counts) {
                const auto id = "msaa-" + std::to_string(samples);
                require(name(view, id) == "MSAA " + std::to_string(samples) +
                    (samples == host->msaa().accepted ? " (selected)" : ""), "Choice marker differs");
                const bool enabled = host->msaa().enabled && std::find(host->msaa().supported.begin(),
                    host->msaa().supported.end(), samples) != host->msaa().supported.end();
                require(static_cast<bool>(request_semantic_action(view.semantic(), *view.tree->find(id), "activate")) == enabled,
                    "MSAA semantic eligibility differs");
            }
        } else require(!view.tree->find("scene-lights-toggle") && !view.tree->find("outlines-toggle") && !view.tree->find("msaa-1") && !view.tree->find("msaa-accepted"), "Collapsed MSAA remained present");
        require(name(view, "value-scene") == snapshot.scene && name(view, "value-device") == snapshot.device &&
            name(view, "value-extent") == "1280 x 720" && name(view, "value-presentation") == snapshot.presentation,
            "Snapshot identity/extent differs");
        for (std::size_t i = 0; i < panel::groups.size(); ++i) {
            require(name(view, "disclosure-" + std::string(panel::groups[i])) == std::string(panel::groups[i]) +
                (host->disclosure().expanded[i] ? " (expanded)" : " (collapsed)"), "Disclosure observation differs");
            const auto& rows = i == 0 ? snapshot.cpu : snapshot.gpu;
            if (i < 2) for (const auto& timing : rows)
                require(view.tree->find(std::string(panel::groups[i]) + "-" + timing.id).has_value() == host->disclosure().expanded[i],
                    "Timing presence differs from disclosure");
        }
        for (const auto& counter : snapshot.counters) {
            const auto id = "counter-" + counter.id;
            require(view.tree->find(id).has_value() == host->disclosure().expanded[2], "Counter presence differs");
            if (host->disclosure().expanded[2]) require(name(view, "value-" + id) == std::to_string(counter.value) + " " + counter.unit,
                "Counter value/unit differs: " + id);
        }
        const auto& history = box(view, "history-bars").border_box;
        require(history.size == Size{254, 64}, "History container size differs");
        for (std::size_t i = 0; i < snapshot.history.size(); ++i) {
            const auto& sample = snapshot.history[i];
            const auto& bar = box(view, "history-frame-" + std::to_string(sample.frame)).border_box;
            const auto height = static_cast<float>(64 * std::min(sample.milliseconds, 40.0) / 40);
            require(bar.size.width == 6 && std::abs(bar.size.height - height) < 0.0001f &&
                bar.origin.x == history.origin.x + i * 8 &&
                std::abs(bar.origin.y + bar.size.height - (history.origin.y + 64)) < 0.0001f,
                "Bar sample geometry differs for " + std::to_string(sample.frame) + " at view " + std::to_string(observations));
        }
        if (snapshot.history.empty()) {
            require(view.tree->get(*view.tree->find("history-bars"))->children.empty() &&
                name(view, "history-summary") == "History unavailable: no samples", "Empty history differs");
        }
        ReplayRecording recording;
        recording.context = panel::context; recording.viewport = panel::viewport;
        recording.document = view.tree->document(); recording.styles = view.styles;
        recording.environment.scale = 1.25f; recording.environment.locale = "en";
        if (host->offset() != Point{}) recording.steps.push_back(InputEvent{1us, Scroll{{16, 16}, host->offset()}});
        recording.steps.push_back(ReplayTick{2us});
        const auto first = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        const auto repeated = take(run_offscreen({&recording, &text, frames.backend(), &frames}));
        require(first == repeated && first.output.generations.back().boxes == geometry(view) &&
            first.output.generations.back().paint == view.paint &&
            first.output.semantics.back().nodes == own_semantic_records(view.inspection.semantics), "Accepted replay differs");
        const auto extent = take(capture_extent(panel::viewport, 1.25f));
        const auto image = take(frames.capture({0, panel::viewport, 1.25f, extent, &view.paint}));
        require(first.frames.back().image == image && take(frames.capture({0, panel::viewport, 1.25f, extent, &view.paint})) == image,
            "Repeated CPU bytes differ from accepted replay");
        ++observations;
    }
    template<class Event> void send(Event event, CommandSource source = CommandSource::keyboard) {
        require(take(host->input({std::chrono::microseconds{++tick}, std::move(event)}, source, text)), "Expected consumed panel event");
        verify();
    }
    void invoke(const std::string& id, CommandSource source = CommandSource::agent) {
        require(take(host->invoke(id, source, text)), "Expected disclosure command"); verify();
    }
    void refresh(panel::Snapshot snapshot) {
        const auto calls = host->disclosure_requests();
        require(take(host->refresh(std::move(snapshot), text)) && calls == host->disclosure_requests(), "Telemetry refresh executed a command");
        verify();
    }
    void exact(char which) {
        const auto& view = host->view();
        const bool a = which == 'A', c = which == 'C';
        require(name(view, "value-frame-summary") == (a ? "16 ms / 62.5 FPS / 24 ms" : "20 ms / 50 FPS / 40 ms"), "Frame units differ");
        if (host->disclosure().expanded[0]) {
            require(name(view, "value-cpu-update") == (a ? "4 / 6 / 8 / 1 ms" : "5 / 7 / 9 / 1 ms"), "CPU update differs");
            require(name(view, "value-cpu-submit") == (a ? "0 / 0 / 0 / 0 ms" : "1 / 2 / 3 / 0.5 ms"), "Measured zero differs");
        }
        if (host->disclosure().expanded[1]) {
            require(name(view, "value-gpu-geometry") == (c ? "GPU timestamps unavailable" : a ? "7 / 9 / 10 / 1 ms" : "9 / 11 / 12 / 1 ms"), "GPU geometry differs");
            require(name(view, "value-gpu-shading") == (c ? "GPU timestamps unavailable" : a ? "3 / 4 / 5 / 0.5 ms" : "4 / 5 / 6 / 0.5 ms"), "GPU shading differs");
        }
        require(name(view, "history-summary") == (c ? "History unavailable: no samples" : a ?
            "4 samples; frames 101-104; min / max / mean 0 / 24 / 12 ms" :
            "4 samples; frames 102-105; min / max / mean 0 / 40 / 20 ms"), "History text differs");
    }
};

void run() {
    Fixture fixture;
    auto& host = *fixture.host;
    fixture.verify(); fixture.exact('A');
    require(host.offset() == Point{} && !visible(host.view(), "disclosure-counters"), "Lower control must require scrolling");
    auto bounds = box(host.view(), "disclosure-counters").border_box;
    require(!take(hit_test(host.view().hit(), {bounds.origin.x + 1, bounds.origin.y + 1})).target, "Clipped lower control remains hittable");
    fixture.send(Scroll{{16, 16}, {0, 48}});
    require(host.offset() == Point{0, 48}, "Normalized scroll differs");
    fixture.send(Scroll{{16, 16}, {0, 4096}});
    require(host.offset() == box(host.view(), "renderer-diagnostics").scroll_limit(), "Bottom clamp differs");
    fixture.send(Scroll{{16, 16}, {0, -4096}});
    require(host.offset() == Point{}, "Top clamp differs");
    for (const auto* group : {"cpu", "gpu", "counters"}) {
        fixture.send(FocusNext{});
        const auto id = "disclosure-" + std::string(group);
        require(host.focused() == id && visible(host.view(), id), "Keyboard reveal differs");
    }
    const auto injected_snapshot = host.snapshot();
    fixture.send(Activate{}, CommandSource::gamepad);
    require(!host.disclosure().expanded[2] && !host.view().tree->find("counter-draws") && host.snapshot() == injected_snapshot,
        "Gamepad collapse changed telemetry or failed to remove rows");
    const auto retained_disclosure = host.disclosure();
    const auto offset = host.offset();
    require(offset.y > 0, "Lower focus must retain a nonzero offset");
    const auto sample_102_a = box(host.view(), "history-frame-102").border_box.size;
    fixture.refresh(panel::sample_b()); fixture.exact('B');
    require(host.disclosure() == retained_disclosure && host.focused() == "disclosure-counters" && host.offset() == offset &&
        box(host.view(), "history-frame-102").border_box.size == sample_102_a && !host.view().tree->find("history-frame-101") &&
        host.view().tree->find("history-frame-105"), "A/B stable identities or refresh retention differ");
    fixture.refresh(panel::sample_c()); fixture.exact('C');
    require(host.disclosure() == retained_disclosure && host.focused() == "disclosure-counters" && host.offset() == offset,
        "Unavailable refresh lost interaction state");
    fixture.invoke("disclosure-counters", CommandSource::accessibility);
    fixture.exact('C');
    fixture.send(FocusPrevious{}, CommandSource::gamepad);
    require(host.focused() == "disclosure-gpu" && visible(host.view(), "disclosure-gpu"), "Gamepad previous/reveal differs");
    fixture.send(Activate{});
    require(!host.disclosure().expanded[1] && !host.view().tree->find("gpu-geometry"), "Keyboard collapse differs");
    bounds = box(host.view(), "disclosure-gpu").border_box;
    const Point point{bounds.origin.x + 2, bounds.origin.y + 2};
    fixture.send(PointerDown{{1}, point}, CommandSource::pointer);
    fixture.send(PointerUp{{1}, point}, CommandSource::pointer);
    require(host.disclosure().expanded[1], "Pointer disclosure differs");
    fixture.exact('C');

    // Exact counters and all declared A/B observations, independent of collapse.
    fixture.refresh(panel::sample_a()); fixture.exact('A');
    fixture.refresh(panel::sample_b()); fixture.exact('B');
    require(name(host.view(), "value-counter-draws") == "18 draws" && name(host.view(), "value-counter-triangles") == "3600 triangles" &&
        name(host.view(), "value-counter-frame-upload") == "2048 bytes" && name(host.view(), "value-counter-session-upload") == "6144 bytes" &&
        name(host.view(), "value-counter-validation") == "0 messages", "Declared counters differ");

    auto invalid = [&](panel::Snapshot snapshot, const std::string& path) {
        const auto before = fixture.stable();
        const auto calls = host.disclosure_requests();
        const auto rejected = host.refresh(std::move(snapshot), fixture.text);
        require(!rejected && !rejected.diagnostics.empty() && rejected.diagnostics.front().path == path &&
            !rejected.diagnostics.front().message.empty() && before == fixture.stable() && calls == host.disclosure_requests(),
            "Malformed snapshot escaped validation/preservation at " + path);
    };
    auto bad = panel::sample_a(); bad.cpu.push_back(bad.cpu[0]); invalid(bad, "/cpu/update/id");
    bad = panel::sample_a(); bad.cpu[0].milliseconds->mean = std::numeric_limits<double>::quiet_NaN(); invalid(bad, "/cpu/update/mean_ms");
    bad = panel::sample_a(); bad.gpu[0].milliseconds->stddev = -1; invalid(bad, "/gpu/geometry/stddev_ms");
    bad = panel::sample_a(); bad.frame_ms = std::numeric_limits<double>::infinity(); invalid(bad, "/frame_ms");
    bad = panel::sample_c(); bad.gpu[0].unavailable.clear(); invalid(bad, "/gpu/geometry/unavailable");
    bad = panel::sample_a(); bad.cpu[0].unavailable = "ambiguous"; invalid(bad, "/cpu/update/unavailable");
    bad = panel::sample_a(); bad.history[1].frame = 101; invalid(bad, "/history/101/frame");
    bad = panel::sample_a(); bad.history[0].milliseconds = -1; invalid(bad, "/history/101/ms");
    bad = panel::sample_a(); bad.history.resize(33); invalid(bad, "/history");
    bad = panel::sample_a(); bad.cpu.resize(33); invalid(bad, "/cpu");
    bad = panel::sample_a(); bad.counters[0].id = "bad id"; invalid(bad, "/counter/bad id/id");
    bad = panel::sample_a(); bad.counters[0].unit = ""; invalid(bad, "/counter/draws/unit");
    bad = panel::sample_a(); bad.width = 0; invalid(bad, "/extent");
    bad = panel::sample_a(); bad.scene = "bad\nscene"; invalid(bad, "/scene");
    bad = panel::sample_a(); bad.device = std::string(1, '\xff'); invalid(bad, "/device");

    // Full boundary, stable sample IDs and saturation using ordinary geometry.
    auto bounded = panel::sample_b(); bounded.history.clear();
    for (std::uint64_t i = 0; i < 32; ++i) bounded.history.push_back({200 + i, i == 31 ? 80.0 : 0.0});
    fixture.refresh(bounded);
    require(box(host.view(), "history-frame-231").border_box.size.height == 64 &&
        name(host.view(), "history-summary") == "32 samples; frames 200-231; min / max / mean 0 / 80 / 2.5 ms", "Bound/saturation differs");
    bounded.history = {{301, std::numeric_limits<double>::max()}, {302, std::numeric_limits<double>::max()}};
    fixture.refresh(bounded);
    require(name(host.view(), "history-summary").find("inf") == std::string::npos, "Finite history overflowed its textual mean");
    fixture.refresh(panel::sample_b());

    for (const bool paint : {false, true}) {
        RejectingText rejecting(paint);
        const auto before = fixture.stable();
        const auto rejected = host.refresh(panel::sample_c(), rejecting);
        require(!rejected && rejected.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure") &&
            rejected.diagnostics.front().path.find("/nodes/") == 0 && before == fixture.stable(), "Refresh failure changed accepted view");
        const auto rejected_navigation = host.input({std::chrono::microseconds{++fixture.tick}, FocusNext{}}, CommandSource::keyboard, rejecting);
        require(!rejected_navigation && before == fixture.stable(), "Rejected navigation changed accepted focus/offset");
        const auto rejected_scroll = host.input({std::chrono::microseconds{++fixture.tick}, Scroll{{16, 16}, {0, -48}}},
            CommandSource::pointer, rejecting);
        require(!rejected_scroll && before == fixture.stable(), "Rejected scroll changed accepted focus/offset");
        fixture.refresh(panel::sample_c()); fixture.exact('C');
        const auto request = take(host.request("disclosure-counters", CommandSource::agent));
        const auto before_command = fixture.stable();
        const auto calls = host.disclosure_requests();
        const auto execution = take(host.execute(request, rejecting));
        require(execution.outcome.status == CommandOutcomeStatus::error && before_command == fixture.stable() &&
            host.disclosure_requests() == calls + 1, "Failed disclosure changed accepted view");
        const auto duplicate = host.execute(request, fixture.text);
        require(!duplicate && duplicate.diagnostics.front().code == "consumed_sequence" && host.disclosure_requests() == calls + 1,
            "Failed disclosure sequence entered twice");
        fixture.invoke("disclosure-counters"); // Fresh request succeeds after rejection.
        fixture.invoke("disclosure-counters");
    }
    const auto stale = take(host.request("disclosure-counters", CommandSource::agent));
    fixture.refresh(panel::sample_b());
    auto before = fixture.stable();
    const auto calls = host.disclosure_requests();
    require(!host.execute(stale, fixture.text) && before == fixture.stable() && calls == host.disclosure_requests(), "Stale command entered");
    fixture.invoke("disclosure-controls"); // Keep the original telemetry-only viewport assertion.
    fixture.invoke("disclosure-counters"); // Remove lower rows so the disabled header remains in view.
    fixture.send(FocusNext{});
    require(host.focused() == "disclosure-counters", "Expected lower owner before disabling");
    require(take(host.eligibility({true, true, false}, fixture.text)), "Eligibility change failed"); fixture.verify();
    fixture.send(Scroll{{16, 16}, {0, 4096}});
    require(visible(host.view(), "disclosure-counters"), "Disabled pointer fixture must expose its owner");
    before = fixture.stable();
    const auto disabled_calls = host.disclosure_requests();
    for (const auto source : {CommandSource::keyboard, CommandSource::gamepad, CommandSource::accessibility, CommandSource::agent})
        require(!host.invoke("disclosure-counters", source, fixture.text), "Disabled disclosure resolved");
    bounds = box(host.view(), "disclosure-counters").border_box;
    // Programmatic focus and pointer dispatch share the same disabled owner contract.
    FocusDispatcher focus;
    require(!focus.focus(host.view().hit(), *host.view().tree->find("disclosure-counters")), "Disabled disclosure focused");
    PointerDispatcher pointer;
    const Point disabled_point{bounds.origin.x + 1, bounds.origin.y + 1};
    take(pointer.dispatch(host.view().hit(), {1us, PointerDown{{2}, disabled_point}}));
    require(take(pointer.dispatch(host.view().hit(), {2us, PointerUp{{2}, disabled_point}})).actions.empty() &&
        before == fixture.stable() && disabled_calls == host.disclosure_requests(), "Disabled pointer changed state");
    require(take(host.eligibility({true, true, true}, fixture.text)), "Eligibility retry failed"); fixture.verify();
    fixture.invoke("disclosure-counters");

    // A document replacement cancels a pending press even for the same author ID.
    fixture.send(FocusNext{}); // Select/reveal a live disclosure.
    const auto pressed_id = *host.focused();
    bounds = box(host.view(), pressed_id).border_box;
    const Point pressed_point{bounds.origin.x + 2, bounds.origin.y + 2};
    fixture.send(PointerDown{{3}, pressed_point}, CommandSource::pointer);
    fixture.refresh(panel::sample_a());
    const auto prior_calls = host.disclosure_requests();
    // The host claim survives replacement; the release emits no command.
    require(take(host.route({std::chrono::microseconds{++fixture.tick}, PointerUp{{3}, pressed_point}}, CommandSource::pointer, fixture.text)),
        "Claimed release reached camera after replacement");
    fixture.verify();
    require(host.disclosure_requests() == prior_calls, "Pointer press survived replacement");
    std::cout << fixture.observations << " telemetry views match repeated geometry/semantics/paint/CPU replay; " <<
        host.disclosure_requests() << " disclosure attempts; synthetic inputs only\n";
}

void numeric_controls() {
    Fixture fixture;
    auto& host = *fixture.host;
    auto execute = [&](const CommandInvocation& call, TextShaper& text, bool success = true) {
        auto result = take(host.execute(call, text));
        require((result.outcome.status == CommandOutcomeStatus::success) == success, "Unexpected numeric command outcome");
        if (!success) require(!result.outcome.diagnostics.empty(), "Numeric rejection has no diagnostic");
        return result;
    };
    auto edit = [&](auto operation, CommandSource source = CommandSource::agent) {
        const auto& draft = host.numeric().draft;
        execute(take(host.request_edit({draft.owner, draft.revision, std::move(operation)}, source)), fixture.text);
    };
    auto draft = [&](std::string value) {
        edit(TextEditSelection{0, host.numeric().draft.text.size()});
        edit(TextEditInsert{std::move(value)});
    };
    auto route = [&](auto event, CommandSource source = CommandSource::keyboard, TextShaper* text = nullptr) {
        return host.route({std::chrono::microseconds{++fixture.tick}, std::move(event)}, source, text ? *text : fixture.text);
    };
    auto invoke = [&](const std::string& id, CommandSource source = CommandSource::agent) {
        require(take(host.invoke(id, source, fixture.text)), "Expected numeric fixture command");
    };
    auto refresh = [&](panel::Snapshot snapshot) { require(take(host.refresh(std::move(snapshot), fixture.text)), "Expected refresh"); };
    auto reveal = [&](const std::string& id) {
        for (int i = 0; host.focused() != id && i < 20; ++i) require(take(route(FocusNext{})), "Expected focus traversal");
        require(host.focused() == id && visible(host.view(), id), "Numeric focus/reveal failed: " + id);
    };
    auto click = [&](const std::string& id) {
        reveal(id);
        const auto b = box(host.view(), id).border_box;
        const Point point{b.origin.x + 2, b.origin.y + 2};
        require(take(route(PointerDown{{12}, point}, CommandSource::pointer)) && host.focused() == id, "Pointer focus failed");
        require(take(route(PointerUp{{12}, point}, CommandSource::pointer)), "Pointer activation failed");
        fixture.verify();
    };
    require(host.numeric().accepted == 1 && host.numeric().draft.text == "1", "Initial direct strength differs");
    reveal("direct-input");
    const auto start_calls = host.setting_attempts();
    click("direct-input");
    require(host.setting_attempts() == start_calls, "TextInput activation committed a value");
    require(take(route(KeyDown{Key::home})) && take(route(KeyDown{Key::end, {.shift = true}})) &&
        take(route(tessera::TextInput{"1.5"})), "Keyboard direct entry failed");
    require(host.numeric().draft.text == "1.5" && host.numeric().accepted == 1 && host.setting_changes() == 0,
        "Editing changed accepted setting");
    click("direct-apply");
    require(host.numeric().accepted == 1.5 && host.setting_changes() == 1 && host.setting_attempts() == start_calls + 1,
        "Pointer Apply did not commit exactly once");
    invoke("direct-input");
    const auto same_revision = host.numeric().draft.revision;
    require(take(route(KeyDown{Key::enter})) && host.numeric().draft.revision == same_revision + 1 && host.setting_changes() == 1,
        "Equal Apply did not advance revision without a setting change");
    // Adjustment parity from the same initial value across pointer, logical keyboard and gamepad.
    execute(take(host.request_value("direct", 1, CommandSource::agent)), fixture.text);
    click("direct-increase");
    reveal("direct-increase"); require(take(route(Activate{}, CommandSource::gamepad)), "Expected gamepad activation");
    require(host.numeric().accepted == 1.5, "Direct entry and two increments differ");
    invoke("direct-decrease", CommandSource::accessibility);
    reveal("direct-increase"); require(take(route(Activate{}, CommandSource::keyboard)), "Expected keyboard activation");
    require(host.numeric().accepted == 1.5, "Semantic/keyboard adjustment parity differs");
    // Ordinary logical directional navigation reaches the same operation owner.
    reveal("direct-decrease"); require(take(route(Navigate{Direction::right}, CommandSource::gamepad)), "Expected gamepad navigation");
    require(host.focused() == "direct-increase", "Gamepad directional adjustment navigation differs");
    require(take(route(Activate{}, CommandSource::gamepad)), "Expected gamepad activation");
    require(host.numeric().accepted == 1.75, "Gamepad adjustment failed");
    draft("1.5"); invoke("direct-apply", CommandSource::accessibility);
    require(host.numeric().accepted == 1.5, "Semantic edit/Apply parity differs");

    for (const auto* invalid : {"", "-", "1.", "01", "+1", " 1", "1 ", "1e0", "NaN", "inf", "4.01", "-0.01", "1x", "日"}) {
        draft(invalid);
        const auto before = fixture.stable();
        const auto changes = host.setting_changes();
        const auto rejected = host.invoke("direct-apply", CommandSource::agent, fixture.text);
        require(!rejected && rejected.diagnostics.front().path.ends_with("/controls/direct") && before == fixture.stable() &&
            host.setting_changes() == changes, "Invalid draft changed accepted observations: [" + std::string(invalid) + "] " + (rejected.diagnostics.empty() ? "no error" : rejected.diagnostics.front().path));
        require(!host.request("direct-increase", CommandSource::agent) && !host.request("direct-decrease", CommandSource::agent),
            "Dirty draft allowed adjustment");
    }
    // Retain exact invalid bytes, selection, revision and owner through all refresh/removal paths.
    edit(TextEditSelection{0, host.numeric().draft.text.size()});
    invoke("direct-input");
    const auto pending = host.numeric(); const auto changes = host.setting_changes(); const auto attempts = host.setting_attempts();
    for (const auto& snapshot : {panel::sample_a(), panel::sample_b(), panel::sample_c()}) {
        refresh(snapshot);
        require(host.numeric() == pending && host.setting_attempts() == attempts && host.setting_changes() == changes,
            "Telemetry changed numeric draft/settings");
    }
    const auto stale = take(host.request("direct-apply", CommandSource::agent));
    invoke("disclosure-controls");
    require(host.numeric() == pending && host.focused() == "disclosure-controls" && !host.view().tree->find("direct-input"),
        "Collapse lost draft or failed header recovery");
    require(!host.execute(stale, fixture.text) && !host.request_value("direct", 2, CommandSource::agent), "Hidden setting remained callable");
    invoke("disclosure-controls");
    require(host.numeric() == pending && host.focused() == "disclosure-controls", "Expansion changed draft/focus");
    const auto disabled_call = take(host.request("direct-apply", CommandSource::agent));
    take(host.numeric_eligibility(false, fixture.text)); fixture.verify();
    require(!host.execute(disabled_call, fixture.text) && !host.request_edit({pending.draft.owner, pending.draft.revision, TextEditInsert{"2"}}, CommandSource::agent),
        "Disabled draft accepted request");
    take(host.numeric_eligibility(true, fixture.text)); fixture.verify();
    require(host.numeric() == pending, "Re-enabling lost draft");
    // Old edit owners/revisions resolve through the live presentation but reject at the host boundary.
    for (const auto request : {TextEditRequest{pending.draft.owner + 1000, pending.draft.revision, TextEditInsert{"2"}},
        TextEditRequest{pending.draft.owner, pending.draft.revision - 1, TextEditInsert{"2"}}}) {
        const auto before = fixture.stable();
        execute(take(host.request_edit(request, CommandSource::agent)), fixture.text, false);
        require(before == fixture.stable(), "Stale edit mutated accepted draft");
    }
    invoke("direct-cancel");
    require(host.numeric().accepted == pending.accepted && host.numeric().draft.text == "1.5" &&
        host.numeric().draft.revision > pending.draft.revision && !host.request("direct-cancel", CommandSource::agent), "Cancel policy differs");
    draft("2."); invoke("direct-input");
    const auto camera = host.camera_events();
    require(!route(KeyDown{Key::enter}) && host.last_consumed() && host.camera_events() == camera, "Rejected raw Apply reached camera");
    require(take(route(KeyDown{Key::escape})) && host.numeric().draft.text == "1.5", "Raw Escape failed Cancel");
    draft("2"); reveal("direct-apply");
    require(take(route(Cancel{}, CommandSource::gamepad)) && host.numeric().draft.text == "1.5", "Operation-owner Cancel failed");

    // Successful requests also consume their sequence and cannot repeat handler attempts.
    const auto once = take(host.request_value("direct", 2, CommandSource::agent));
    execute(once, fixture.text);
    const auto after_once = fixture.stable(); const auto attempts_once = host.setting_attempts();
    require(!host.execute(once, fixture.text) && after_once == fixture.stable() && attempts_once == host.setting_attempts(),
        "Accepted setting request executed twice");
    // Canonical decimal remains valid entry grammar, including short subunit decimals.
    draft("0.000000000000000000000000000001"); invoke("direct-apply");
    require(take(panel::parse_numeric(host.numeric().draft.text)) == host.numeric().accepted, "Canonical decimal cannot be reapplied");
    const auto tiny = take(host.request_value("direct", std::numeric_limits<double>::denorm_min(), CommandSource::agent));
    const auto before_tiny = fixture.stable(); execute(tiny, fixture.text, false);
    require(before_tiny == fixture.stable(), "Unrepresentable bounded canonical draft was accepted");
    // Both endpoints and clamped fractional adjustment, with typed node-free validation.
    for (const auto value : {0.0, 4.0}) {
        execute(take(host.request_value("direct", value, CommandSource::agent)), fixture.text);
        require(!host.request(value == 0 ? "direct-decrease" : "direct-increase", CommandSource::agent), "Endpoint direction enabled");
        fixture.verify();
    }
    for (const auto value : {-1.0, 4.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        require(!host.request_value("direct", value, CommandSource::agent), "Invalid typed numeric resolved");
    require(!host.request_value("ambient", 1, CommandSource::agent), "Node-free request accepted foreign field");
    execute(take(host.request_value("direct", 3.9, CommandSource::agent)), fixture.text);
    invoke("direct-increase"); require(host.numeric().accepted == 4, "Upper adjustment did not clamp");
    execute(take(host.request_value("direct", 0.1, CommandSource::agent)), fixture.text);
    invoke("direct-decrease"); require(host.numeric().accepted == 0, "Lower adjustment did not clamp");
    draft("-0"); invoke("direct-apply"); require(host.numeric().draft.text == "0", "Negative zero was not canonicalized");
    draft("1.50"); invoke("direct-apply"); require(host.numeric().draft.text == "1.5", "Shortest canonical text differs");
    require(!host.request_edit({host.numeric().draft.owner, host.numeric().draft.revision, TextEditInsert{std::string(33, '1')}}, CommandSource::agent),
        "Oversized insert resolved");
    edit(TextEditSelection{0, 0});
    auto overflow = take(host.request_edit({host.numeric().draft.owner, host.numeric().draft.revision, TextEditInsert{std::string(32, '1')}}, CommandSource::agent));
    const auto before_overflow = fixture.stable(); execute(overflow, fixture.text, false);
    require(before_overflow == fixture.stable(), "Combined draft bound failed");

    // Every command family stages owned candidates; failure never accepts a partial edit, value or disclosure.
    for (const bool paint : {false, true}) {
        RejectingText rejecting(paint);
        draft("2.50");
        for (const auto* id : {"direct-apply", "direct-cancel", "disclosure-controls"}) {
            const auto before = fixture.stable(); const auto changes_before = host.setting_changes();
            const auto call = take(host.request(id, CommandSource::agent));
            const auto failed = execute(call, rejecting, false);
            require(failed.outcome.diagnostics.front().path.find("/nodes/") != std::string::npos &&
                failed.outcome.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure") &&
                failed.outcome.diagnostics.size() <= max_command_result_diagnostics && before == fixture.stable() &&
                changes_before == host.setting_changes(), "Failed numeric stage changed accepted observations: " + std::string(id) + " " + failed.outcome.diagnostics.front().path + " equal=" + std::to_string(before == fixture.stable()));
            require(!host.execute(call, fixture.text), "Rejected numeric request reused sequence");
            invoke(id); // Fresh request succeeds once.
            if (std::string{id} == "disclosure-controls") invoke(id);
            if (host.numeric().draft.text == panel::canonical(host.numeric().accepted)) draft("2.50");
        }
        const auto before_edit = fixture.stable();
        auto edit_call = take(host.request_edit({host.numeric().draft.owner, host.numeric().draft.revision, TextEditSelection{0, 1}}, CommandSource::agent));
        execute(edit_call, rejecting, false); require(before_edit == fixture.stable(), "Failed edit changed revision/selection");
        require(!host.execute(edit_call, fixture.text), "Failed edit request executed twice");
        edit(TextEditSelection{0, 1});
        invoke("direct-cancel");
        const auto before_adjust = fixture.stable();
        auto adjustment = take(host.request("direct-increase", CommandSource::agent));
        execute(adjustment, rejecting, false); require(before_adjust == fixture.stable(), "Failed adjustment changed setting/draft");
        require(!host.execute(adjustment, fixture.text), "Failed adjustment reused sequence");
        invoke("direct-increase");
        fixture.verify();
    }
    // Consumed errors, disabled hits and claimed streams across replacement never reach the camera.
    reveal("direct-increase");
    const auto b = box(host.view(), "direct-increase").border_box;
    const Point p{b.origin.x + 2, b.origin.y + 2};
    const auto delivered = host.camera_events();
    require(take(route(PointerDown{{22}, p}, CommandSource::pointer)), "Panel did not claim press");
    refresh(panel::sample_b());
    require(take(route(PointerMove{{22}, {800, 200}}, CommandSource::pointer)) &&
        take(route(PointerUp{{22}, {800, 200}}, CommandSource::pointer)) && host.camera_events() == delivered,
        "Replaced panel lost claimed stream");
    take(host.numeric_eligibility(false, fixture.text));
    require(take(route(PointerDown{{23}, p}, CommandSource::pointer)) && take(route(PointerUp{{23}, p}, CommandSource::pointer)) &&
        host.camera_events() == delivered, "Disabled panel hit reached camera");
    take(host.numeric_eligibility(true, fixture.text));
    reveal("direct-input");
    RejectingText rejecting(false);
    const auto before_error = fixture.stable();
    require(!route(tessera::TextInput{"2"}, CommandSource::keyboard, &rejecting) && host.last_consumed() &&
        host.camera_events() == delivered && before_error == fixture.stable(), "Consumed edit error reached camera or accepted a candidate");
    require(!take(route(KeyDown{Key::f1})) && host.camera_events() == delivered + 1, "Unmapped key missed camera");
    require(!take(route(PointerDown{{24}, {800, 200}}, CommandSource::pointer)) && host.camera_events() == delivered + 2,
        "Outside press missed camera");
    require(!take(route(PointerUp{{24}, p}, CommandSource::pointer)) && host.camera_events() == delivered + 3,
        "Outside stream was claimed after entering panel");
    auto reopened = take(panel::Host::open(panel::sample_a(), fixture.text));
    require(reopened->numeric().draft.owner != host.numeric().draft.owner, "Reopened panel reused draft lifetime");
    fixture.verify();
    std::cout << fixture.observations << " numeric views match CPU/replay; " << host.setting_attempts() << " setting attempts, " <<
        host.setting_changes() << " accepted changes; " << host.camera_events() << " camera deliveries\n";
}
}
namespace {
void msaa_controls() {
    // Every route starts at identical settings and must enter one handler exactly once.
    for (int route = 0; route < 5; ++route) {
        Fixture f;
        auto& h = *f.host;
        auto send = [&](auto event, CommandSource source) {
            require(take(h.route({std::chrono::microseconds{++f.tick}, std::move(event)}, source, f.text)), "Expected MSAA input");
        };
        if (route < 3) {
            for (int i = 0; h.focused() != "msaa-2" && i < 20; ++i) send(FocusNext{}, CommandSource::keyboard);
            require(h.focused() == "msaa-2" && visible(h.view(), "msaa-2"), "MSAA traversal/reveal failed");
            if (route == 0) {
                const auto b = box(h.view(), "msaa-2").border_box;
                const Point p{b.origin.x + 2, b.origin.y + 2};
                send(PointerDown{{31}, p}, CommandSource::pointer);
                require(h.setting_attempts() == 0, "MSAA press executed before release");
                send(PointerUp{{31}, p}, CommandSource::pointer);
            } else if (route == 1) send(KeyDown{Key::enter}, CommandSource::keyboard);
            else {
                send(Navigate{Direction::left}, CommandSource::gamepad);
                require(h.focused() == "msaa-1", "MSAA left navigation differs");
                send(Navigate{Direction::right}, CommandSource::gamepad);
                require(h.focused() == "msaa-2", "MSAA right navigation differs");
                send(Activate{}, CommandSource::gamepad);
            }
        } else if (route == 3) take(h.invoke("msaa-2", CommandSource::accessibility, f.text));
        else {
            const auto call = take(h.request_value("msaa", 2, CommandSource::agent));
            require(take(h.execute(call, f.text)).outcome.status == CommandOutcomeStatus::success, "MSAA node-free set failed");
            require(!h.execute(call, f.text), "MSAA sequence executed twice");
        }
        require(h.msaa().accepted == 2 && h.setting_attempts() == 1 && h.setting_changes() == 1 && h.camera_events() == 0 &&
            h.numeric().accepted == 1 && h.numeric().draft.text == "1", "MSAA route parity/once-only failed");
        f.verify();
    }

    Fixture f;
    auto& h = *f.host;
    auto send = [&](auto event, CommandSource source = CommandSource::keyboard, TextShaper* text = nullptr) {
        return h.route({std::chrono::microseconds{++f.tick}, std::move(event)}, source, text ? *text : f.text);
    };
    auto reveal = [&](const std::string& id) {
        for (int i = 0; h.focused() != id && i < 20; ++i) take(send(KeyDown{Key::tab}));
        require(h.focused() == id && visible(h.view(), id), "MSAA reveal failed: " + id);
    };
    auto execute = [&](const CommandInvocation& call, TextShaper& text, bool success) {
        auto result = take(h.execute(call, text));
        require((result.outcome.status == CommandOutcomeStatus::success) == success, "Unexpected MSAA outcome");
        return result;
    };
    auto invoke = [&](const std::string& id) { take(h.invoke(id, CommandSource::agent, f.text)); };
    auto capabilities = [&](std::vector<unsigned> supported, bool enabled = true) {
        const auto attempts = h.setting_attempts(), changes = h.setting_changes();
        take(h.msaa_capabilities(std::move(supported), enabled, f.text));
        require(h.setting_attempts() == attempts && h.setting_changes() == changes, "Capability update requested a setting");
    };
    const auto initial = h.msaa();
    require(initial.accepted == 1 && initial.supported == std::vector<unsigned>{1, 2, 4} && initial.enabled, "Initial MSAA differs");
    require(!h.request("msaa-8", CommandSource::agent), "Unsupported choice enabled");
    // Direct request schema must not coerce strings/Booleans or accept extra choice arguments.
    for (const CommandValue value : {CommandValue{true}, CommandValue{std::string{"2"}}})
        require(!h.view().commands.resolve({h.view().commands.generation(), "msaa.set", {{"field", std::string{"msaa"}},
            {"value", value}}, CommandSource::agent, 999}), "MSAA coerced a nonnumber");
    const auto action = take(request_semantic_action(h.view().semantic(), *h.view().tree->find("msaa-2"), "activate"));
    require(!h.view().commands.resolve_action(h.view().semantic(), action, h.view().commands.generation(),
        {{"value", 4.0}}, CommandSource::agent, 999), "Choice accepted another sample payload");
    for (double value : {0.0, 9.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        require(!h.request_value("msaa", value, CommandSource::agent), "MSAA schema accepted invalid number");
    for (double value : {1.5, 3.0, 8.0}) {
        const auto before = f.stable(); const auto attempts = h.setting_attempts();
        const auto call = take(h.request_value("msaa", value, CommandSource::agent));
        const auto result = execute(call, f.text, false);
        require(result.outcome.diagnostics.front().path.ends_with("/controls/msaa") && before == f.stable() &&
            h.setting_attempts() == attempts + 1 && !h.execute(call, f.text), "Invalid MSAA membership changed state or repeated");
    }
    // Equal activation accepts a fresh presentation while recording no accepted-value change.
    const auto generation = h.view().commands.generation(), attempts = h.setting_attempts();
    invoke("msaa-1");
    require(h.msaa() == initial && h.setting_attempts() == attempts + 1 && h.setting_changes() == 0 &&
        h.view().commands.generation() > generation, "Equal MSAA accounting differs");
    // Preserve incomplete draft bytes and reverse selection through every MSAA operation/update.
    auto edit = [&](auto operation) {
        const auto d = h.numeric().draft;
        execute(take(h.request_edit({d.owner, d.revision, std::move(operation)}, CommandSource::agent)), f.text, true);
    };
    edit(TextEditSelection{0, 1}); edit(TextEditInsert{"2."}); edit(TextEditSelection{2, 0});
    const auto pending = h.numeric();
    reveal("direct-input");
    const auto held = take(h.request("msaa-4", CommandSource::agent));
    invoke("msaa-2");
    require(h.focused() == "direct-input" && h.numeric() == pending, "MSAA changed eligible field focus or pending draft");
    const auto after = f.stable(); const auto entered = h.setting_attempts();
    require(!h.execute(held, f.text) && after == f.stable() && entered == h.setting_attempts(), "Stale presentation entered MSAA handler");
    reveal("msaa-4"); invoke("msaa-4");
    require(h.numeric() == pending && h.focused() == "msaa-4" && visible(h.view(), "msaa-4"), "MSAA lost draft/focus");
    f.verify();
    for (const auto& snapshot : {panel::sample_a(), panel::sample_b(), panel::sample_c()}) {
        const auto before_msaa = h.msaa(); const auto before_attempts = h.setting_attempts();
        take(h.refresh(snapshot, f.text));
        require(h.numeric() == pending && h.msaa() == before_msaa && h.setting_attempts() == before_attempts &&
            h.focused() == "msaa-4", "Refresh changed MSAA/draft/focus");
    }
    for (const auto& supported : {std::vector<unsigned>{}, std::vector<unsigned>{1, 2},
        std::vector<unsigned>{1, 4, 4}, std::vector<unsigned>{1, 4, 3}, std::vector<unsigned>{0, 4}}) {
        const auto before = f.stable();
        require(!h.msaa_capabilities(supported, false, f.text) && before == f.stable(), "Invalid capabilities accepted/repaired");
    }
    const auto held_capability = take(h.request_value("msaa", 2, CommandSource::agent));
    capabilities({4, 8});
    require(h.focused() == "msaa-4" && !h.request("msaa-2", CommandSource::agent) && !h.execute(held_capability, f.text),
        "Capability replacement lost focus or allowed stale request");
    execute(take(h.request_value("msaa", 8, CommandSource::agent)), f.text, true);
    require(h.msaa().accepted == 8 && h.numeric() == pending, "Newly supported MSAA changed numeric draft");
    // Disable a focused nonselected choice by membership, then disable the focused setting entirely.
    reveal("msaa-4"); capabilities({8});
    require(h.focused() == "msaa-8" && visible(h.view(), "msaa-8"), "Membership focus recovery/reveal differs");
    const auto held_disabled = take(h.request("msaa-8", CommandSource::agent));
    capabilities({8}, false);
    require(h.focused() != "msaa-8" && !h.request_value("msaa", 8, CommandSource::agent) &&
        !h.request("msaa-8", CommandSource::agent) && !h.execute(held_disabled, f.text), "Disabled MSAA remained callable");
    require(h.focused().has_value() && visible(h.view(), *h.focused()), "Disabled MSAA recovery was not revealed");
    capabilities({1, 2, 4, 8}); reveal("msaa-8");
    invoke("disclosure-controls");
    require(h.focused() == "disclosure-controls" && visible(h.view(), "disclosure-controls") && h.numeric() == pending,
        "MSAA collapse did not recover/reveal header or lost draft");
    require(!h.request_value("msaa", 1, CommandSource::agent) && !h.request("msaa-1", CommandSource::agent), "Hidden MSAA callable");
    const auto collapsed = f.stable();
    require(!h.msaa_capabilities({1}, false, f.text) && collapsed == f.stable(), "Collapsed capabilities excluded accepted MSAA");
    capabilities({8}, false); invoke("disclosure-controls");
    require(h.focused() == "disclosure-controls" && h.numeric() == pending, "MSAA expansion changed header focus/draft");
    capabilities({1, 2, 4, 8});
    // Failed value/capability/eligibility stages preserve the full accepted view.
    for (bool paint : {false, true}) {
        RejectingText rejecting(paint);
        for (bool node_free : {false, true}) {
            const auto before = f.stable(); const auto attempts_before = h.setting_attempts(), changes = h.setting_changes();
            const auto call = node_free ? take(h.request_value("msaa", 2, CommandSource::agent)) : take(h.request("msaa-4", CommandSource::agent));
            const auto result = execute(call, rejecting, false);
            require(before == f.stable() && attempts_before + 1 == h.setting_attempts() && changes == h.setting_changes() &&
                result.outcome.diagnostics.size() <= max_command_result_diagnostics &&
                result.outcome.diagnostics.front().path.find("/nodes/") != std::string::npos &&
                result.outcome.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure"), "Failed MSAA stage leaked observations");
            require(!h.execute(call, f.text), "Failed MSAA reused sequence");
            const auto retry = node_free ? take(h.request_value("msaa", 2, CommandSource::agent)) : take(h.request("msaa-4", CommandSource::agent));
            execute(retry, f.text, true);
            require(h.numeric() == pending, "MSAA retry reset pending draft");
        }
        for (bool enabled : {false, true}) {
            const auto before = f.stable(); const auto attempts_before = h.setting_attempts();
            const auto rejected = h.msaa_capabilities({2, 8}, enabled, rejecting);
            require(!rejected && before == f.stable() && attempts_before == h.setting_attempts() &&
                !rejected.diagnostics.empty() && rejected.diagnostics.size() <= max_command_result_diagnostics &&
                rejected.diagnostics.front().path.starts_with("/nodes/") &&
                rejected.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure"),
                "Failed capabilities/eligibility leaked state or setting request");
            capabilities({2, 8}, enabled); capabilities({1, 2, 4, 8});
        }
        f.verify();
    }
    reveal("msaa-4");
    const auto b = box(h.view(), "msaa-4").border_box;
    const Point p{b.origin.x + 2, b.origin.y + 2};
    const auto camera = h.camera_events(), calls = h.setting_attempts();
    take(send(PointerDown{{41}, p}, CommandSource::pointer));
    capabilities({2, 8});
    take(send(PointerUp{{41}, p}, CommandSource::pointer));
    require(h.setting_attempts() == calls && h.last_consumed(), "Replacement retained pending activation or lost input claim");
    // Disabled choice is still an inside-panel hit, consumed without command/camera delivery.
    take(send(PointerDown{{42}, p}, CommandSource::pointer)); take(send(PointerUp{{42}, p}, CommandSource::pointer));
    require(h.setting_attempts() == calls && h.camera_events() == camera, "Disabled MSAA hit reached handler/camera");
    capabilities({1, 2, 4, 8}); reveal("msaa-4");
    const auto current = box(h.view(), "msaa-4").border_box;
    const Point point{current.origin.x + 2, current.origin.y + 2};
    take(send(PointerDown{{43}, point}, CommandSource::pointer));
    RejectingText rejecting(false);
    const auto before_error = f.stable();
    require(!send(PointerUp{{43}, point}, CommandSource::pointer, &rejecting) && h.last_consumed() &&
        h.camera_events() == camera && before_error == f.stable(), "Failed MSAA pointer activation reached camera/accepted state");
    take(send(PointerUp{{43}, point}, CommandSource::pointer));
    require(h.setting_attempts() == calls + 1, "MSAA release executed failed command twice");
    take(send(PointerDown{{44}, point}, CommandSource::pointer)); capabilities({1, 2, 4, 8});
    take(send(PointerMove{{44}, {800, 200}}, CommandSource::pointer));
    take(send(PointerCancel{{44}}, CommandSource::pointer));
    require(h.last_consumed() && h.camera_events() == camera && h.numeric() == pending, "Replaced MSAA stream escaped camera arbitration/draft retention");
    take(send(Scroll{{16, 16}, {0, 4096}}));
    require(h.offset() == box(h.view(), "renderer-diagnostics").scroll_limit(), "MSAA bottom scroll clamp differs");
    f.verify();
    std::cout << f.observations << " MSAA views match CPU/replay; 5 equal input routes; " << h.setting_attempts() <<
        " setting attempts, " << h.setting_changes() << " accepted changes; " << h.camera_events() << " camera deliveries\n";
}
void boolean_controls(bool scene_lights) {
    const std::string field = scene_lights ? "scene_lights" : "outlines";
    const std::string id = scene_lights ? "scene-lights-toggle" : "outlines-toggle";
    const std::string previous = scene_lights ? "outlines-toggle" : "msaa-1";
    const bool initial = scene_lights;
    auto state = [&](const panel::Host& host) {
        return std::pair{scene_lights ? host.scene_lights().accepted : host.outlines().accepted,
            scene_lights ? host.scene_lights().enabled : host.outlines().enabled};
    };
    auto peer = [&](const panel::Host& host) {
        return std::pair{scene_lights ? host.outlines().accepted : host.scene_lights().accepted,
            scene_lights ? host.outlines().enabled : host.scene_lights().enabled};
    };
    auto set = [&](panel::Host& host, std::string target, bool value) {
        return scene_lights ? host.request_scene_lights(std::move(target), value, CommandSource::agent) :
            host.request_outlines(std::move(target), value, CommandSource::agent);
    };
    auto eligible = [&](panel::Host& host, bool enabled, TextShaper& text) {
        return scene_lights ? host.scene_lights_eligibility(enabled, text) : host.outlines_eligibility(enabled, text);
    };
    // Compare ordinary activation routes from the same accepted initial value.
    for (int route = 0; route < 4; ++route) {
        Fixture f;
        auto& h = *f.host;
        auto send = [&](auto event, CommandSource source) {
            require(take(h.route({std::chrono::microseconds{++f.tick}, std::move(event)}, source, f.text)), "Expected Boolean control input");
        };
        require(state(h) == std::pair{initial, true}, "Initial Boolean control differs");
        if (route < 3) {
            const auto target = route == 2 ? previous : id;
            for (int i = 0; h.focused() != target && i < 20; ++i)
                send(KeyDown{Key::tab}, CommandSource::keyboard);
            require(h.focused() == target && visible(h.view(), target), "Boolean control traversal/reveal failed");
            if (route == 0) {
                const auto b = box(h.view(), target).border_box;
                const Point p{b.origin.x + 2, b.origin.y + 2};
                send(PointerDown{{51}, p}, CommandSource::pointer);
                require(h.setting_attempts() == 0, "Boolean control press executed before release");
                send(PointerUp{{51}, p}, CommandSource::pointer);
            } else if (route == 1) send(KeyDown{Key::space}, CommandSource::keyboard);
            else {
                send(Navigate{Direction::down}, CommandSource::gamepad);
                require(h.focused() == id, "Gamepad Boolean control navigation differs");
                send(Activate{}, CommandSource::gamepad);
            }
        } else take(h.invoke(id, CommandSource::accessibility, f.text));
        require(state(h).first != initial && h.setting_attempts() == 1 && h.setting_changes() == 1 && h.camera_events() == 0 &&
            h.numeric().accepted == 1 && h.numeric().draft.text == "1" && h.msaa().accepted == 1 && peer(h) == std::pair{!initial, true}, "Boolean control route parity/once-only differs");
        f.verify();
    }
    Fixture f;
    auto& h = *f.host;
    auto send = [&](auto event, CommandSource source = CommandSource::keyboard, TextShaper* text = nullptr) {
        return h.route({std::chrono::microseconds{++f.tick}, std::move(event)}, source, text ? *text : f.text);
    };
    auto reveal = [&](const std::string& id) {
        for (int i = 0; h.focused() != id && i < 20; ++i) take(send(KeyDown{Key::tab}));
        require(h.focused() == id && visible(h.view(), id), "Boolean control reveal failed: " + id);
    };
    auto invoke = [&](const std::string& id) { take(h.invoke(id, CommandSource::agent, f.text)); };
    auto execute = [&](const CommandInvocation& call, TextShaper& text, bool success = true) {
        auto result = take(h.execute(call, text));
        require((result.outcome.status == CommandOutcomeStatus::success) == success, "Unexpected Boolean control outcome");
        return result;
    };
    auto eligibility = [&](bool enabled) {
        const auto attempts = h.setting_attempts(), changes = h.setting_changes();
        take(eligible(h, enabled, f.text));
        require(h.setting_attempts() == attempts && h.setting_changes() == changes, "Boolean control eligibility requested a setting");
    };
    const auto pristine = f.stable();
    // Schema, field identity, owner and presentation rejection must precede handler entry.
    for (const CommandValue value : {CommandValue{1.0}, CommandValue{std::string{"true"}}})
        require(!h.view().commands.resolve({h.view().commands.generation(), field + ".set", {{"field", field},
            {"value", value}}, CommandSource::agent, 999}), "Boolean control coerced a non-Boolean");
    for (const CommandArguments args : {CommandArguments{}, CommandArguments{{"field", field}},
        CommandArguments{{"value", true}}, CommandArguments{{"field", field}, {"value", true}, {"extra", false}}})
        require(!h.view().commands.resolve({h.view().commands.generation(), field + ".set", args,
            CommandSource::agent, 999}), "Boolean set accepted missing/extra fields");
    for (const auto& wrong : {std::string{"direct"}, std::string{"msaa"}, std::string{}, std::string{"scene-lights"}, scene_lights ? std::string{"outlines"} : std::string{"scene_lights"}})
        require(!set(h, wrong, true), "Boolean control accepted another field");
    const auto action = take(request_semantic_action(h.view().semantic(), *h.view().tree->find(id), "activate"));
    for (const CommandArguments args : {CommandArguments{{"value", true}}, CommandArguments{{"field", std::string{"direct"}}}})
        require(!h.view().commands.resolve_action(h.view().semantic(), action, h.view().commands.generation(),
            args, CommandSource::agent, 999), "Boolean control activation accepted arguments");
    Fixture foreign;
    require(h.view().commands.generation() == foreign.host->view().commands.generation(), "Foreign counter setup differs");
    const auto foreign_call = take(foreign.host->request(id, CommandSource::agent));
    const auto foreign_set = take(set(*foreign.host, field, true));
    const auto foreign_action = take(request_semantic_action(foreign.host->view().semantic(),
        *foreign.host->view().tree->find(id), "activate"));
    require(!h.execute(foreign_call, f.text) && !h.execute(foreign_set, f.text) &&
        !h.view().commands.resolve_action(h.view().semantic(), foreign_action, h.view().commands.generation(), {}, CommandSource::agent, 999) &&
        h.setting_attempts() == 0 && pristine == f.stable(), "Foreign Boolean control owner/presentation entered handler");
    const auto equal_generation = h.view().commands.generation();
    const auto equal = take(set(h, field, initial));
    execute(equal, f.text);
    require(h.setting_attempts() == 1 && h.setting_changes() == 0 && h.view().commands.generation() > equal_generation &&
        !h.execute(equal, f.text), "Equal Boolean accounting/sequence differs");
    // Retain incomplete bytes, reverse selection, lifetime owner/revision, strength and MSAA.
    auto edit = [&](auto operation) {
        const auto d = h.numeric().draft;
        execute(take(h.request_edit({d.owner, d.revision, std::move(operation)}, CommandSource::agent)), f.text);
    };
    edit(TextEditSelection{0, 1}); edit(TextEditInsert{"2."}); edit(TextEditSelection{2, 0});
    const auto pending = h.numeric(); const auto msaa = h.msaa();
    const auto other = peer(h);
    reveal("direct-input");
    const auto held = take(h.request(id, CommandSource::agent));
    invoke(id);
    require(h.numeric() == pending && h.msaa() == msaa && peer(h) == other && h.focused() == "direct-input", "Boolean control reset unrelated state/focus");
    const auto after = f.stable(); const auto attempts = h.setting_attempts();
    require(!h.execute(held, f.text) && after == f.stable() && attempts == h.setting_attempts(), "Stale Boolean control entered handler");
    reveal(id);
    for (const auto& snapshot : {panel::sample_a(), panel::sample_b(), panel::sample_c()}) {
        take(h.refresh(snapshot, f.text));
        require(h.numeric() == pending && h.msaa() == msaa && peer(h) == other && state(h).first != initial && h.focused() == id &&
            visible(h.view(), id) && h.setting_attempts() == attempts, "Refresh changed toggle/draft/focus");
        f.verify();
    }
    const auto disabled_call = take(set(h, field, initial));
    eligibility(false);
    require(!h.execute(disabled_call, f.text) && !set(h, field, initial) &&
        !h.request(id, CommandSource::agent) && h.focused() != id &&
        h.focused() && visible(h.view(), *h.focused()), "Disabled Boolean control eligibility/recovery differs");
    require(h.numeric() == pending && h.msaa() == msaa && peer(h) == other && h.view().commands.lookup("direct.apply").value->state.enabled &&
        h.view().commands.lookup("msaa.set").value->state.enabled, "Boolean control eligibility affected other settings");
    f.verify();
    require(peer(h) == other && h.view().commands.lookup((scene_lights ? "outlines" : "scene_lights") + std::string{".toggle"}).value->state.enabled,
        "Toggle eligibility disabled its peer");
    take(h.invoke(scene_lights ? "outlines-toggle" : "scene-lights-toggle", CommandSource::agent, f.text));
    require(state(h) == std::pair{!initial, false} && h.numeric() == pending && h.msaa() == msaa,
        "Peer operation changed disabled toggle/draft");
    take(h.invoke(scene_lights ? "outlines-toggle" : "scene-lights-toggle", CommandSource::agent, f.text));
    eligibility(true);
    take(scene_lights ? h.outlines_eligibility(false, f.text) : h.scene_lights_eligibility(false, f.text));
    take(h.numeric_eligibility(false, f.text)); take(h.msaa_capabilities({1, 2, 4}, false, f.text));
    require(h.view().commands.lookup(field + ".toggle").value->state.enabled &&
        h.view().commands.lookup(field + ".set").value->state.enabled, "Other settings disabled toggle");
    invoke(id);
    require(state(h).first == initial && h.numeric().draft == pending.draft && h.numeric().accepted == pending.accepted,
        "Independent Boolean control operation changed draft");
    require(peer(h) == std::pair{other.first, false}, "Independent toggle mutated peer");
    f.verify();
    take(scene_lights ? h.outlines_eligibility(true, f.text) : h.scene_lights_eligibility(true, f.text));
    take(h.numeric_eligibility(true, f.text)); take(h.msaa_capabilities({1, 2, 4}, true, f.text));
    reveal(id);
    const auto hidden_action = take(h.request(id, CommandSource::agent));
    const auto hidden_set = take(set(h, field, !initial));
    invoke("disclosure-controls");
    const auto collapsed = f.stable(); const auto collapsed_attempts = h.setting_attempts();
    require(!h.execute(hidden_action, f.text) && !h.execute(hidden_set, f.text) && collapsed == f.stable() &&
        h.setting_attempts() == collapsed_attempts, "Hidden toggle entered handler");
    require(h.focused() == "disclosure-controls" && visible(h.view(), "disclosure-controls") && h.numeric() == pending && h.msaa() == msaa && peer(h) == other &&
        !h.request(id, CommandSource::agent) && !set(h, field, !initial), "Boolean control collapse policy differs");
    f.verify(); invoke("disclosure-controls");
    require(h.focused() == "disclosure-controls" && h.numeric() == pending, "Expansion lost header focus/draft");
    f.verify();
    // Both preparation stages retain every accepted observation; failed sequences cannot be reused.
    for (bool paint : {false, true}) {
        RejectingText rejecting(paint);
        for (bool node_free : {false, true}) {
            const auto before = f.stable(); const auto count = h.setting_attempts(), changes = h.setting_changes();
            const auto call = node_free ? take(set(h, field, !state(h).first)) :
                take(h.request(id, CommandSource::agent));
            const auto result = execute(call, rejecting, false);
            require(before == f.stable() && count + 1 == h.setting_attempts() && changes == h.setting_changes() &&
                result.outcome.diagnostics.size() <= max_command_result_diagnostics &&
                result.outcome.diagnostics.front().path.find("/nodes/") != std::string::npos &&
                result.outcome.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure") &&
                !h.execute(call, f.text), "Failed Boolean control stage leaked state or reused sequence");
            const auto retry = node_free ? take(set(h, field, !state(h).first)) :
                take(h.request(id, CommandSource::agent));
            execute(retry, f.text);
            require(h.setting_attempts() == count + 2 && h.setting_changes() == changes + 1 && h.numeric() == pending && h.msaa() == msaa && peer(h) == other,
                "Boolean control retry accounting/retention differs");
            f.verify();
        }
        for (bool enabled : {false, true}) {
            eligibility(!enabled);
            const auto before = f.stable(); const auto count = h.setting_attempts();
            const auto rejected = eligible(h, enabled, rejecting);
            require(!rejected && before == f.stable() && count == h.setting_attempts() &&
                !rejected.diagnostics.empty() && rejected.diagnostics.size() <= max_command_result_diagnostics &&
                rejected.diagnostics.front().path.starts_with("/nodes/") &&
                rejected.diagnostics.front().code == (paint ? "injected_paint" : "injected_measure"), "Failed Boolean control eligibility leaked state");
            eligibility(enabled);
        }
        eligibility(true);
    }
    reveal(id);
    auto point = [&] { const auto b = box(h.view(), id).border_box; return Point{b.origin.x + 2, b.origin.y + 2}; };
    const auto p = point(); const auto camera = h.camera_events(), calls = h.setting_attempts();
    take(send(PointerDown{{61}, p}, CommandSource::pointer)); eligibility(false);
    take(send(PointerMove{{61}, {800, 200}}, CommandSource::pointer));
    take(send(PointerUp{{61}, {800, 200}}, CommandSource::pointer));
    require(h.last_consumed() && h.setting_attempts() == calls, "Replacement lost stream claim or retained activation");
    take(send(Scroll{{16, 16}, {0, 4096}}));
    const auto disabled_point = point();
    take(send(PointerDown{{62}, disabled_point}, CommandSource::pointer)); take(send(PointerUp{{62}, disabled_point}, CommandSource::pointer));
    require(h.setting_attempts() == calls && h.camera_events() == camera, "Disabled Boolean control hit reached handler/camera");
    eligibility(true); reveal(id);
    const auto current = point();
    take(send(PointerDown{{63}, current}, CommandSource::pointer)); take(h.refresh(panel::sample_a(), f.text));
    take(send(PointerMove{{63}, {800, 200}}, CommandSource::pointer)); take(send(PointerCancel{{63}}, CommandSource::pointer));
    require(h.last_consumed() && h.setting_attempts() == calls, "Replaced Boolean control cancel escaped arbitration");
    reveal(id);
    RejectingText rejecting(false);
    const auto before = f.stable();
    require(!send(KeyDown{Key::enter}, CommandSource::keyboard, &rejecting) && h.last_consumed() &&
        before == f.stable() && h.setting_attempts() == calls + 1 && h.camera_events() == camera, "Failed raw Boolean control input leaked state/camera");
    take(send(KeyDown{Key::enter}));
    require(h.setting_attempts() == calls + 2, "Raw activation duplicated logical activation");
    const auto release_point = point();
    take(send(PointerDown{{64}, release_point}, CommandSource::pointer));
    const auto pressed = f.stable(); const auto pressed_calls = h.setting_attempts();
    require(!send(PointerUp{{64}, release_point}, CommandSource::pointer, &rejecting) && h.last_consumed() && pressed == f.stable(),
        "Failed Boolean control release changed accepted state");
    take(send(PointerUp{{64}, release_point}, CommandSource::pointer));
    require(h.setting_attempts() == pressed_calls + 1 && h.camera_events() == camera && h.numeric() == pending && h.msaa() == msaa && peer(h) == other,
        "Failed Boolean control release retriggered or changed unrelated state");
    take(send(Scroll{{16, 16}, {0, 4096}}));
    require(h.offset() == box(h.view(), "renderer-diagnostics").scroll_limit(), "Boolean control bottom clamp differs");
    f.verify();
    std::cout << field << ": " << f.observations << " Boolean control views match CPU/replay; 4 equal activation routes; " << h.setting_attempts() <<
        " setting attempts, " << h.setting_changes() << " accepted changes; " << h.camera_events() << " camera deliveries\n";
}

}
int main() {
    try { run(); numeric_controls(); msaa_controls(); boolean_controls(false); boolean_controls(true); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
