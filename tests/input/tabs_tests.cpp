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
const ValidationContext actions{{"before", "after", "inside", "show.a", "show.b", "show.c", "show.d"}};
struct Options {
    std::string selected = "b";
    bool list_disabled = false;
    bool c_disabled = false;
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
        std::vector<ui::TabOption> tabs{{"a", "General", "show.a"},
                                        {"b", "Display", "show.b"},
                                        {"c", "Advanced", "show.c", options.c_disabled},
                                        {"d", "Notes", "show.d"}};
        auto authored =
            take(session.lower(ui::Column({ui::Button("Before", "before", {.node = {.id = "before"}}),
                                           ui::Tabs("Sections", options.selected, tabs,
                                                    {ui::Button("Inside", "inside", {.node = {.id = "inside"}})},
                                                    {.node = {.id = "sections"}, .disabled = options.list_disabled}),
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
    check(at != tree.nodes.end(), "Missing tab semantic entry");
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
    const auto& tabs = find(f.document.root, "sections");
    check(tabs.children.size() == 2 && tabs.children[0].id == "sections-list" &&
              tabs.children[1].id == "sections-panel",
          "Tabs must lower to a tab list followed by the panel");
    const auto& list = find(tabs, "sections-list");
    check(list.properties.at("collection_role") == Property{std::string("tab_list")} &&
              list.properties.at("labelled_by") == Property{NodeReference{"sections-title"}} &&
              list.children.size() == 5 && find(list, "sections-title").properties.at("text") == text("Sections"),
          "The tab list must be labelled by its title and hold every tab");
    const auto& display = find(list, "sections-tab-b");
    check(display.properties.at("collection_role") == Property{std::string("tab")} &&
              display.properties.at("selected") == Property{true} && display.events.at("activate") == "show.b" &&
              display.children.size() == 1 &&
              find(display, "sections-label-b").properties.at("text") == text("Display") &&
              find(list, "sections-tab-a").properties.at("selected") == Property{false},
          "Tabs must carry their role, selected state, action and label");
    check(find(tabs, "sections-panel").children.size() == 1 && find(tabs, "sections-panel").children[0].id == "inside",
          "The panel must hold the supplied content");
    const auto title = f.node("sections-title");
    check(std::none_of(f.layout.boxes.begin(), f.layout.boxes.end(),
                       [&](const LayoutBox& box) { return box.node == title && box.visible; }),
          "The title must not be displayed");
    check(f.box("sections-tab-a").border_box.origin.x < f.box("sections-tab-b").border_box.origin.x &&
              f.box("sections-tab-a").border_box.origin.y == f.box("sections-tab-b").border_box.origin.y &&
              f.box("sections-tab-a").border_box.origin.y < f.box("inside").border_box.origin.y,
          "Tabs must sit in a row above the panel");
    const auto border = [&](std::string_view id) { return f.styles[f.node(id).index].border_color; };
    check(border("sections-tab-b") != border("sections-tab-a") && border("sections-tab-a") == border("sections-tab-c"),
          "Only the selected tab shows its indicator");

    f.build({.c_disabled = true});
    const auto semantics = take(build_semantic_tree(f.semantic()));
    const auto& owner = entry(semantics, "sections-list");
    check(owner.role == SemanticRole::tab_list && owner.name == "Sections" && owner.actions.empty() && !owner.selected,
          "The tab list must be named by its title and expose no action");
    const auto& b = entry(semantics, "sections-tab-b");
    const auto& a = entry(semantics, "sections-tab-a");
    check(b.role == SemanticRole::tab && b.name == "Display" && b.name_source == NameSource::relationship &&
              b.selected == true && a.selected == false && semantics.nodes[b.parent].id == "sections-list" &&
              b.actions == std::vector<SemanticAction>{{"activate", "show.b"}},
          "Tabs must be named with selected state and one ordinary action");
    check(std::none_of(semantics.nodes.begin(), semantics.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text &&
                                  (node.name == "Sections" || node.name == "Display" || node.name == "General");
                       }),
          "Tab labels and the hidden title are not separate text entries");
    check(entry(semantics, "inside").role == SemanticRole::button, "Panel content keeps its ordinary semantics");
    const auto& advanced = entry(semantics, "sections-tab-c");
    check(!advanced.enabled && advanced.actions.empty() && !advanced.focusable,
          "A disabled tab stays exposed without actions or focus");
    check(take(request_semantic_action(f.semantic(), a.node, "activate")).action == "show.a",
          "Semantic activation must request the tab's selection action");
    f.build({.list_disabled = true});
    const auto disabled = take(build_semantic_tree(f.semantic()));
    check(std::none_of(disabled.nodes.begin(), disabled.nodes.end(),
                       [](const SemanticNode& node) { return node.enabled && node.role == SemanticRole::tab; }) &&
              entry(disabled, "inside").enabled,
          "Disabling the tabs disables every tab but not the panel");
}

void codecs() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Tab roles and state must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Tab roles and selected state must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget target{SemanticRole::tab, "Notes"};
    check(take(resolve_target(inspection, target)) == f.node("sections-tab-d"), "Inspection must resolve tab targets");
    const std::vector<UiInvariant> invariants{
        {"tabs-name", SemanticNameInvariant{SemanticTarget{SemanticRole::tab_list, "Sections"}, "Sections"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept tab_list targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::tab, "Display"}},
                           InputEvent{1us, Navigate{Direction::right}}, ReplaySemanticAction{target, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain tab targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "show.c" &&
                  output.actions[1].action == "show.d",
              "Replay must share tab arrow selection and semantic activation");
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
    check(dispatch(focus, FocusNext{}).focused == f.node("sections-tab-b"), "Tab must enter at the selected tab");
    check(dispatch(focus, FocusNext{}).focused == f.node("inside"), "Tab must leave the list for the panel");
    check(dispatch(focus, FocusNext{}).focused == f.node("after"), "Tab must continue after the panel");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("inside"), "Shift+Tab must reach the panel");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("sections-tab-b"),
          "Shift+Tab must enter the list at the selected tab");
    check(dispatch(focus, FocusPrevious{}).focused == f.node("before"), "Shift+Tab must leave the list");

    (void)take(focus.focus(f.hit(), f.node("sections-tab-b")));
    auto moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("sections-tab-c") &&
              moved.actions == std::vector<ActionRequest>{{"activate", "show.c", f.node("sections-tab-c")}},
          "Right must move to the next tab and request its selection");
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("sections-tab-d") && moved.actions[0].action == "show.d", "Right must continue");
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("sections-tab-a") && moved.actions[0].action == "show.a",
          "Right wraps past the last tab");
    moved = dispatch(focus, Navigate{Direction::left});
    check(moved.focused == f.node("sections-tab-d") && moved.actions[0].action == "show.d",
          "Left wraps before the first tab");
    moved = dispatch(focus, KeyDown{Key::home});
    check(moved.focused == f.node("sections-tab-a") && moved.actions[0].action == "show.a",
          "Home selects the first tab");
    check(dispatch(focus, KeyDown{Key::home}).actions.empty(), "Staying on a tab requests nothing");
    moved = dispatch(focus, KeyDown{Key::end});
    check(moved.focused == f.node("sections-tab-d") && moved.actions[0].action == "show.d", "End selects the last tab");
    moved = dispatch(focus, Activate{});
    check(moved.focused == f.node("sections-tab-d") && moved.actions.size() == 1 && moved.actions[0].action == "show.d",
          "Activate requests the focused tab");
    moved = dispatch(focus, Navigate{Direction::down});
    check(moved.focused == f.node("inside") && moved.actions.empty(),
          "Down leaves a tab through geometric movement into the panel without selecting");
    moved = dispatch(focus, Navigate{Direction::up});
    check(moved.focused != f.node("inside") && moved.actions.empty(), "Up from the panel moves without selecting");

    f.build({.c_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("sections-tab-b")));
    moved = dispatch(focus, Navigate{Direction::right});
    check(moved.focused == f.node("sections-tab-d") && moved.actions[0].action == "show.d",
          "Arrows skip disabled tabs");

    f.build({.selected = "c", .c_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("sections-tab-a"),
          "A disabled selected tab yields the first enabled entry");
    f.build({.list_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("inside"), "A disabled list has no Tab stop");
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    const std::vector<ui::TabOption> one{{"a", "A", "show.a"}};
    check(has(lower(ui::Tabs("Sections", "a", one, {})).diagnostics, "tabs_id", "/root/children/0"),
          "Tabs require a stable ID");
    check(has(lower(ui::Tabs("", "a", one, {}, {.node = {.id = "t"}})).diagnostics, "tabs_label", "/root/children/0"),
          "Tabs require a label");
    check(has(lower(ui::Tabs("Sections", "a", {}, {}, {.node = {.id = "t"}})).diagnostics, "tabs_empty",
              "/root/children/0"),
          "Tabs require at least one tab");
    check(has(lower(ui::Tabs("Sections", "a", {{"a", "A", "show.a"}, {"a", "B", "show.b"}}, {}, {.node = {.id = "t"}}))
                  .diagnostics,
              "tabs_keys", "/root/children/0"),
          "Tab keys must be unique");
    check(has(lower(ui::Tabs("Sections", "z", one, {}, {.node = {.id = "t"}})).diagnostics, "tabs_selection",
              "/root/children/0"),
          "The selection must name an existing tab");
    check(has(lower(ui::Tabs("Sections", "a", {{"a", "", "show.a"}}, {}, {.node = {.id = "t"}})).diagnostics,
              "tabs_tab_label", "/root/children/0"),
          "Tabs require labels");

    Fixture f;
    f.build();
    // root/children/1 is the Tabs Column; its first child is the list, whose first child is the title.
    auto malformed = f.document;
    malformed.root.children[1].children[0].children[1].properties["selected"] = true;
    check(has(validate(malformed, actions), "collection_selection",
              "/root/children/1/children/0/children/2/properties/selected"),
          "At most one tab may be selected");
    malformed = f.document;
    malformed.root.children[1].children[0].children[2].events.clear();
    check(has(validate(malformed, actions), "tab_action", "/root/children/1/children/0/children/2/events/activate"),
          "Tabs require a selection action");
    malformed = f.document;
    malformed.root.children[1].children[0].events["activate"] = "show.a";
    check(has(validate(malformed, actions), "collection_action", "/root/children/1/children/0/events/activate"),
          "The tab list itself takes no action");
    malformed = f.document;
    malformed.root.children[0].properties["collection_role"] = std::string("tab");
    check(has(validate(malformed, actions), "collection_parent", "/root/children/0"),
          "Tabs belong directly under a tab_list");
    malformed = f.document;
    malformed.root.children[1].children[0].children.push_back(malformed.root.children[0]);
    check(has(validate(malformed, actions), "tab_child", "/root/children/1/children/0/children/5"),
          "Tab lists contain only tabs and Text");
    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto list = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [](const auto& node) { return node.id == "sections-list"; });
    list->selected = true;
    check(!validate(snapshot).empty(), "Semantic validation rejects selected state on the tab list");
}
} // namespace

int main() {
    try {
        composition_and_semantics();
        codecs();
        keyboard();
        rejection();
        std::cout << "Tabs composition, semantics, codecs, keyboard and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
