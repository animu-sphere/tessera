#include "../../examples/renderer-diagnostics/host.hpp"
#include "../../examples/renderer-diagnostics/sample_data.hpp"
#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>

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
            view.tree->document(), view.commands.generation(), view.inspection, geometry(view), view.paint};
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
    for (const auto* group : panel::groups) {
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
    // An unmatched release may be unconsumed after replacement, but must emit no command.
    (void)take(host.input({std::chrono::microseconds{++fixture.tick}, PointerUp{{3}, pressed_point}}, CommandSource::pointer, fixture.text));
    fixture.verify();
    require(host.disclosure_requests() == prior_calls, "Pointer press survived replacement");
    std::cout << fixture.observations << " telemetry views match repeated geometry/semantics/paint/CPU replay; " <<
        host.disclosure_requests() << " disclosure attempts; synthetic inputs only\n";
}
}
int main() {
    try { run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
