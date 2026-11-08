#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
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
const ValidationContext actions{{"before", "after", "pick.a", "pick.b", "pick.c", "pick.d"}};
struct Options {
    std::optional<std::string> selected = "b";
    bool group_disabled = false;
    bool c_disabled = false;
    bool row = false;
};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{320, 240};
    void build(Options options = {}) {
        std::vector<ui::RadioOption> choices{{"a", "Inherited", "pick.a"},
                                             {"b", "Visible", "pick.b"},
                                             {"c", "Hidden", "pick.c", options.c_disabled},
                                             {"d", "Guide", "pick.d"}};
        ui::CollectionProps props{.node = {.id = "mode"}, .disabled = options.group_disabled};
        if (options.row) props.node.style.direction = FlexDirection::row;
        auto authored = take(session.lower(ui::Column({ui::Button("Before", "before", {.node = {.id = "before"}}),
                                                       ui::RadioGroup("Mode", options.selected, choices, props),
                                                       ui::Button("After", "after", {.node = {.id = "after"}})},
                                                      {.id = "root"}),
                                           actions));
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
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing radio semantic entry");
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

void composition_and_semantics() {
    Fixture f;
    f.build();
    const auto& group = find(f.document.root, "mode");
    check(group.properties.at("collection_role") == Property{std::string("radio_group")} &&
              group.properties.at("labelled_by") == Property{NodeReference{"mode-title"}} &&
              group.children.size() == 5 && find(group, "mode-title").properties.at("text") == text("Mode"),
          "RadioGroup must lower to a labelled radio_group Box with its title first");
    const auto& visible = find(group, "mode-option-b");
    check(visible.properties.at("collection_role") == Property{std::string("radio")} &&
              visible.properties.at("selected") == Property{true} && visible.events.at("activate") == "pick.b" &&
              visible.children.size() == 2 && visible.children[0].properties.at("text") == text("(o)") &&
              find(group, "mode-option-a").children[0].properties.at("text") == text("( )"),
          "Options must carry radio role, checked state, action and an explicit marker");
    check(f.box("mode-option-a").border_box.origin.y < f.box("mode-option-b").border_box.origin.y,
          "RadioGroup defaults to a Column");
    f.build({.row = true});
    check(f.box("mode-option-a").border_box.origin.x < f.box("mode-option-b").border_box.origin.x &&
              f.box("mode-option-a").border_box.origin.y == f.box("mode-option-b").border_box.origin.y,
          "An authored direction must override the Column default");

    f.build({.c_disabled = true});
    const auto semantics = take(build_semantic_tree(f.semantic()));
    const auto& owner = entry(semantics, "mode");
    check(owner.role == SemanticRole::radio_group && owner.name == "Mode" && owner.actions.empty() && !owner.selected,
          "The group must be named by its title and expose no action of its own");
    const auto& b = entry(semantics, "mode-option-b");
    const auto& a = entry(semantics, "mode-option-a");
    check(b.role == SemanticRole::radio && b.name == "Visible" && b.name_source == NameSource::relationship &&
              b.selected == true && a.selected == false && semantics.nodes[b.parent].id == "mode" &&
              b.actions == std::vector<SemanticAction>{{"activate", "pick.b"}},
          "Options must be named radios with checked state and one ordinary action");
    check(std::none_of(semantics.nodes.begin(), semantics.nodes.end(),
                       [](const SemanticNode& node) { return node.name == "(o)" || node.name == "( )"; }),
          "Markers are presentational, not separate text entries");
    const auto& hidden = entry(semantics, "mode-option-c");
    check(!hidden.enabled && hidden.actions.empty() && !hidden.focusable,
          "A disabled option stays exposed without actions or focus");
    check(take(request_semantic_action(f.semantic(), a.node, "activate")).action == "pick.a",
          "Semantic activation must request the option's selection action");
    check(has(request_semantic_action(f.semantic(), hidden.node, "activate").diagnostics, "disabled_target", "/target"),
          "Disabled options reject semantic activation");
    f.build({.group_disabled = true});
    const auto disabled = take(build_semantic_tree(f.semantic()));
    check(std::none_of(disabled.nodes.begin(), disabled.nodes.end(),
                       [](const SemanticNode& node) { return node.enabled && node.role == SemanticRole::radio; }),
          "Disabling the group disables every option");
    f.build({.selected = std::nullopt});
    const auto none = take(build_semantic_tree(f.semantic()));
    check(std::all_of(
              none.nodes.begin(), none.nodes.end(),
              [](const SemanticNode& node) { return node.role != SemanticRole::radio || node.selected == false; }),
          "nullopt checks no option");
}

void codecs() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Radio roles and state must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Radio roles and checked state must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget target{SemanticRole::radio, "Hidden"};
    check(take(resolve_target(inspection, target)) == f.node("mode-option-c"),
          "Inspection must resolve radio role targets");
    const std::vector<UiInvariant> invariants{
        {"radio-name", SemanticNameInvariant{SemanticTarget{SemanticRole::radio_group, "Mode"}, "Mode"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept radio role targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::radio, "Visible"}},
                           InputEvent{1us, Navigate{Direction::down}}, ReplaySemanticAction{target, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain radio targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "pick.c" &&
                  output.actions[1].action == "pick.c",
              "Replay must share radio arrow selection and semantic activation");
    }
}

void keyboard() {
    Fixture f;
    f.build();
    std::chrono::microseconds time{0};
    const auto dispatch = [&](FocusDispatcher& focus, auto data) {
        time += 1us;
        return take(focus.dispatch(f.hit(), {time, std::move(data)}));
    };
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("mode-option-b"), "Tab must enter at the checked option");
    check(dispatch(focus, FocusNext{}).focused == f.node("after"), "Tab must leave the group after one stop");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("mode-option-b"),
          "Shift+Tab must enter at the checked option");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("before"), "Shift+Tab must leave the group");

    (void)take(focus.focus(f.hit(), f.node("mode-option-b")));
    auto moved = dispatch(focus, Navigate{Direction::down});
    check(moved.focused == f.node("mode-option-c") &&
              moved.actions == std::vector<ActionRequest>{{"activate", "pick.c", f.node("mode-option-c")}},
          "Down must move to the next option and request its selection");
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("mode-option-d") && moved.actions.size() == 1 && moved.actions[0].action == "pick.d",
          "Right must behave like Down");
    moved = dispatch(focus, Navigate{Direction::down});
    check(moved.focused == f.node("mode-option-a") && moved.actions[0].action == "pick.a",
          "Arrows wrap past the last option");
    moved = dispatch(focus, Navigate{Direction::left});
    check(moved.focused == f.node("mode-option-d") && moved.actions[0].action == "pick.d",
          "Left/Up wrap before the first option");
    moved = dispatch(focus, KeyDown{Key::home});
    check(moved.focused == f.node("mode-option-a") && moved.actions[0].action == "pick.a",
          "Home selects the first option");
    check(dispatch(focus, KeyDown{Key::home}).actions.empty(), "Staying on an option requests nothing");
    moved = dispatch(focus, KeyDown{Key::end});
    check(moved.focused == f.node("mode-option-d") && moved.actions[0].action == "pick.d",
          "End selects the last option");
    moved = dispatch(focus, Activate{});
    check(moved.focused == f.node("mode-option-d") && moved.actions.size() == 1 && moved.actions[0].action == "pick.d",
          "Activate requests the focused option");
    // A non-checked focused option still leaves the group with either Tab direction.
    check(dispatch(focus, FocusPrevious{}).focused == f.node("before"),
          "Shift+Tab from an unchecked option must leave the group");

    f.build({.c_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("mode-option-b")));
    moved = dispatch(focus, Navigate{Direction::down});
    check(moved.focused == f.node("mode-option-d") && moved.actions[0].action == "pick.d",
          "Arrows skip disabled options");

    f.build({.selected = std::nullopt});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("mode-option-a"),
          "Without a checked option, Tab enters at the first one");
    f.build({.selected = "c", .c_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("mode-option-a"),
          "A disabled checked option yields the first enabled entry");
    f.build({.group_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("after"), "A disabled group has no Tab stop");
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    check(has(lower(ui::RadioGroup("Mode", "a", {{"a", "A", "pick.a"}})).diagnostics, "radio_group_id",
              "/root/children/0"),
          "RadioGroup requires a stable ID");
    check(has(lower(ui::RadioGroup("", "a", {{"a", "A", "pick.a"}}, {.node = {.id = "g"}})).diagnostics,
              "radio_group_label", "/root/children/0"),
          "RadioGroup requires a label");
    check(has(lower(ui::RadioGroup("Mode", "a", {{"a", "A", "pick.a"}, {"a", "B", "pick.b"}}, {.node = {.id = "g"}}))
                  .diagnostics,
              "radio_group_keys", "/root/children/0"),
          "Option keys must be unique");
    check(has(lower(ui::RadioGroup("Mode", "z", {{"a", "A", "pick.a"}}, {.node = {.id = "g"}})).diagnostics,
              "radio_group_selection", "/root/children/0"),
          "The selection must name an existing option");
    check(has(lower(ui::RadioGroup("Mode", "a", {{"a", "", "pick.a"}}, {.node = {.id = "g"}})).diagnostics,
              "radio_group_option_label", "/root/children/0"),
          "Options require labels");

    Fixture f;
    f.build();
    auto malformed = f.document;
    malformed.root.children[1].children[1].properties["selected"] = true;
    check(has(validate(malformed, actions), "collection_selection", "/root/children/1/children/2/properties/selected"),
          "At most one option may be checked");
    malformed = f.document;
    malformed.root.children[1].children[1].events.clear();
    check(has(validate(malformed, actions), "radio_action", "/root/children/1/children/1/events/activate"),
          "Radios require a selection action");
    malformed = f.document;
    malformed.root.children[1].events["activate"] = "pick.a";
    check(has(validate(malformed, actions), "collection_action", "/root/children/1/events/activate"),
          "The group itself takes no action");
    malformed = f.document;
    malformed.root.children[0].properties["collection_role"] = std::string("radio");
    check(has(validate(malformed, actions), "collection_parent", "/root/children/0"),
          "Radios belong directly under a radio_group");
    malformed = f.document;
    malformed.root.children[0].properties["collection_role"] = std::string("checkbox");
    check(has(validate(malformed, actions), "collection_role", "/root/children/0/properties/collection_role"),
          "Unknown collection roles remain rejected");
    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto group =
        std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [](const auto& node) { return node.id == "mode"; });
    group->selected = true;
    check(!validate(snapshot).empty(), "Semantic validation rejects checked state on the group");
}
} // namespace

int main() {
    try {
        composition_and_semantics();
        codecs();
        keyboard();
        rejection();
        std::cout << "RadioGroup composition, semantics, codecs, keyboard and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
