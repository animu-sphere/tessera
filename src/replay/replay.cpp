#include <tessera/replay/replay.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>
#include "replay_detail.hpp"
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
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

bool is_command(const InputEvent& event) {
    return std::holds_alternative<FocusNext>(event.data) || std::holds_alternative<FocusPrevious>(event.data) ||
           std::holds_alternative<Navigate>(event.data) || std::holds_alternative<Activate>(event.data) ||
           std::holds_alternative<Cancel>(event.data);
}

// Child-index path from the root to a node of the tree.
std::vector<std::uint32_t> path_to(const UiTree& tree, NodeHandle target) {
    std::vector<std::uint32_t> path;
    auto current = tree.root();
    while (current != target) {
        const auto children = tree.children(current);
        // Preorder indices: the target lies under the last child that starts at or before it.
        std::uint32_t child = 0;
        while (child + 1 < children.size() && children[child + 1].index <= target.index) ++child;
        path.push_back(child);
        current = children[child];
    }
    return path;
}

} // namespace

namespace detail {

// Plays steps against one current coherent snapshot. A step rejected before its commit point leaves every
// member as it was; a failure after it closes the player.
class ReplayPlayer {
public:
    ReplayPlayer(ReplayRecording recording, TextShaper& text)
        : recording_(std::move(recording)), text_(text), viewport_(recording_.viewport) {}

    // Settles the initial snapshot and then applies every recorded step.
    bool run() {
        auto steps = std::move(recording_.steps);
        recording_.steps.clear();
        if (!load(recording_.document, recording_.styles, "") || !settle({}, "")) return false;
        for (auto& step : steps) {
            if (!apply(std::move(step))) return false;
        }
        return true;
    }

    // Applies one step as /steps/<n>, where n counts the steps accepted so far.
    bool apply(ReplayStep next) {
        errors_.clear();
        const auto i = recording_.steps.size();
        const auto at = "/steps/" + std::to_string(i);
        if (!open(at)) return false;
        if (i >= max_replay_steps) {
            errors_.push_back({"out_of_range", Severity::error, "/steps",
                               "Use at most " + std::to_string(max_replay_steps) + " replay steps.", {}});
            return false;
        }
        recording_.steps.push_back(std::move(next));
        const auto& step = recording_.steps.back();
        const auto clock = clock_;
        const auto viewport = viewport_;
        const auto offsets = offsets_;
        committed_ = false;
        bool ok = true;
        if (const auto* event = std::get_if<InputEvent>(&step)) {
            if (std::holds_alternative<Scroll>(event->data)) ok = scroll(*event, i, at);
            else if (is_command(*event)) ok = command(*event, i, at);
            else ok = dispatch(*event, i, at);
        } else if (const auto* resize = std::get_if<ReplayResize>(&step)) {
            ok = resize_to(resize->viewport, i, at);
        } else if (const auto* reload = std::get_if<ReplayReload>(&step)) {
            ok = replace(*reload, i, at);
        } else if (const auto* focus = std::get_if<ReplayFocus>(&step)) {
            ok = focus_on(*focus, i, at);
        } else {
            ok = invoke(std::get<ReplaySemanticAction>(step), i, at);
        }
        if (ok) return true;
        recording_.steps.pop_back();
        if (committed_) {
            closed_ = true;
        } else {
            clock_ = clock;
            viewport_ = viewport;
            offsets_ = offsets;
        }
        return false;
    }

    // Records a handle from the current tree as an author-ID target, else as a child-index path.
    std::optional<InspectionTarget> target(NodeHandle handle) {
        errors_.clear();
        const auto at = "/steps/" + std::to_string(recording_.steps.size());
        if (!open(at)) return std::nullopt;
        const auto* node = current_.tree->get(handle);
        if (!node) {
            errors_.push_back({"stale_target", Severity::error, at + "/target",
                               "Capture the current generation and resolve the target again; this handle belongs to "
                               "another tree.", {}});
            return std::nullopt;
        }
        if (node->id) return AuthorIdTarget{*node->id};
        return PathTarget{path_to(*current_.tree, handle)};
    }

    Result<InspectionSnapshot> capture(const DocumentSourceMap* sources) {
        errors_.clear();
        if (!open("")) return {std::nullopt, std::move(errors_)};
        return capture_inspection({current_.tree.get(), current_.styles, &current_.layout, sources, focused_});
    }

    bool closed() const noexcept { return closed_; }
    const ReplayRecording& recording() const noexcept { return recording_; }
    const ReplayOutput& output() const noexcept { return output_; }
    ReplayOutput take_output() { return std::move(output_); }
    std::vector<Diagnostic> take_errors() { return std::move(errors_); }
    const std::vector<Diagnostic>& warnings() const noexcept { return warnings_; }
    std::vector<Diagnostic> take_warnings() { return std::move(warnings_); }

private:
    // The current coherent tree/styles/layout. Only values are copied into observations.
    struct Snapshot {
        std::unique_ptr<UiTree> tree;
        std::vector<ResolvedStyle> styles;
        LayoutResult layout;
        HitTestInput input() const { return {tree.get(), styles, &layout}; }
    };

    bool open(const std::string& at) {
        if (!closed_) return true;
        errors_.push_back({"closed_session", Severity::error, at,
                           "An earlier step failed after changing the session; open a new session from recording().",
                           {}});
        return false;
    }

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

    // Loads a replacement document and settles it, revealing recovered focus under the host policy since
    // the reload reset every offset; the previous snapshot returns if neither commits.
    bool replace(const ReplayReload& reload, std::size_t step, const std::string& at) {
        auto previous = std::move(current_);
        if (load(reload.document, reload.styles, at) && settle(step, at, recording_.policy.reveal_focus)) return true;
        if (!committed_) current_ = std::move(previous);
        return false;
    }

    // Lays out the new viewport. Under the host reveal policy, focus that was in view, so that revealing it
    // would move nothing, is revealed again before the resize's generation is recorded; focus the user
    // scrolled away from stays where it is.
    bool resize_to(Size viewport, std::size_t step, const std::string& at) {
        bool keep = false;
        if (recording_.policy.reveal_focus && focused_) {
            auto revealed = scroll_into_view(current_.input(), *focused_);
            if (!revealed) {
                relocate(errors_, std::move(revealed.diagnostics), at);
                return false;
            }
            keep = revealed.value->empty();
        }
        viewport_ = viewport;
        return settle(step, at, keep);
    }

    // Full-tree layout and paint at an update point, then re-target stationary pointers and recover focus.
    // The new layout is the commit point. With `reveal`, recovered focus is scrolled into view and laid
    // out again before the generation is recorded, so no generation shows it hidden.
    bool settle(std::optional<std::size_t> step, const std::string& at, bool reveal = false) {
        auto layout = compute_layout({current_.tree.get(), current_.styles, viewport_, &text_, offsets_});
        if (!layout) {
            relocate(errors_, std::move(layout.diagnostics), at);
            return false;
        }
        auto paint = build_paint_list({current_.tree.get(), current_.styles, &*layout.value, &text_});
        if (!paint) {
            relocate(errors_, std::move(paint.diagnostics), at);
            return false;
        }
        committed_ = true;
        current_.layout = std::move(*layout.value);
        auto refreshed = input_.refresh(current_.input());
        if (!refreshed) {
            relocate(errors_, std::move(refreshed.diagnostics), at);
            return false;
        }
        auto recovered = focus_.refresh(current_.input());
        if (!recovered) {
            relocate(errors_, std::move(recovered.diagnostics), at);
            return false;
        }
        focused_ = recovered.value->focused;
        if (reveal && focused_) {
            auto revealed = scroll_into_view(current_.input(), *focused_);
            if (!revealed) {
                relocate(errors_, std::move(revealed.diagnostics), at);
                return false;
            }
            if (!revealed.value->empty()) {
                for (const auto& update : *revealed.value) offsets_[update.container.index] = update.offset;
                return settle(step, at);
            }
        }
        ReplayGeneration generation{step, {}, std::move(*paint.value)};
        generation.boxes.reserve(current_.layout.boxes.size());
        for (const auto& box : current_.layout.boxes)
            generation.boxes.push_back(
                {box.node.index, box.parent, box.border_box, box.border, box.padding, box.visible, box.clip, box.scroll});
        output_.generations.push_back(std::move(generation));
        return observe(step, at);
    }

    // Projects the current generation with the current focus.
    bool observe(std::optional<std::size_t> step, const std::string& at) {
        auto projected = build_semantic_tree({current_.tree.get(), current_.styles, &current_.layout, focused_});
        if (!projected) {
            relocate(errors_, std::move(projected.diagnostics), at);
            return false;
        }
        relocate(warnings_, std::move(projected.diagnostics), at);
        ReplaySemantics semantics{step, output_.generations.size() - 1, {}, {}};
        if (focused_) semantics.focused = focused_->index;
        semantics.nodes.reserve(projected.value->nodes.size());
        for (auto& entry : projected.value->nodes) {
            std::optional<std::uint32_t> labelled_by;
            if (entry.labelled_by) labelled_by = entry.labelled_by->index;
            semantics.nodes.push_back({entry.node.index, std::move(entry.id), entry.parent, entry.role,
                                       std::move(entry.name), entry.name_source, labelled_by, entry.enabled,
                                       entry.focusable, entry.focused, std::move(entry.actions)});
        }
        output_.semantics.push_back(std::move(semantics));
        return true;
    }

    // Rejects time running backwards across pointer, scroll, and command steps, which share one stream.
    bool in_order(const InputEvent& event, const std::string& at) {
        if (!clock_ || event.timestamp >= *clock_) return true;
        errors_.push_back({"event_order", Severity::error, at + "/timestamp",
                           "Send non-decreasing timestamps across pointer, scroll, and command steps.", {}});
        return false;
    }

    // Routes the scroll against the current snapshot, stores the new offsets, and settles a new generation.
    bool scroll(const InputEvent& event, std::size_t step, const std::string& at) {
        auto invalid = validate(event);
        if (!invalid.empty()) {
            relocate(errors_, std::move(invalid), at);
            return false;
        }
        if (!in_order(event, at)) return false;
        auto routed = route_scroll(current_.input(), std::get<Scroll>(event.data));
        if (!routed) {
            relocate(errors_, std::move(routed.diagnostics), at);
            return false;
        }
        clock_ = event.timestamp;
        for (const auto& update : routed.value->updates) offsets_[update.container.index] = update.offset;
        return settle(step, at);
    }

    bool dispatch(const InputEvent& event, std::size_t step, const std::string& at) {
        if (!in_order(event, at)) return false;
        auto result = input_.dispatch(current_.input(), event);
        if (!result) {
            relocate(errors_, std::move(result.diagnostics), at);
            return false;
        }
        committed_ = true;
        clock_ = event.timestamp;
        record(step, result.value->actions);
        // Host press policy: a primary press also focuses its press owner when that owner is focusable.
        // Pointer dispatch itself never moves focus, and a press never reveals.
        const auto* down = std::get_if<PointerDown>(&event.data);
        if (!recording_.policy.press_focus || !down || down->button != PointerButton::primary) return true;
        for (const auto& state : result.value->pointers) {
            if (state.pointer != down->pointer || !state.pressed || state.pressed == focused_) continue;
            auto moved = focus_.focus(current_.input(), *state.pressed);
            if (!moved) {
                if (moved.diagnostics.size() == 1 && moved.diagnostics[0].code == "not_focusable") return true;
                relocate(errors_, std::move(moved.diagnostics), at);
                return false;
            }
            focused_ = moved.value->focused;
            return observe(step, at);
        }
        return true;
    }

    // Moves focus or requests an action through focus dispatch, then observes semantics with the new focus.
    bool command(const InputEvent& event, std::size_t step, const std::string& at) {
        if (!in_order(event, at)) return false;
        auto result = focus_.dispatch(current_.input(), event);
        if (!result) {
            relocate(errors_, std::move(result.diagnostics), at);
            return false;
        }
        committed_ = true;
        clock_ = event.timestamp;
        const auto previous = focused_;
        focused_ = result.value->focused;
        record(step, result.value->actions);
        return reveal(previous, step, at);
    }

    // Resolves a fixture-identity target in the current generation; failures are located under the step.
    std::optional<NodeHandle> resolve(const InspectionTarget& target, const std::string& at) {
        auto captured = capture_inspection({current_.tree.get(), current_.styles, &current_.layout, nullptr, focused_});
        if (!captured) {
            relocate(errors_, std::move(captured.diagnostics), at);
            return std::nullopt;
        }
        auto resolved = resolve_target(*captured.value, target);
        if (!resolved) {
            relocate(errors_, std::move(resolved.diagnostics), at);
            return std::nullopt;
        }
        return *resolved.value;
    }

    // Host programmatic focus on a resolved target, observed (and revealed) like a command.
    bool focus_on(const ReplayFocus& step_focus, std::size_t step, const std::string& at) {
        const auto target = resolve(step_focus.target, at);
        if (!target) return false;
        auto result = focus_.focus(current_.input(), *target);
        if (!result) {
            relocate(errors_, std::move(result.diagnostics), at);
            return false;
        }
        committed_ = true;
        const auto previous = focused_;
        focused_ = result.value->focused;
        return reveal(previous, step, at);
    }

    // Semantic invocation on a resolved target; it requests what a pointer activation requests.
    bool invoke(const ReplaySemanticAction& action, std::size_t step, const std::string& at) {
        const auto target = resolve(action.target, at);
        if (!target) return false;
        auto request = request_semantic_action({current_.tree.get(), current_.styles, &current_.layout, focused_},
                                               *target, action.binding);
        if (!request) {
            relocate(errors_, std::move(request.diagnostics), at);
            return false;
        }
        committed_ = true;
        std::vector<ActionRequest> requests{std::move(*request.value)};
        record(step, requests);
        return true;
    }

    // Host reveal policy: focus moved to another node scrolls it into view. Changed offsets settle a new
    // generation, which observes semantics; otherwise the current generation is observed.
    bool reveal(std::optional<NodeHandle> previous, std::size_t step, const std::string& at) {
        if (!recording_.policy.reveal_focus || !focused_ || focused_ == previous) return observe(step, at);
        auto revealed = scroll_into_view(current_.input(), *focused_);
        if (!revealed) {
            relocate(errors_, std::move(revealed.diagnostics), at);
            return false;
        }
        if (revealed.value->empty()) return observe(step, at);
        for (const auto& update : *revealed.value) offsets_[update.container.index] = update.offset;
        return settle(step, at);
    }

    void record(std::size_t step, std::vector<ActionRequest>& requests) {
        for (auto& request : requests) {
            output_.actions.push_back({step, output_.generations.size() - 1, std::move(request.binding),
                                       std::move(request.action), request.target.index,
                                       current_.tree->get(request.target)->id});
        }
    }

    ReplayRecording recording_; // Accepted steps only.
    TextShaper& text_;
    Size viewport_;
    Snapshot current_;
    std::vector<Point> offsets_; // Requested scroll offsets by node index; reset by reload.
    std::optional<std::chrono::microseconds> clock_;
    PointerDispatcher input_;
    FocusDispatcher focus_;
    std::optional<NodeHandle> focused_; // Current focus in the current tree.
    ReplayOutput output_;
    std::vector<Diagnostic> errors_;   // From the latest call.
    std::vector<Diagnostic> warnings_; // From every accepted step.
    bool committed_ = false;           // The current step has changed the session.
    bool closed_ = false;
};

} // namespace detail

namespace {

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

std::string format(const ReplaySemanticNode& entry) {
    static constexpr const char* roles[] = {"root", "button", "text"};
    std::string result = std::string(roles[static_cast<int>(entry.role)]) + " '" + entry.name + "' " +
                         (entry.id ? "'" + *entry.id + "' " : "") + "#" + std::to_string(entry.node);
    if (!entry.enabled) result += ", disabled";
    if (entry.focusable) result += ", focusable";
    if (entry.focused) result += ", focused";
    return result + ", " + std::to_string(entry.actions.size()) + " actions";
}
std::string format(std::optional<std::uint32_t> focused) {
    return focused ? "focus on #" + std::to_string(*focused) : "no focus";
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

    void semantics(const ReplaySemantics& expected, const ReplaySemantics& actual, const std::string& path) {
        if (expected.step != actual.step) mismatch(path + "/step", "Semantics were observed at a different step.");
        if (expected.generation != actual.generation)
            mismatch(path + "/generation", "Semantics were observed in a different generation.");
        if (expected.focused != actual.focused)
            mismatch(path + "/focused", "Expected " + format(expected.focused) + ", got " + format(actual.focused) + ".");
        const auto nodes = path + "/nodes";
        count(expected.nodes.size(), actual.nodes.size(), nodes, "semantic entries");
        for (std::size_t i = 0; i < std::min(expected.nodes.size(), actual.nodes.size()); ++i) {
            if (expected.nodes[i] != actual.nodes[i])
                mismatch(nodes + "/" + std::to_string(i), "Expected " + format(expected.nodes[i]) + ", got " +
                                                              format(actual.nodes[i]) + ".");
        }
    }

private:
    std::vector<Diagnostic>& out_;
    float tolerance_;
};

} // namespace

namespace {

bool ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool ascii_alnum(char c) { return ascii_alpha(c) || (c >= '0' && c <= '9'); }

// Hyphen-separated subtags of 1-8 ASCII letters/digits; the first has 2-8 letters. No canonicalization.
bool locale_syntax(std::string_view locale) {
    if (locale.empty() || locale.size() > 64) return false;
    bool first = true;
    for (std::size_t begin = 0; begin <= locale.size();) {
        auto end = locale.find('-', begin);
        if (end == std::string_view::npos) end = locale.size();
        const auto subtag = locale.substr(begin, end - begin);
        if (subtag.empty() || subtag.size() > 8) return false;
        for (const auto c : subtag)
            if (first ? !ascii_alpha(c) : !ascii_alnum(c)) return false;
        if (first && subtag.size() < 2) return false;
        first = false;
        begin = end + 1;
    }
    return true;
}

void check_text(const ReplayText& text, detail::Checker& check) {
    check.enumeration(text.kind, ReplayText::Kind::font_profile, "/environment/text/kind");
    if (text.kind == ReplayText::Kind::placeholder) {
        if (!text.id.empty() || !text.sha256.empty())
            check.error("invalid_text_service", "/environment/text",
                        "Placeholder text declares no font profile; clear id and sha256.");
        return;
    }
    if (text.kind != ReplayText::Kind::font_profile) return;
    if (!detail::valid_utf8(text.id)) check.error("invalid_utf8", "/environment/text/id", "Use valid UTF-8.");
    else if (text.id.empty() || text.id.size() > 256 ||
             std::any_of(text.id.begin(), text.id.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        check.error("invalid_identifier", "/environment/text/id",
                    "Use 1-256 UTF-8 bytes without ASCII controls for the font profile identity.");
    if (text.sha256.size() != 64 || !std::all_of(text.sha256.begin(), text.sha256.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        check.error("invalid_digest", "/environment/text/sha256",
                    "Use the 64 lowercase hexadecimal digits of the profile's SHA-256.");
}

} // namespace

namespace detail {

std::vector<Diagnostic> check_recording(const ReplayRecording& recording) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    if (recording.version != replay_version)
        check.error("unsupported_version", "/version", "Only replay version 1 is supported; re-record explicitly.");
    if (recording.steps.size() > max_replay_steps)
        check.error("out_of_range", "/steps", "Use at most " + std::to_string(max_replay_steps) + " replay steps.");
    check.positive(recording.environment.scale, "/environment/scale");
    if (!locale_syntax(recording.environment.locale))
        check.error("invalid_locale", "/environment/locale",
                    "Use hyphen-separated subtags of 1-8 ASCII letters or digits, starting with 2-8 letters, "
                    "e.g. 'und' or 'ja-JP'.");
    check_text(recording.environment.text, check);
    return errors;
}

} // namespace detail

using detail::check_recording;

Result<ReplayOutput> play_replay(const ReplayRecording& recording, TextShaper& text) {
    auto errors = check_recording(recording);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    detail::ReplayPlayer player(recording, text);
    if (!player.run()) return {std::nullopt, player.take_errors()};
    return {player.take_output(), player.take_warnings()};
}

Result<ReplaySession> ReplaySession::open(ReplayRecording recording, TextShaper& text) {
    auto errors = check_recording(recording);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto player = std::make_unique<detail::ReplayPlayer>(std::move(recording), text);
    if (!player->run()) return {std::nullopt, player->take_errors()};
    auto warnings = player->warnings();
    return {ReplaySession(std::move(player)), std::move(warnings)};
}

ReplaySession::ReplaySession(std::unique_ptr<detail::ReplayPlayer> player) : player_(std::move(player)) {}
ReplaySession::ReplaySession(ReplaySession&&) noexcept = default;
ReplaySession& ReplaySession::operator=(ReplaySession&&) noexcept = default;
ReplaySession::~ReplaySession() = default;

Result<std::size_t> ReplaySession::apply(ReplayStep step) {
    const auto warnings = player_->warnings().size();
    if (!player_->apply(std::move(step))) return {std::nullopt, player_->take_errors()};
    const auto& all = player_->warnings();
    return {player_->recording().steps.size() - 1,
            std::vector<Diagnostic>(all.begin() + static_cast<std::ptrdiff_t>(warnings), all.end())};
}

Result<std::size_t> ReplaySession::focus(NodeHandle target) {
    auto resolved = player_->target(target);
    if (!resolved) return {std::nullopt, player_->take_errors()};
    return apply(ReplayFocus{std::move(*resolved)});
}

Result<std::size_t> ReplaySession::invoke(NodeHandle target, std::string binding) {
    auto resolved = player_->target(target);
    if (!resolved) return {std::nullopt, player_->take_errors()};
    return apply(ReplaySemanticAction{std::move(*resolved), std::move(binding)});
}

Result<InspectionSnapshot> ReplaySession::capture(const DocumentSourceMap* sources) const {
    return player_->capture(sources);
}

bool ReplaySession::closed() const noexcept { return player_->closed(); }
const ReplayRecording& ReplaySession::recording() const noexcept { return player_->recording(); }
const ReplayOutput& ReplaySession::output() const noexcept { return player_->output(); }

std::vector<Diagnostic> compare_replay(const ReplayOutput& expected, const ReplayOutput& actual,
                                       float geometry_tolerance) {
    std::vector<Diagnostic> errors;
    detail::Checker(errors).non_negative(geometry_tolerance, "/geometry_tolerance");
    if (!errors.empty()) return errors;
    Comparison compare(errors, geometry_tolerance);
    compare.count(expected.generations.size(), actual.generations.size(), "/generations", "generations");
    for (std::size_t i = 0; i < std::min(expected.generations.size(), actual.generations.size()); ++i)
        compare.generation(expected.generations[i], actual.generations[i], "/generations/" + std::to_string(i));
    compare.count(expected.semantics.size(), actual.semantics.size(), "/semantics", "semantic observations");
    for (std::size_t i = 0; i < std::min(expected.semantics.size(), actual.semantics.size()); ++i)
        compare.semantics(expected.semantics[i], actual.semantics[i], "/semantics/" + std::to_string(i));
    compare.count(expected.actions.size(), actual.actions.size(), "/actions", "action requests");
    for (std::size_t i = 0; i < std::min(expected.actions.size(), actual.actions.size()); ++i) {
        if (expected.actions[i] != actual.actions[i])
            compare.mismatch("/actions/" + std::to_string(i), "Expected " + format(expected.actions[i]) + ", got " +
                                                                  format(actual.actions[i]) + ".");
    }
    return errors;
}

} // namespace tessera
