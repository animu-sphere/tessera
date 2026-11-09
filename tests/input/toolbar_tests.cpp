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
const ValidationContext actions{{"before", "after", "cut", "copy", "wrap", "paste", "pick"}};
struct Options {
    bool cut_disabled = false;
    bool copy_disabled = false;
};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{480, 240};
    void build(Options options = {}) {
        auto authored = take(session.lower(
            ui::Column(
                {ui::Button("Before", "before", {.node = {.id = "before"}}),
                 ui::Toolbar("Edit tools",
                             {ui::Button("Cut", "cut", {.node = {.id = "cut"}, .disabled = options.cut_disabled}),
                              ui::Button("Copy", "copy", {.node = {.id = "copy"}, .disabled = options.copy_disabled}),
                              // A checked toggle must not move the toolbar's entry.
                              ui::Checkbox("Wrap", true, "wrap", {.node = {.id = "wrap"}}),
                              ui::Button("Paste", "paste", {.node = {.id = "paste"}})},
                             {.id = "tools"}),
                 ui::Button("After", "after", {.node = {.id = "after"}})},
                {.id = "root", .style = {.align = Align::start}}),
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
    check(at != tree.nodes.end(), "Missing toolbar semantic entry");
    return *at;
}

void composition_and_semantics() {
    Fixture f;
    f.build();
    const auto& tools = f.document.root.children[1];
    check(tools.id == "tools" && tools.properties.at("collection_role") == Property{std::string("toolbar")} &&
              tools.properties.at("labelled_by") == Property{NodeReference{"tools-title"}} &&
              tools.children.size() == 5 && tools.children[0].id == "tools-title" && tools.children[1].id == "cut",
          "Toolbar must lower to a labelled toolbar Box with its hidden title first");
    check(f.box("cut").border_box.origin.x < f.box("copy").border_box.origin.x &&
              f.box("cut").border_box.origin.y == f.box("copy").border_box.origin.y,
          "Toolbar controls sit in a Row");
    const auto title = f.node("tools-title");
    check(std::none_of(f.layout.boxes.begin(), f.layout.boxes.end(),
                       [&](const LayoutBox& box) { return box.node == title && box.visible; }),
          "The title must not be displayed");

    const auto semantics = take(build_semantic_tree(f.semantic()));
    const auto& owner = entry(semantics, "tools");
    check(owner.role == SemanticRole::toolbar && owner.name == "Edit tools" &&
              owner.name_source == NameSource::relationship && owner.actions.empty() && !owner.focusable,
          "The toolbar must be named by its title and expose no action or focus");
    const auto& cut = entry(semantics, "cut");
    check(cut.role == SemanticRole::button && semantics.nodes[cut.parent].id == "tools" &&
              cut.actions == std::vector<SemanticAction>{{"activate", "cut"}} &&
              entry(semantics, "wrap").role == SemanticRole::checkbox,
          "Controls keep their ordinary roles under the toolbar");
    check(std::none_of(semantics.nodes.begin(), semantics.nodes.end(),
                       [](const SemanticNode& node) { return node.name == "Edit tools" && node.id != "tools"; }),
          "The hidden title is not a separate entry");
}

void codecs() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "The toolbar role must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "The toolbar role must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget target{SemanticRole::toolbar, "Edit tools"};
    check(take(resolve_target(inspection, target)) == f.node("tools"), "Inspection must resolve toolbar targets");
    const std::vector<UiInvariant> invariants{{"tools-name", SemanticNameInvariant{target, "Edit tools"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept toolbar targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{AuthorIdTarget{"cut"}}, InputEvent{1us, Navigate{Direction::right}},
                           InputEvent{2us, Activate{}}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain toolbar input");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 1 && output.actions[0].action == "copy",
              "Replay arrows must move within the toolbar without activating");
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
    check(dispatch(focus, FocusNext{}).focused == f.node("cut"), "Tab must enter at the first control");
    check(dispatch(focus, FocusNext{}).focused == f.node("after"), "Tab must leave the toolbar after one stop");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("cut"),
          "Shift+Tab must enter at the first control, not a checked toggle");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("before"), "Shift+Tab must leave the toolbar");

    (void)take(focus.focus(f.hit(), f.node("cut")));
    auto moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("copy") && moved.actions.empty(), "Right must move focus without requesting");
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("wrap") && moved.actions.empty(), "Toggles are ordinary toolbar controls");
    (void)dispatch(focus, Navigate{Direction::right});
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("cut") && moved.actions.empty(), "Right wraps past the last control");
    moved = dispatch(focus, Navigate{Direction::left});
    check(moved.focused == f.node("paste") && moved.actions.empty(), "Left wraps before the first control");
    check(dispatch(focus, KeyDown{Key::home}).focused == f.node("cut"), "Home reaches the first control");
    check(dispatch(focus, KeyDown{Key::end}).focused == f.node("paste"), "End reaches the last control");
    (void)take(focus.focus(f.hit(), f.node("wrap")));
    check(dispatch(focus, FocusNext{}).focused == f.node("after"), "Tab leaves from any control");
    (void)take(focus.focus(f.hit(), f.node("copy")));
    moved = dispatch(focus, Activate{});
    check(moved.focused == f.node("copy") &&
              moved.actions == std::vector<ActionRequest>{{"activate", "copy", f.node("copy")}},
          "Activate requests the focused control's own action");
    moved = dispatch(focus, Navigate{Direction::down});
    check(moved.focused == f.node("after") && moved.actions.empty(), "Down keeps geometric movement");

    f.build({.copy_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("cut")));
    check(dispatch(focus, Navigate{Direction::right}).focused == f.node("wrap"), "Arrows skip disabled controls");
    f.build({.cut_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("copy"), "Tab enters at the first eligible control");
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    check(has(lower(ui::Toolbar("Tools", {ui::Button("Cut", "cut")})).diagnostics, "toolbar_id", "/root/children/0"),
          "Toolbar requires a stable ID");
    check(has(lower(ui::Toolbar("", {ui::Button("Cut", "cut")}, {.id = "t"})).diagnostics, "toolbar_label",
              "/root/children/0"),
          "Toolbar requires a label");
    check(has(lower(ui::Toolbar("Tools",
                                {ui::RadioGroup("Mode", std::nullopt, {{"a", "A", "pick"}}, {.node = {.id = "mode"}})},
                                {.id = "t"}))
                  .diagnostics,
              "toolbar_child", "/root/children/0/children/1/properties/collection_role"),
          "Toolbars reject collections");
    check(has(lower(ui::Toolbar("Tools", {ui::Toolbar("Inner", {ui::Button("Cut", "cut")}, {.id = "inner"})},
                                {.id = "t"}))
                  .diagnostics,
              "toolbar_child", "/root/children/0/children/1/properties/collection_role"),
          "Toolbars reject nested toolbars");

    Fixture f;
    f.build();
    auto malformed = f.document;
    malformed.root.children[1].events["activate"] = "cut";
    check(has(validate(malformed, actions), "collection_action", "/root/children/1/events/activate"),
          "The toolbar itself takes no action");
    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto tools =
        std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [](const auto& node) { return node.id == "tools"; });
    tools->actions = {{"activate", "cut"}};
    check(!validate(snapshot).empty(), "Semantic validation rejects actions on the toolbar");
}
} // namespace

int main() {
    try {
        composition_and_semantics();
        codecs();
        keyboard();
        rejection();
        std::cout << "Toolbar composition, semantics, codecs, keyboard and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
