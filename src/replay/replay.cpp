#include <tessera/replay/replay.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>
#include "../detail/checks.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

namespace tessera {
namespace {

void relocate(std::vector<Diagnostic>& out, std::vector<Diagnostic> diagnostics, const std::string& prefix) {
    for (auto& diagnostic : diagnostics) {
        diagnostic.path = prefix + diagnostic.path;
        out.push_back(std::move(diagnostic));
    }
}

// The current coherent tree/styles/layout. Only values are copied into observations.
struct Snapshot {
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput input() const { return {tree.get(), styles, &layout}; }
};

class Player {
public:
    Player(const ReplayRecording& recording, TextShaper& text) : recording_(recording), text_(text) {}

    Result<ReplayOutput> run() {
        if (!load(recording_.document, recording_.styles, "") || !settle({}, "")) return fail();
        for (std::size_t i = 0; i < recording_.steps.size(); ++i) {
            const auto at = "/steps/" + std::to_string(i);
            const auto& step = recording_.steps[i];
            bool ok = true;
            if (const auto* event = std::get_if<InputEvent>(&step)) {
                ok = std::holds_alternative<Scroll>(event->data) ? scroll(*event, i, at) : dispatch(*event, i, at);
            } else if (const auto* resize = std::get_if<ReplayResize>(&step)) {
                viewport_ = resize->viewport;
                ok = settle(i, at);
            } else if (const auto* reload = std::get_if<ReplayReload>(&step)) {
                ok = load(reload->document, reload->styles, at) && settle(i, at);
            }
            if (!ok) return fail();
        }
        return {std::move(output_), {}};
    }

private:
    Result<ReplayOutput> fail() { return {std::nullopt, std::move(errors_)}; }

    bool load(const UiDocument& document, const std::vector<ResolvedStyle>& styles, const std::string& at) {
        auto created = UiTree::create(document, recording_.context);
        if (!created) {
            relocate(errors_, std::move(created.diagnostics), at + "/document");
            return false;
        }
        current_.tree = std::move(*created.value);
        current_.styles = styles;
        offsets_.assign(current_.tree->size(), Point{});
        return true;
    }

    // Full-tree layout and paint at an update point, then re-target stationary pointers.
    bool settle(std::optional<std::size_t> step, const std::string& at) {
        auto layout = compute_layout({current_.tree.get(), current_.styles, viewport_, &text_, offsets_});
        if (!layout) {
            relocate(errors_, std::move(layout.diagnostics), at);
            return false;
        }
        current_.layout = std::move(*layout.value);
        auto paint = build_paint_list({current_.tree.get(), current_.styles, &current_.layout, &text_});
        if (!paint) {
            relocate(errors_, std::move(paint.diagnostics), at);
            return false;
        }
        auto refreshed = input_.refresh(current_.input());
        if (!refreshed) {
            relocate(errors_, std::move(refreshed.diagnostics), at);
            return false;
        }
        ReplayGeneration generation{step, {}, std::move(*paint.value)};
        generation.boxes.reserve(current_.layout.boxes.size());
        for (const auto& box : current_.layout.boxes)
            generation.boxes.push_back(
                {box.node.index, box.parent, box.border_box, box.border, box.padding, box.visible, box.clip, box.scroll});
        output_.generations.push_back(std::move(generation));
        return true;
    }

    // Rejects time running backwards across pointer and scroll steps, which share one stream.
    bool ordered(const InputEvent& event, const std::string& at) {
        if (!clock_ || event.timestamp >= *clock_) {
            clock_ = event.timestamp;
            return true;
        }
        errors_.push_back({"event_order", Severity::error, at + "/timestamp",
                           "Send non-decreasing timestamps across pointer and scroll steps.", {}});
        return false;
    }

    // Routes the scroll against the current snapshot, stores the new offsets, and settles a new generation.
    bool scroll(const InputEvent& event, std::size_t step, const std::string& at) {
        auto invalid = validate(event);
        if (!invalid.empty()) {
            relocate(errors_, std::move(invalid), at);
            return false;
        }
        if (!ordered(event, at)) return false;
        auto routed = route_scroll(current_.input(), std::get<Scroll>(event.data));
        if (!routed) {
            relocate(errors_, std::move(routed.diagnostics), at);
            return false;
        }
        for (const auto& update : routed.value->updates) offsets_[update.container.index] = update.offset;
        return settle(step, at);
    }

    bool dispatch(const InputEvent& event, std::size_t step, const std::string& at) {
        auto result = input_.dispatch(current_.input(), event);
        if (!result) {
            relocate(errors_, std::move(result.diagnostics), at);
            return false;
        }
        // The dispatcher checks time only within pointer steps; a scroll step may have advanced the clock.
        if (!ordered(event, at)) return false;
        for (auto& request : result.value->actions) {
            output_.actions.push_back({step, output_.generations.size() - 1, std::move(request.binding),
                                       std::move(request.action), request.target.index,
                                       current_.tree->get(request.target)->id});
        }
        return true;
    }

    const ReplayRecording& recording_;
    TextShaper& text_;
    Size viewport_ = recording_.viewport;
    Snapshot current_;
    std::vector<Point> offsets_; // Requested scroll offsets by node index; reset by reload.
    std::optional<std::chrono::microseconds> clock_;
    PointerDispatcher input_;
    ReplayOutput output_;
    std::vector<Diagnostic> errors_;
};

std::string format(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
std::string format(const Rect& rect) {
    return "(x " + format(rect.origin.x) + ", y " + format(rect.origin.y) + ", w " + format(rect.size.width) +
           ", h " + format(rect.size.height) + ")";
}
std::string format(const Edges& edges) {
    return "(top " + format(edges.top) + ", right " + format(edges.right) + ", bottom " + format(edges.bottom) +
           ", left " + format(edges.left) + ")";
}
std::string format(const ScrollGeometry& scroll) {
    return "(extent " + format(scroll.extent.width) + "x" + format(scroll.extent.height) + ", offset " +
           format(scroll.offset.x) + ", " + format(scroll.offset.y) + ")";
}
std::string format(const ReplayAction& action) {
    return action.binding + " '" + action.action + "' from " + (action.id ? "'" + *action.id + "'" : "node") +
           " #" + std::to_string(action.node) + " at step " + std::to_string(action.step) + " (generation " +
           std::to_string(action.generation) + ")";
}

class Comparison {
public:
    Comparison(std::vector<Diagnostic>& out, float tolerance) : out_(out), tolerance_(tolerance) {}

    void mismatch(std::string path, std::string message) {
        out_.push_back({"replay_mismatch", Severity::error, std::move(path), std::move(message), {}});
    }
    bool count(std::size_t expected, std::size_t actual, const std::string& path, std::string_view noun) {
        if (expected == actual) return true;
        mismatch(path, "Expected " + std::to_string(expected) + " " + std::string(noun) + ", got " +
                       std::to_string(actual) + ".");
        return false;
    }
    bool near(float expected, float actual) const {
        return expected == actual || std::fabs(static_cast<double>(expected) - actual) <= tolerance_;
    }
    bool near(const Rect& expected, const Rect& actual) const {
        return near(expected.origin.x, actual.origin.x) && near(expected.origin.y, actual.origin.y) &&
               near(expected.size.width, actual.size.width) && near(expected.size.height, actual.size.height);
    }
    bool near(const ScrollGeometry& expected, const ScrollGeometry& actual) const {
        return near(expected.extent.width, actual.extent.width) && near(expected.extent.height, actual.extent.height) &&
               near(expected.offset.x, actual.offset.x) && near(expected.offset.y, actual.offset.y);
    }
    bool near(const Edges& expected, const Edges& actual) const {
        return near(expected.top, actual.top) && near(expected.right, actual.right) &&
               near(expected.bottom, actual.bottom) && near(expected.left, actual.left);
    }

    void box(const ReplayBox& expected, const ReplayBox& actual, const std::string& path) {
        if (expected.node != actual.node)
            mismatch(path + "/node", "Expected node #" + std::to_string(expected.node) + ", got #" +
                                     std::to_string(actual.node) + ".");
        if (expected.parent != actual.parent) mismatch(path + "/parent", "Box parent index differs.");
        if (!near(expected.border_box, actual.border_box))
            mismatch(path + "/border_box", "Expected " + format(expected.border_box) + ", got " +
                                           format(actual.border_box) + ".");
        if (!near(expected.border, actual.border))
            mismatch(path + "/border", "Expected " + format(expected.border) + ", got " + format(actual.border) + ".");
        if (!near(expected.padding, actual.padding))
            mismatch(path + "/padding", "Expected " + format(expected.padding) + ", got " +
                                        format(actual.padding) + ".");
        if (expected.visible != actual.visible)
            mismatch(path + "/visible", expected.visible ? "Expected a visible box." : "Expected a hidden box.");
        if (expected.clip.has_value() != actual.clip.has_value())
            mismatch(path + "/clip", expected.clip ? "Expected a clipped box." : "Expected an unclipped box.");
        else if (expected.clip && !near(*expected.clip, *actual.clip))
            mismatch(path + "/clip", "Expected " + format(*expected.clip) + ", got " + format(*actual.clip) + ".");
        if (expected.scroll.has_value() != actual.scroll.has_value())
            mismatch(path + "/scroll", expected.scroll ? "Expected a scroll box." : "Expected a box without scrolling.");
        else if (expected.scroll && !near(*expected.scroll, *actual.scroll))
            mismatch(path + "/scroll", "Expected " + format(*expected.scroll) + ", got " + format(*actual.scroll) + ".");
    }

    void generation(const ReplayGeneration& expected, const ReplayGeneration& actual, const std::string& path) {
        if (expected.step != actual.step) mismatch(path + "/step", "Generation was produced by a different step.");
        const auto boxes = path + "/boxes";
        count(expected.boxes.size(), actual.boxes.size(), boxes, "boxes");
        for (std::size_t i = 0; i < std::min(expected.boxes.size(), actual.boxes.size()); ++i)
            box(expected.boxes[i], actual.boxes[i], boxes + "/" + std::to_string(i));
        const auto commands = path + "/paint/commands";
        const auto& want = expected.paint.commands;
        const auto& got = actual.paint.commands;
        count(want.size(), got.size(), commands, "paint commands");
        for (std::size_t i = 0; i < std::min(want.size(), got.size()); ++i) {
            if (want[i] != got[i])
                mismatch(commands + "/" + std::to_string(i), "Paint command differs from the expected owned value.");
        }
    }

private:
    std::vector<Diagnostic>& out_;
    float tolerance_;
};

} // namespace

Result<ReplayOutput> play_replay(const ReplayRecording& recording, TextShaper& text) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (recording.version != replay_prototype_version)
        check.error("unsupported_version", "/version", "Only replay prototype version 0 is supported; re-record explicitly.");
    if (recording.steps.size() > max_replay_steps)
        check.error("out_of_range", "/steps", "Use at most " + std::to_string(max_replay_steps) + " replay steps.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return Player(recording, text).run();
}

std::vector<Diagnostic> compare_replay(const ReplayOutput& expected, const ReplayOutput& actual,
                                       float geometry_tolerance) {
    std::vector<Diagnostic> errors;
    detail::Checker(errors).non_negative(geometry_tolerance, "/geometry_tolerance");
    if (!errors.empty()) return errors;
    Comparison compare(errors, geometry_tolerance);
    compare.count(expected.generations.size(), actual.generations.size(), "/generations", "generations");
    for (std::size_t i = 0; i < std::min(expected.generations.size(), actual.generations.size()); ++i)
        compare.generation(expected.generations[i], actual.generations[i], "/generations/" + std::to_string(i));
    compare.count(expected.actions.size(), actual.actions.size(), "/actions", "action requests");
    for (std::size_t i = 0; i < std::min(expected.actions.size(), actual.actions.size()); ++i) {
        if (expected.actions[i] != actual.actions[i])
            compare.mismatch("/actions/" + std::to_string(i), "Expected " + format(expected.actions[i]) + ", got " +
                                                                  format(actual.actions[i]) + ".");
    }
    return errors;
}

} // namespace tessera
