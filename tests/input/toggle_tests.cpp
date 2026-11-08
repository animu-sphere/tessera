#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;
namespace ui = tessera::authoring;

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
const ValidationContext actions{{"before", "feature.toggle", "snap.toggle"}};
struct Options {
    bool feature = false;
    bool snap = false;
    bool feature_disabled = false;
    bool snap_disabled = false;
};
struct Fixture {
    ui::Session session;
    Signal<bool> feature = session.state("feature", false);
    Signal<bool> snap = session.state("snap", false);
    Signal<bool> snap_disabled = session.state("snap-disabled", false);
    ui::Presentation authored;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{320, 240};
    ui::Element form(bool feature_disabled) const {
        return ui::Column(
            {ui::Button("Before", "before", {.node = {.id = "before"}}),
             ui::Checkbox("Enable feature", feature, "feature.toggle",
                          {.node = {.id = "feature"}, .disabled = feature_disabled}),
             ui::Switch("Snap to grid", snap, "snap.toggle", {.node = {.id = "snap"}, .disabled = snap_disabled})},
            {.id = "root"});
    }
    void build(Options options = {}) {
        session.update([&] {
            feature.write(options.feature);
            snap.write(options.snap);
            snap_disabled.write(options.snap_disabled);
        });
        authored = take(session.lower(form(options.feature_disabled), actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, viewport, &text, {}}));
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox& box(std::string_view id) const {
        const auto target = node(id);
        return *std::find_if(layout.boxes.begin(), layout.boxes.end(),
                             [&](const LayoutBox& box) { return box.node == target; });
    }
    const ResolvedStyle& style(std::string_view id) const { return styles[node(id).index]; }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing toggle semantic entry");
    return *at;
}
const UiNode* search(const UiNode& node, std::string_view id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children)
        if (const auto* found = search(child, id)) return found;
    return nullptr;
}
const UiNode& find(const UiNode& node, std::string_view id) {
    const auto* found = search(node, id);
    check(found != nullptr, "Missing authored node");
    return *found;
}
Property text(const char* value) { return Property{std::string(value)}; }

void composition() {
    Fixture f;
    f.build();
    const auto& checkbox = find(f.document.root, "feature");
    check(checkbox.properties.at("toggle_role") == text("checkbox") &&
              checkbox.properties.at("selected") == Property{false} &&
              checkbox.properties.at("labelled_by") == Property{NodeReference{"feature-label"}} &&
              checkbox.events.at("activate") == "feature.toggle" && checkbox.children.size() == 2 &&
              checkbox.children[0].properties.at("text") == text("[ ]") &&
              find(checkbox, "feature-label").properties.at("text") == text("Enable feature"),
          "Checkbox must lower to a labelled checkbox Box with a marker and its label");
    const auto& toggle = find(f.document.root, "snap");
    check(toggle.properties.at("toggle_role") == text("switch") &&
              toggle.properties.at("selected") == Property{false} &&
              toggle.properties.at("labelled_by") == Property{NodeReference{"snap-label"}} &&
              toggle.children.size() == 2 && toggle.children[0].id == "snap-track" &&
              toggle.children[0].children.size() == 1 && toggle.children[0].children[0].id == "snap-thumb" &&
              toggle.children[1].id == "snap-label",
          "Switch must lower to a labelled switch Box with track, thumb and label parts");

    const auto off_track = f.box("snap-track").border_box;
    const auto off_thumb = f.box("snap-thumb").border_box;
    const auto off_color = f.style("snap-track").background;
    check(off_track.size.width == 32 && off_track.size.height == 18 && off_thumb.size.width == 12 &&
              off_thumb.origin.x == off_track.origin.x + 3 && off_thumb.origin.y == off_track.origin.y + 3,
          "An off Switch places its thumb at the start of the padded track");
    check(f.box("snap-label").border_box.origin.x >= off_track.origin.x + off_track.size.width + 8,
          "The Switch label follows its track after the default gap");

    f.build({.feature = true, .snap = true});
    check(find(f.document.root, "feature").properties.at("selected") == Property{true} &&
              find(f.document.root, "feature").children[0].properties.at("text") == text("[x]") &&
              find(f.document.root, "snap").properties.at("selected") == Property{true},
          "Checked bindings must settle into the owner state and its marker in one candidate");
    const auto on_track = f.box("snap-track").border_box;
    const auto on_thumb = f.box("snap-thumb").border_box;
    check(on_track == off_track && on_thumb.origin.x == on_track.origin.x + on_track.size.width - 3 - 12 &&
              on_thumb.origin.y == off_thumb.origin.y,
          "An on Switch moves only its thumb to the end of the unchanged track");
    check(f.style("snap-track").background != off_color, "An on Switch must change its track color");
    check(validate_sources(f.authored.document, f.authored.sources).empty(), "Generated parts need valid sources");

    ui::Session literal;
    const auto fixed =
        take(literal.lower(ui::Switch("Fixed", true, "snap.toggle", {.node = {.id = "fixed"}}), actions));
    check(fixed.document.root.properties.at("selected") == Property{true} && fixed.styles[1].justify == Justify::end &&
              fixed.styles[1].background == f.style("snap-track").background,
          "A literal on value must choose the on track style");
}

void semantics() {
    Fixture f;
    f.build({.feature = true});
    const auto tree = take(build_semantic_tree(f.semantic()));
    const auto& feature = entry(tree, "feature");
    check(feature.role == SemanticRole::checkbox && feature.name == "Enable feature" &&
              feature.name_source == NameSource::relationship && feature.selected == true && feature.focusable &&
              feature.actions == std::vector<SemanticAction>{{"activate", "feature.toggle"}},
          "Checkbox must project a labelled checkbox with checked state and one ordinary action");
    const auto& snap = entry(tree, "snap");
    check(snap.role == SemanticRole::switch_ && snap.name == "Snap to grid" && snap.selected == false &&
              snap.actions == std::vector<SemanticAction>{{"activate", "snap.toggle"}},
          "Switch must project a labelled switch with its on state");
    check(std::none_of(tree.nodes.begin(), tree.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.name == "[x]" ||
                                  node.name == "Enable feature" && node.role == SemanticRole::text ||
                                  node.id == "snap-track" || node.id == "snap-thumb";
                       }),
          "Markers, labels and switch parts are presentational");
    check(take(request_semantic_action(f.semantic(), snap.node, "activate")).action == "snap.toggle",
          "Semantic activation must request the switch command");

    f.build({.feature_disabled = true, .snap_disabled = true});
    const auto disabled = take(build_semantic_tree(f.semantic()));
    for (const auto* id : {"feature", "snap"}) {
        const auto& control = entry(disabled, id);
        check(!control.enabled && !control.focusable && control.actions.empty() && control.selected == false,
              "A disabled toggle stays exposed with its state but without actions or focus");
        check(has(request_semantic_action(f.semantic(), control.node, "activate").diagnostics, "disabled_target",
                  "/target"),
              "Disabled toggles reject semantic activation");
    }
}

void input() {
    Fixture f;
    f.build();
    std::chrono::microseconds time{0};
    const auto dispatch = [&](FocusDispatcher& focus, auto data) {
        time += 1us;
        return take(focus.dispatch(f.hit(), {time, std::move(data)}));
    };
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("feature"), "Tab must reach the Checkbox");
    auto activated = dispatch(focus, Activate{});
    check(activated.actions == std::vector<ActionRequest>{{"activate", "feature.toggle", f.node("feature")}},
          "Activate must request the Checkbox command without changing its value");
    check(dispatch(focus, FocusNext{}).focused == f.node("snap"), "Tab must reach the Switch as one stop");
    const auto keyboard = dispatch(focus, Activate{}).actions;
    check(keyboard == std::vector<ActionRequest>{{"activate", "snap.toggle", f.node("snap")}},
          "Activate must request the Switch command");
    check(dispatch(focus, Navigate{Direction::right}).actions.empty(), "Arrows do not toggle a Switch");

    for (const auto* part : {"snap-thumb", "snap-track", "snap-label"}) {
        PointerDispatcher pointer;
        (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center(part)}}));
        const auto clicked = take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, f.center(part)}})).actions;
        check(clicked == keyboard, "Pressing any Switch part must request the same command once");
    }

    f.build({.snap_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("feature")));
    check(dispatch(focus, FocusNext{}).focused != f.node("snap"), "A disabled Switch has no Tab stop");
    PointerDispatcher pointer;
    (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center("snap-track")}}));
    check(take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, f.center("snap-track")}})).actions.empty(),
          "A disabled Switch ignores pointer activation");
}

void codecs() {
    Fixture f;
    f.build({.snap = true});
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Toggle roles and state must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Toggle roles and checked state must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget checkbox{SemanticRole::checkbox, "Enable feature"};
    const SemanticTarget toggle{SemanticRole::switch_, "Snap to grid"};
    check(take(resolve_target(inspection, checkbox)) == f.node("feature") &&
              take(resolve_target(inspection, toggle)) == f.node("snap"),
          "Inspection must resolve toggle role targets");
    const std::vector<UiInvariant> invariants{{"checkbox-name", SemanticNameInvariant{checkbox, "Enable feature"}},
                                              {"switch-name", SemanticNameInvariant{toggle, "Snap to grid"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept toggle role targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{checkbox}, InputEvent{1us, Activate{}},
                           ReplaySemanticAction{toggle, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain toggle targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "feature.toggle" &&
                  output.actions[1].action == "snap.toggle",
              "Replay must share keyboard and semantic toggle activation");
    }
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    check(has(lower(ui::Checkbox("Feature", true, "feature.toggle")).diagnostics, "checkbox_id", "/root/children/0"),
          "Checkbox requires a stable ID");
    check(has(lower(ui::Switch("Snap", true, "snap.toggle")).diagnostics, "switch_id", "/root/children/0"),
          "Switch requires a stable ID");
    check(!lower(ui::Switch("Snap", true, "unknown", {.node = {.id = "snap"}})),
          "Unregistered Switch command accepted");
    auto deepest = ui::Switch("Bounded", true, "snap.toggle", {.node = {.id = "bounded"}});
    for (std::size_t i = 3; i < max_document_depth; ++i) deepest = ui::Column({deepest});
    check(bool(session.lower(deepest, actions)) && !session.lower(ui::Column({deepest}), actions),
          "Generated Switch parts must count against depth bounds");

    Fixture f;
    f.build();
    const auto at = [](const char* tail) { return std::string("/root/children/1") + tail; };
    auto malformed = f.document;
    malformed.root.children[1].properties["toggle_role"] = std::string("radio");
    check(has(validate(malformed, actions), "toggle_role", at("/properties/toggle_role")),
          "Unknown toggle roles are rejected");
    malformed = f.document;
    malformed.root.children[1].properties.erase("selected");
    check(has(validate(malformed, actions), "toggle_state", at("/properties/selected")),
          "Toggles require a checked state");
    malformed = f.document;
    malformed.root.children[1].events.clear();
    check(has(validate(malformed, actions), "toggle_action", at("/events/activate")), "Toggles require a command");
    malformed = f.document;
    malformed.root.children[1].properties["separator"] = true;
    check(has(validate(malformed, actions), "toggle_role", at("/properties/toggle_role")),
          "A toggle cannot combine with another role marker");
    malformed = f.document;
    malformed.root.children[1].properties.erase("toggle_role");
    check(has(validate(malformed, actions), "collection_state", at("/properties/selected")),
          "Checked state still requires a stateful role");
    malformed = f.document;
    malformed.root.children[1].children[1].properties["toggle_role"] = std::string("checkbox");
    check(!validate(malformed, actions).empty(), "Text cannot carry a toggle role");

    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto before = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                     [](const auto& node) { return node.id == "before"; });
    before->selected = true;
    check(!validate(snapshot).empty(), "Semantic validation still rejects checked state on a button");
}
} // namespace

int main() {
    try {
        composition();
        semantics();
        input();
        codecs();
        rejection();
        std::cout << "Checkbox/Switch composition, semantics, input, codecs and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
