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
        : recording_(std::move(recording)), text_(text), state_{.viewport = recording_.viewport,
                                                                .scale = recording_.environment.scale,
                                                                .host = recording_.environment.host},
          input_(recording_.policy.compatible_pointer ? PointerSnapshotPolicy::compatible_author_ids
                                                      : PointerSnapshotPolicy::tree_identity) {}

    // Settles the initial snapshot and then applies every recorded step.
    bool run() {
        auto steps = std::move(recording_.steps);
        recording_.steps.clear();
        auto tree = create_tree(recording_.document, "");
        if (!tree) return false;
        for (std::size_t i = 0; i < state_.host.size(); ++i) {
            if (!tree->find(state_.host[i].owner)) {
                errors_.push_back({"target_not_found",
                                   Severity::error,
                                   "/environment/host/" + std::to_string(i) + "/owner",
                                   "Declare an owner in the initial document.",
                                   {}});
                return false;
            }
        }
        if (!install_view(std::move(tree), recording_.styles, {}, "")) return false;
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
            errors_.push_back({"out_of_range",
                               Severity::error,
                               "/steps",
                               "Use at most " + std::to_string(max_replay_steps) + " replay steps.",
                               {}});
            return false;
        }
        recording_.steps.push_back(std::move(next));
        const auto& step = recording_.steps.back();
        auto previous = state_;
        committed_ = false;
        bool ok = true;
        if (const auto* event = std::get_if<InputEvent>(&step)) {
            if (std::holds_alternative<Scroll>(event->data))
                ok = scroll(*event, i, at);
            else if (is_command(*event))
                ok = command(*event, i, at);
            else
                ok = dispatch(*event, i, at);
        } else if (const auto* resize = std::get_if<ReplayResize>(&step)) {
            ok = resize_to(resize->viewport, i, at);
        } else if (const auto* reload = std::get_if<ReplayReload>(&step)) {
            cancel_pending();
            auto tree = create_tree(reload->document, at);
            ok = tree && install_view(std::move(tree), reload->styles, i, at);
        } else if (const auto* update = std::get_if<ReplayHostUpdate>(&step)) {
            ok = host_update(*update, i, at);
        } else if (const auto* focus = std::get_if<ReplayFocus>(&step)) {
            ok = focus_on(*focus, i, at);
        } else if (const auto* scale_step = std::get_if<ReplayScale>(&step)) {
            detail::Checker check(errors_);
            check.positive(scale_step->scale, at + "/scale");
            ok = errors_.empty();
            if (ok) {
                state_.scale = scale_step->scale;
                ok = settle(i, at);
            }
        } else if (const auto* tick = std::get_if<ReplayTick>(&step)) {
            if (tick->time < state_.animation_time) {
                errors_.push_back({"event_order",
                                   Severity::error,
                                   at + "/time",
                                   "Use nonnegative, non-decreasing animation-clock microseconds.",
                                   {}});
                ok = false;
            } else {
                state_.animation_time = tick->time;
                ok = settle(i, at);
            }
        } else {
            ok = invoke(std::get<ReplaySemanticAction>(step), i, at);
        }
        if (ok) return true;
        recording_.steps.pop_back();
        if (committed_) {
            closed_ = true;
        } else {
            state_ = std::move(previous);
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
            errors_.push_back({"stale_target",
                               Severity::error,
                               at + "/target",
                               "Capture the current generation and resolve the target again; this handle belongs to "
                               "another tree.",
                               {}});
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
    void cancel_pending() {
        for (auto& slot : state_.host)
            if (slot.readiness == ReplayReadiness::loading) slot.readiness = ReplayReadiness::cancelled;
    }

    bool host_update(const ReplayHostUpdate& update, std::size_t step, const std::string& at) {
        if (recording_.version != replay_host_version) {
            errors_.push_back({"unsupported_event",
                               Severity::error,
                               at,
                               "Host updates require an explicit Replay version 2 recording.",
                               {}});
            return false;
        }
        relocate(errors_, check_host_slot(update.slot, "/slot"), at);
        if (!errors_.empty()) return false;
        auto& slots = state_.host;
        auto found =
            std::find_if(slots.begin(), slots.end(), [&](const auto& slot) { return slot.id == update.slot.id; });
        const auto reject = [&](const char* code, const char* field, const char* message) {
            errors_.push_back({code, Severity::error, at + "/slot" + field, message, {}});
            return false;
        };
        if (found == slots.end()) return reject("undeclared_state", "/id", "Use a declared host slot.");
        if (found->owner != update.slot.owner)
            return reject("stale_owner", "/owner", "A slot cannot change its declared owner.");
        if (update.slot.readiness == ReplayReadiness::loading) {
            if (update.slot.request <= found->request)
                return reject("stale_request", "/request",
                              "Start with a strictly newer request number; never reuse it.");
        } else if (update.slot.readiness == ReplayReadiness::idle) {
            return reject("invalid_transition", "/readiness", "Idle is an initial declaration, not an update.");
        } else if (found->readiness != ReplayReadiness::loading || update.slot.request != found->request) {
            return reject("stale_request", "/request", "Complete only the current loading request.");
        }
        if (!current_.tree->find(found->owner))
            return reject("stale_owner", "/owner", "The declared owner has been removed; resolve a live owner.");
        if (update.slot.readiness != ReplayReadiness::ready && update.slot.value != found->value)
            return reject("invalid_transition", "/value", "Only a ready completion may publish a new host value.");
        // Validate the candidate before changing slot state or the live tree. Host updates replace the
        // entire immutable view; conservative cancellation prevents author-ID reuse from reviving requests.
        const auto view_at = at + "/view";
        auto tree = create_tree(update.view.document, view_at);
        if (!tree) return false;
        if (update.slot.readiness == ReplayReadiness::loading && !tree->find(found->owner))
            return reject("stale_owner", "/owner", "A new request requires its owner in the replacement view.");
        const auto index = static_cast<std::size_t>(found - slots.begin());
        cancel_pending();
        slots[index] = update.slot;
        return install_view(std::move(tree), update.view.styles, step, view_at);
    }

    // Tree, styles and layout travel together when a view is installed.
    struct Snapshot {
        std::unique_ptr<UiTree> tree;
        std::vector<ResolvedStyle> styles;
        LayoutResult layout;
        HitTestInput input() const { return {tree.get(), styles, &layout}; }
    };

    bool open(const std::string& at) {
        if (!closed_) return true;
        errors_.push_back({"closed_session",
                           Severity::error,
                           at,
                           "An earlier step failed after changing the session; open a new session from recording().",
                           {}});
        return false;
    }

    std::unique_ptr<UiTree> create_tree(const UiDocument& document, const std::string& at) {
        auto created = UiTree::create(document, recording_.context);
        if (!created) {
            relocate(errors_, std::move(created.diagnostics), at + "/document");
            return nullptr;
        }
        return std::move(*created.value);
    }

    // Prepare the view with zero scroll offsets before installing it. A replacement can then reveal recovered focus.
    bool install_view(std::unique_ptr<UiTree> tree, const std::vector<ResolvedStyle>& styles,
                      std::optional<std::size_t> step, const std::string& at) {
        Snapshot next{std::move(tree), styles, {}};
        auto paint = layout_and_paint(next, {}, at);
        if (!paint) return false;
        state_.offsets.assign(next.tree->size(), Point{});
        current_ = std::move(next);
        return finish_update(std::move(*paint), step, at, step.has_value() && recording_.policy.reveal_focus);
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
        state_.viewport = viewport;
        return settle(step, at, keep);
    }

    bool settle(std::optional<std::size_t> step, const std::string& at, bool reveal = false) {
        auto paint = layout_and_paint(current_, state_.offsets, at);
        return paint && finish_update(std::move(*paint), step, at, reveal);
    }

    std::optional<UiDrawList> layout_and_paint(Snapshot& view, std::span<const Point> offsets, const std::string& at) {
        auto layout = compute_layout({view.tree.get(), view.styles, state_.viewport, &text_, offsets});
        if (!layout) {
            relocate(errors_, std::move(layout.diagnostics), at);
            return std::nullopt;
        }
        auto paint = build_paint_list({view.tree.get(), view.styles, &*layout.value, &text_});
        if (!paint) {
            relocate(errors_, std::move(paint.diagnostics), at);
            return std::nullopt;
        }
        view.layout = std::move(*layout.value);
        return std::move(paint.value);
    }

    // The installed layout commits the update. Recover focus and retarget stationary pointers before recording it.
    // Revealing recovered focus can require a second layout before publishing the single generation.
    bool finish_update(UiDrawList paint, std::optional<std::size_t> step, const std::string& at, bool reveal) {
        committed_ = true;
        if (!recover_focus(at)) return false;
        if (reveal) {
            const auto scrolled = scroll_to_focus(at);
            if (!scrolled) return false;
            if (*scrolled) {
                auto revealed_paint = layout_and_paint(current_, state_.offsets, at);
                if (!revealed_paint || !recover_focus(at)) return false;
                paint = std::move(*revealed_paint);
            }
        }
        auto refreshed = input_.refresh(current_.input());
        if (!refreshed) {
            relocate(errors_, std::move(refreshed.diagnostics), at);
            return false;
        }
        ReplayGeneration generation{step, {}, std::move(paint)};
        generation.viewport = state_.viewport;
        generation.device_scale = state_.scale;
        generation.animation_time = state_.animation_time;
        generation.host = state_.host;
        generation.boxes.reserve(current_.layout.boxes.size());
        for (const auto& box : current_.layout.boxes)
            generation.boxes.push_back({box.node.index, box.parent, box.border_box, box.border, box.padding,
                                        box.visible, box.clip, box.scroll});
        output_.generations.push_back(std::move(generation));
        return observe(step, at);
    }

    bool recover_focus(const std::string& at) {
        auto recovered = focus_.refresh(current_.input());
        if (!recovered) {
            relocate(errors_, std::move(recovered.diagnostics), at);
            return false;
        }
        focused_ = recovered.value->focused;
        return true;
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
        semantics.nodes = own_semantic_records(*projected.value);
        output_.semantics.push_back(std::move(semantics));
        return true;
    }

    // Rejects time running backwards across pointer, scroll, and command steps, which share one stream.
    bool in_order(const InputEvent& event, const std::string& at) {
        if (!state_.input_time || event.timestamp >= *state_.input_time) return true;
        errors_.push_back({"event_order",
                           Severity::error,
                           at + "/timestamp",
                           "Send non-decreasing timestamps across pointer, scroll, and command steps.",
                           {}});
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
        state_.input_time = event.timestamp;
        for (const auto& update : routed.value->updates) state_.offsets[update.container.index] = update.offset;
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
        state_.input_time = event.timestamp;
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
        state_.input_time = event.timestamp;
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
        if (!recording_.policy.reveal_focus || focused_ == previous) return observe(step, at);
        const auto scrolled = scroll_to_focus(at);
        if (!scrolled) return false;
        return *scrolled ? settle(step, at) : observe(step, at);
    }

    // Returns whether focus required a scroll offset change, or nullopt on rejection.
    std::optional<bool> scroll_to_focus(const std::string& at) {
        if (!focused_) return false;
        auto revealed = scroll_into_view(current_.input(), *focused_);
        if (!revealed) {
            relocate(errors_, std::move(revealed.diagnostics), at);
            return std::nullopt;
        }
        for (const auto& update : *revealed.value) state_.offsets[update.container.index] = update.offset;
        return !revealed.value->empty();
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
    // Requested inputs that can change before a step commits.
    struct StepState {
        Size viewport;
        float scale;
        std::chrono::microseconds animation_time{};
        std::vector<ReplayHostSlot> host;
        std::vector<Point> offsets; // By node index; reset by reload.
        std::optional<std::chrono::microseconds> input_time;
    } state_;
    Snapshot current_;
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
    return "(x " + format(rect.origin.x) + ", y " + format(rect.origin.y) + ", w " + format(rect.size.width) + ", h " +
           format(rect.size.height) + ")";
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
    return action.binding + " '" + action.action + "' from " + (action.id ? "'" + *action.id + "'" : "node") + " #" +
           std::to_string(action.node) + " at step " + std::to_string(action.step) + " (generation " +
           std::to_string(action.generation) + ")";
}

std::string format(const ReplaySemanticNode& entry) {
    static constexpr const char* roles[] = {"root",      "button",     "text",         "separator", "dialog",
                                            "list",      "tree",       "list_item",    "tree_item", "menu",
                                            "menu_item", "scrollbar",  "radio_group",  "radio",     "checkbox",
                                            "switch",    "text_input", "number_input", "tab_list",  "tab",
                                            "toolbar",   "combobox",   "listbox",      "option",    "group"};
    std::string result = std::string(roles[static_cast<int>(entry.role)]) + " '" + entry.name + "' " +
                         (entry.id ? "'" + *entry.id + "' " : "") + "#" + std::to_string(entry.node);
    if (entry.value) result += ", value '" + *entry.value + "'";
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
            mismatch(path + "/node",
                     "Expected node #" + std::to_string(expected.node) + ", got #" + std::to_string(actual.node) + ".");
        if (expected.parent != actual.parent) mismatch(path + "/parent", "Box parent index differs.");
        if (!near(expected.border_box, actual.border_box))
            mismatch(path + "/border_box",
                     "Expected " + format(expected.border_box) + ", got " + format(actual.border_box) + ".");
        if (!near(expected.border, actual.border))
            mismatch(path + "/border", "Expected " + format(expected.border) + ", got " + format(actual.border) + ".");
        if (!near(expected.padding, actual.padding))
            mismatch(path + "/padding",
                     "Expected " + format(expected.padding) + ", got " + format(actual.padding) + ".");
        if (expected.visible != actual.visible)
            mismatch(path + "/visible", expected.visible ? "Expected a visible box." : "Expected a hidden box.");
        if (expected.clip.has_value() != actual.clip.has_value())
            mismatch(path + "/clip", expected.clip ? "Expected a clipped box." : "Expected an unclipped box.");
        else if (expected.clip && !near(*expected.clip, *actual.clip))
            mismatch(path + "/clip", "Expected " + format(*expected.clip) + ", got " + format(*actual.clip) + ".");
        if (expected.scroll.has_value() != actual.scroll.has_value())
            mismatch(path + "/scroll",
                     expected.scroll ? "Expected a scroll box." : "Expected a box without scrolling.");
        else if (expected.scroll && !near(*expected.scroll, *actual.scroll))
            mismatch(path + "/scroll",
                     "Expected " + format(*expected.scroll) + ", got " + format(*actual.scroll) + ".");
    }

    void generation(const ReplayGeneration& expected, const ReplayGeneration& actual, const std::string& path) {
        if (expected.step != actual.step) mismatch(path + "/step", "Generation was produced by a different step.");
        if (expected.viewport != actual.viewport) mismatch(path + "/viewport", "Logical viewport differs.");
        if (expected.device_scale != actual.device_scale) mismatch(path + "/device_scale", "Device scale differs.");
        if (expected.animation_time != actual.animation_time)
            mismatch(path + "/animation_time", "Animation-clock observation differs.");
        if (expected.host != actual.host) mismatch(path + "/host", "Declared host state/readiness differs.");
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
            mismatch(path + "/focused",
                     "Expected " + format(expected.focused) + ", got " + format(actual.focused) + ".");
        const auto nodes = path + "/nodes";
        count(expected.nodes.size(), actual.nodes.size(), nodes, "semantic entries");
        for (std::size_t i = 0; i < std::min(expected.nodes.size(), actual.nodes.size()); ++i) {
            if (expected.nodes[i] != actual.nodes[i])
                mismatch(nodes + "/" + std::to_string(i),
                         "Expected " + format(expected.nodes[i]) + ", got " + format(actual.nodes[i]) + ".");
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
    if (!detail::valid_utf8(text.id))
        check.error("invalid_utf8", "/environment/text/id", "Use valid UTF-8.");
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
    if (recording.version != replay_version && recording.version != replay_host_version)
        check.error("unsupported_version", "/version", "Use replay version 1 or 2; re-record explicitly.");
    if (recording.steps.size() > max_replay_steps)
        check.error("out_of_range", "/steps", "Use at most " + std::to_string(max_replay_steps) + " replay steps.");
    check.positive(recording.environment.scale, "/environment/scale");
    if (!locale_syntax(recording.environment.locale))
        check.error("invalid_locale", "/environment/locale",
                    "Use hyphen-separated subtags of 1-8 ASCII letters or digits, starting with 2-8 letters, "
                    "e.g. 'und' or 'ja-JP'.");
    check_text(recording.environment.text, check);
    if (recording.version == replay_version && !recording.environment.host.empty())
        check.error("unsupported_event", "/environment/host", "Host slots require Replay version 2.");
    if (recording.environment.host.size() > max_replay_host_slots)
        check.error("out_of_range", "/environment/host", "Declare at most 64 host slots.");
    std::vector<std::string> names;
    for (std::size_t i = 0; i < recording.environment.host.size(); ++i) {
        const auto& slot = recording.environment.host[i];
        const auto at = "/environment/host/" + std::to_string(i);
        relocate(errors, check_host_slot(slot, at), "");
        if (slot.request != 0 || slot.readiness != ReplayReadiness::idle)
            check.error("invalid_transition", at, "Initial slots must be idle with request zero.");
        if (std::find(names.begin(), names.end(), slot.id) != names.end())
            check.error("duplicate_state", at + "/id", "Declare each host slot once.");
        names.push_back(slot.id);
    }
    return errors;
}

std::vector<Diagnostic> check_host_slot(const ReplayHostSlot& slot, const std::string& path) {
    std::vector<Diagnostic> errors;
    Checker check(errors);
    const auto identifier = [&](const std::string& value, const char* field) {
        if (!valid_utf8(value))
            check.error("invalid_utf8", path + field, "Use valid UTF-8.");
        else if (value.empty() || value.size() > 256 ||
                 std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
            check.error("invalid_identifier", path + field, "Use 1-256 UTF-8 bytes without ASCII controls.");
    };
    identifier(slot.id, "/id");
    identifier(slot.owner, "/owner");
    check.enumeration(slot.readiness, ReplayReadiness::cancelled, path + "/readiness");
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
            compare.mismatch("/actions/" + std::to_string(i),
                             "Expected " + format(expected.actions[i]) + ", got " + format(actual.actions[i]) + ".");
    }
    return errors;
}

} // namespace tessera
