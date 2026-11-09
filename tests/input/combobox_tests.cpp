#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/popup.hpp>
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
const ValidationContext actions{
    {"before", "after", "purpose.toggle", "purpose.default", "purpose.render", "purpose.proxy", "purpose.guide"}};
struct Options {
    std::string selected = "render";
    bool open = false;
    bool disabled = false;
    bool proxy_disabled = false;
    bool guide_disabled = false;
    std::optional<float> width;
};
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{480, 320};
    void build(Options options = {}) {
        const std::vector<ui::ComboBoxOption> purposes{{"default", "Default", "purpose.default"},
                                                       {"render", "Render", "purpose.render"},
                                                       {"proxy", "Proxy", "purpose.proxy", options.proxy_disabled},
                                                       {"guide", "Guide", "purpose.guide", options.guide_disabled}};
        ui::Props field{.id = "purpose"};
        if (options.width) field.style.width = Dimension::points(*options.width);
        auto parts = ui::ComboBox("Purpose", options.selected, purposes, "purpose.toggle",
                                  {.node = field, .open = options.open, .disabled = options.disabled});
        auto authored =
            take(session.lower(ui::Column({ui::Button("Before", "before", {.node = {.id = "before"}}), parts.field,
                                           ui::Button("After", "after", {.node = {.id = "after"}}), parts.list},
                                          {.id = "root", .style = {.align = Align::start, .gap = 8.f}}),
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
    const LayoutBox* find_box(std::string_view id) const {
        const auto target = node(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == target; });
        return found == layout.boxes.end() ? nullptr : &*found;
    }
    const LayoutBox& box(std::string_view id) const {
        const auto* found = find_box(id);
        check(found != nullptr, "Missing layout box");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing combobox semantic entry");
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
    const auto& field = find(f.document.root, "purpose");
    check(field.properties.at("collection_role") == text("combobox") &&
              field.properties.at("expanded") == Property{false} &&
              field.properties.at("edit_value") == text("Render") &&
              field.properties.at("labelled_by") == Property{NodeReference{"purpose-title"}} &&
              field.events.at("activate") == "purpose.toggle" && field.children.size() == 3 &&
              find(field, "purpose-title").properties.at("text") == text("Purpose") &&
              find(field, "purpose-value").properties.at("text") == text("Render") &&
              find(field, "purpose-arrow").properties.at("text") == text("v"),
          "The trigger must carry its role, state, value, label and toggle action");
    const auto& list = f.document.root.children.back();
    check(list.id == "purpose-list" && list.properties.at("collection_role") == text("listbox") &&
              list.properties.at("popup_anchor") == text("purpose") &&
              list.properties.at("popup_match_width") == Property{true} &&
              list.properties.at("labelled_by") == Property{NodeReference{"purpose-list-title"}} &&
              list.children.size() == 5,
          "The list must be a root-sibling listbox anchored at the trigger");
    const auto& render = find(list, "purpose-option-render");
    check(render.properties.at("collection_role") == text("option") &&
              render.properties.at("selected") == Property{true} && render.events.at("activate") == "purpose.render" &&
              find(render, "purpose-label-render").properties.at("text") == text("Render") &&
              render.children[0].properties.at("text") == text("*") &&
              find(list, "purpose-option-default").properties.at("selected") == Property{false},
          "Options must carry their role, selection, action, label and marker");
    check(!f.find_box("purpose-list") && !f.find_box("purpose-option-render"), "A closed list must not be displayed");
    auto closed = take(build_semantic_tree(f.semantic()));
    const auto& trigger = entry(closed, "purpose");
    check(trigger.role == SemanticRole::combobox && trigger.name == "Purpose" &&
              trigger.name_source == NameSource::relationship && trigger.value == "Render" &&
              trigger.expanded == false && trigger.focusable &&
              trigger.actions == std::vector<SemanticAction>{{"activate", "purpose.toggle"}},
          "The trigger must project a named combobox with its value and collapsed state");
    check(std::none_of(closed.nodes.begin(), closed.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text || node.role == SemanticRole::listbox;
                       }),
          "Trigger Text is presentational and a closed list has no entries");

    f.build({.open = true});
    const auto& anchor = f.box("purpose").border_box;
    const auto& popup = f.box("purpose-list").border_box;
    check(f.document.root.children.back().children.size() == 5 &&
              find(f.document.root, "purpose-arrow").properties.at("text") == text("^") &&
              popup.origin.x == anchor.origin.x && popup.origin.y == anchor.origin.y + anchor.size.height + 4 &&
              popup.size.width >= anchor.size.width,
          "An open list sits below the trigger and is at least as wide");
    const auto open = take(build_semantic_tree(f.semantic()));
    const auto& listbox = entry(open, "purpose-list");
    const auto& option = entry(open, "purpose-option-render");
    check(entry(open, "purpose").expanded == true && listbox.role == SemanticRole::listbox &&
              listbox.name == "Purpose" && listbox.actions.empty() && !listbox.selected,
          "An open list projects a named listbox and an expanded trigger");
    check(option.role == SemanticRole::option && option.name == "Render" && option.selected == true &&
              entry(open, "purpose-option-guide").selected == false && open.nodes[option.parent].id == "purpose-list" &&
              option.actions == std::vector<SemanticAction>{{"activate", "purpose.render"}},
          "Options must be named, selected and activatable");
    check(std::none_of(open.nodes.begin(), open.nodes.end(),
                       [](const SemanticNode& node) { return node.role == SemanticRole::text; }),
          "Option labels and markers are not separate text entries");
    check(take(request_semantic_action(f.semantic(), option.node, "activate")).action == "purpose.render",
          "Semantic activation must request the option's selection action");

    f.build({.open = true, .proxy_disabled = true});
    const auto proxy = take(build_semantic_tree(f.semantic()));
    check(!entry(proxy, "purpose-option-proxy").enabled && entry(proxy, "purpose-option-proxy").actions.empty() &&
              entry(proxy, "purpose-option-guide").enabled,
          "A disabled option stays exposed without actions");
    f.build({.open = true, .disabled = true});
    const auto disabled = take(build_semantic_tree(f.semantic()));
    check(!entry(disabled, "purpose").enabled && !entry(disabled, "purpose").focusable &&
              std::none_of(disabled.nodes.begin(), disabled.nodes.end(),
                           [](const SemanticNode& node) { return node.role == SemanticRole::option && node.enabled; }),
          "Disabling the ComboBox disables its trigger and every option");
}

void width() {
    Fixture f;
    f.build({.open = true, .width = 300.f});
    check(f.box("purpose").border_box.size.width == 300 && f.box("purpose-list").border_box.size.width == 300,
          "A matched list takes the wider trigger's width");
    auto narrow = f.document;
    narrow.root.children.back().properties.erase("popup_match_width");
    const auto tree = take(UiTree::create(narrow, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, {}}));
    const auto layout = take(compute_layout({tree.get(), styles, f.viewport, &f.text, {}}));
    const auto list = *tree->find("purpose-list");
    const auto at =
        std::find_if(layout.boxes.begin(), layout.boxes.end(), [&](const LayoutBox& box) { return box.node == list; });
    check(at != layout.boxes.end() && at->border_box.size.width < 300,
          "Without popup_match_width the list keeps its preferred width");
    f.viewport = {200, 320};
    f.build({.open = true, .width = 300.f});
    check(f.box("purpose-list").border_box.size.width <= 200, "The matched width stays inside the viewport");
}

void codecs() {
    Fixture f;
    f.build({.open = true});
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "ComboBox roles, value and popup_match_width must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(f.node("purpose-option-render")), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "ComboBox value, expansion and option selection must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget guide{SemanticRole::option, "Guide"};
    check(take(resolve_target(inspection, guide)) == f.node("purpose-option-guide") &&
              take(resolve_target(inspection, SemanticTarget{SemanticRole::combobox, "Purpose"})) == f.node("purpose"),
          "Inspection must resolve combobox and option targets");
    const std::vector<UiInvariant> invariants{
        {"purpose-name", SemanticNameInvariant{SemanticTarget{SemanticRole::listbox, "Purpose"}, "Purpose"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept listbox targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::option, "Render"}},
                           InputEvent{1us, Navigate{Direction::down}}, InputEvent{2us, Activate{}},
                           ReplaySemanticAction{guide, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain combobox targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "purpose.proxy" &&
                  output.actions[1].action == "purpose.guide",
              "Replay must share option movement, activation and semantic activation");
    }
}

void keyboard_and_pointer() {
    Fixture f;
    f.build();
    std::chrono::microseconds time{0};
    const auto event = [&](auto data) {
        time += 1us;
        return InputEvent{time, std::move(data)};
    };
    FocusDispatcher focus;
    ComboBoxDispatcher combo{"purpose"};
    check(take(combo.refresh(f.hit())) == ComboBoxDispatchResult{}, "A closed ComboBox requests nothing");
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(take(focus.dispatch(f.hit(), event(FocusNext{}))).focused == f.node("purpose"),
          "The trigger is an ordinary Tab stop");
    auto moved = take(focus.dispatch(f.hit(), event(Activate{})));
    check(moved.actions == std::vector<ActionRequest>{{"activate", "purpose.toggle", f.node("purpose")}},
          "Activate on the trigger requests its toggle action");
    const Modifiers alt{.alt = true};
    auto result = take(combo.dispatch(f.hit(), event(KeyDown{Key::down, alt}), f.node("before")));
    check(!result.open && !result.consumed, "Alt+Down elsewhere does not open the list");
    result = take(combo.dispatch(f.hit(), event(KeyDown{Key::down, alt, true}), f.node("purpose")));
    check(!result.open, "A repeated Alt+Down does not open the list");
    result = take(combo.dispatch(f.hit(), event(KeyDown{Key::down, {}}), f.node("purpose")));
    check(!result.open && !result.consumed, "Plain Down leaves the trigger to ordinary navigation");
    result = take(combo.dispatch(f.hit(), event(KeyDown{Key::down, alt}), f.node("purpose")));
    check(result.open && result.consumed && !result.dismiss, "Alt+Down on the focused trigger opens the list");

    const auto open = [&](Options options) {
        options.open = true;
        f.build(options);
        auto opened = take(combo.refresh(f.hit()));
        if (opened.focus) (void)take(focus.focus(f.hit(), *opened.focus));
        return opened;
    };
    result = open({});
    check(result.focus == f.node("purpose-option-render") && !result.dismiss, "Opening focuses the selected option");
    check(take(combo.refresh(f.hit())).focus == std::nullopt, "Refreshing an open list requests nothing more");
    const auto navigate = [&](auto data) { return take(focus.dispatch(f.hit(), event(std::move(data)))); };
    moved = navigate(Navigate{Direction::down});
    check(moved.focused == f.node("purpose-option-proxy") && moved.actions.empty(),
          "Down moves focus without selecting");
    check(navigate(Navigate{Direction::down}).focused == f.node("purpose-option-guide"), "Down continues");
    check(navigate(Navigate{Direction::down}).focused == f.node("purpose-option-guide"), "Down stops at the end");
    check(navigate(KeyDown{Key::home}).focused == f.node("purpose-option-default"), "Home reaches the first option");
    check(navigate(Navigate{Direction::up}).focused == f.node("purpose-option-default"), "Up stops at the start");
    check(navigate(KeyDown{Key::end}).focused == f.node("purpose-option-guide"), "End reaches the last option");
    check(navigate(Navigate{Direction::left}).focused == f.node("purpose-option-guide"),
          "Left and Right do not leave the list");
    moved = navigate(Activate{});
    check(moved.actions == std::vector<ActionRequest>{{"activate", "purpose.guide", f.node("purpose-option-guide")}},
          "Activate requests the focused option's selection");

    result = open({.proxy_disabled = true});
    (void)take(focus.focus(f.hit(), f.node("purpose-option-render")));
    check(navigate(Navigate{Direction::down}).focused == f.node("purpose-option-guide"),
          "Movement skips disabled options");
    ComboBoxDispatcher fresh{"purpose"};
    f.build({.selected = "guide", .guide_disabled = true});
    (void)take(fresh.refresh(f.hit()));
    f.build({.selected = "guide", .open = true, .guide_disabled = true});
    check(take(fresh.refresh(f.hit())).focus == f.node("purpose-option-default"),
          "A disabled selection opens at the first eligible option");

    open({});
    for (const auto& dismissal :
         {InputEvent{0us, KeyDown{Key::escape}}, InputEvent{0us, Cancel{}}, InputEvent{0us, KeyDown{Key::up, alt}}}) {
        auto copy = dismissal;
        time += 1us;
        copy.timestamp = time;
        result = take(combo.dispatch(f.hit(), copy, f.node("purpose-option-render")));
        check(result.dismiss && result.consumed && result.focus == f.node("purpose"),
              "Escape, Cancel and Alt+Up dismiss and restore trigger focus");
    }
    result = take(combo.dispatch(f.hit(), event(FocusNext{}), f.node("purpose-option-render")));
    check(result.dismiss && !result.consumed && result.focus == f.node("purpose"),
          "Sequential focus dismisses without consuming the command");
    result = take(combo.dispatch(f.hit(), event(KeyDown{Key::tab, {.shift = true}}), f.node("purpose-option-render")));
    check(result.dismiss && !result.consumed, "Raw Shift+Tab dismisses without consuming");
    result = take(combo.dispatch(f.hit(), event(PointerMove{{1}, f.center("purpose-option-proxy")})));
    check(!result.dismiss && result.focus == f.node("purpose-option-proxy"), "Hover focuses an eligible option");
    result = take(combo.dispatch(f.hit(), event(PointerDown{{1}, f.center("purpose-option-proxy")})));
    check(!result.dismiss && !result.consumed, "A press inside the list is ordinary input");
    result = take(combo.dispatch(f.hit(), event(PointerDown{{1}, f.center("before")})));
    check(result.dismiss && result.consumed && result.focus == f.node("purpose"),
          "An outside press dismisses without clicking through");
    result = take(combo.dispatch(f.hit(), event(PointerDown{{1}, f.center("purpose")})));
    check(result.dismiss && result.consumed, "Pressing the trigger while open closes the list");

    f.build();
    result = take(combo.refresh(f.hit()));
    check(result.focus == f.node("purpose") && !result.dismiss, "Closing returns focus to the trigger");
    f.build({.open = true});
    (void)take(combo.refresh(f.hit()));
    f.build({.open = true, .disabled = true});
    result = take(combo.refresh(f.hit()));
    check(result.dismiss, "A list whose ComboBox becomes ineligible asks the host to close it");
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::ComboBoxParts parts) {
        return session.lower(ui::Column({std::move(parts.field), std::move(parts.list)}), actions);
    };
    const std::vector<ui::ComboBoxOption> one{{"a", "A", "purpose.default"}};
    check(
        has(lower(ui::ComboBox("Purpose", "a", one, "purpose.toggle")).diagnostics, "combobox_id", "/root/children/0"),
        "ComboBox requires a stable ID");
    check(has(lower(ui::ComboBox("", "a", one, "purpose.toggle", {.node = {.id = "c"}})).diagnostics, "combobox_label",
              "/root/children/0"),
          "ComboBox requires a label");
    check(has(lower(ui::ComboBox("Purpose", "a", {}, "purpose.toggle", {.node = {.id = "c"}})).diagnostics,
              "combobox_empty", "/root/children/0"),
          "ComboBox requires an option");
    check(has(lower(ui::ComboBox("Purpose", "a", {{"a", "A", "purpose.default"}, {"a", "B", "purpose.render"}},
                                 "purpose.toggle", {.node = {.id = "c"}}))
                  .diagnostics,
              "combobox_keys", "/root/children/0"),
          "Option keys must be unique");
    check(has(lower(ui::ComboBox("Purpose", "z", one, "purpose.toggle", {.node = {.id = "c"}})).diagnostics,
              "combobox_selection", "/root/children/0"),
          "The selection must name an existing option");
    check(
        has(lower(ui::ComboBox("Purpose", "a", {{"a", "", "purpose.default"}}, "purpose.toggle", {.node = {.id = "c"}}))
                .diagnostics,
            "combobox_option_label", "/root/children/0"),
        "Options require labels");
    check(has(lower(ui::ComboBox("Purpose", "a", one, "purpose.toggle", {.node = {.id = "c"}, .list = {.id = "x"}}))
                  .diagnostics,
              "combobox_list_id", "/root/children/1"),
          "The list ID is generated");

    Fixture f;
    f.build({.open = true});
    // root/children/1 is the trigger; root/children/3 is the list, whose first child is its title.
    auto malformed = f.document;
    malformed.root.children[1].properties.erase("expanded");
    check(has(validate(malformed, actions), "combobox_state", "/root/children/1/properties/expanded"),
          "A combobox declares its expansion state");
    malformed = f.document;
    malformed.root.children[1].properties.erase("edit_value");
    check(has(validate(malformed, actions), "edit_value", "/root/children/1/properties/edit_value"),
          "A combobox declares its value");
    malformed = f.document;
    malformed.root.children[1].events.clear();
    check(has(validate(malformed, actions), "combobox_action", "/root/children/1/events/activate"),
          "A combobox requires a toggle action");
    malformed = f.document;
    malformed.root.children[1].children.push_back(malformed.root.children[0]);
    check(has(validate(malformed, actions), "combobox_child", "/root/children/1/children/3"),
          "A combobox contains only Text");
    malformed = f.document;
    malformed.root.children[3].children[1].events.clear();
    check(has(validate(malformed, actions), "option_action", "/root/children/3/children/1/events/activate"),
          "Options require a selection action");
    malformed = f.document;
    malformed.root.children[3].children[1].properties["selected"] = true;
    check(has(validate(malformed, actions), "collection_selection", "/root/children/3/children/2/properties/selected"),
          "At most one option may be selected");
    malformed = f.document;
    malformed.root.children[3].children.push_back(malformed.root.children[0]);
    check(has(validate(malformed, actions), "listbox_child", "/root/children/3/children/5"),
          "Listboxes contain only options and Text");
    malformed = f.document;
    malformed.root.children[0].properties["collection_role"] = std::string("option");
    check(has(validate(malformed, actions), "collection_parent", "/root/children/0"),
          "Options belong directly under a listbox");
    malformed = f.document;
    malformed.root.children[3].properties.erase("popup_anchor");
    check(has(validate(malformed, actions), "listbox_popup", "/root/children/3"),
          "A listbox is an anchored root-sibling popup");
    malformed = f.document;
    malformed.root.children[0].properties["expanded"] = true;
    check(has(validate(malformed, actions), "collection_state", "/root/children/0/properties/expanded"),
          "Only tree items and comboboxes have expansion state");

    auto snapshot = take(capture_semantics(f.semantic(), 1));
    const auto at = [&](std::string_view id) {
        return std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                            [&](const auto& node) { return node.id == id; });
    };
    auto copy = snapshot;
    snapshot = copy;
    at("purpose")->value.reset();
    check(!validate(snapshot).empty(), "Semantic validation requires a combobox value");
    snapshot = copy;
    at("purpose")->expanded.reset();
    check(!validate(snapshot).empty(), "Semantic validation requires combobox expansion state");
    snapshot = copy;
    at("purpose-list")->selected = true;
    check(!validate(snapshot).empty(), "Semantic validation rejects selected state on the listbox");

    Fixture g;
    g.build();
    check(!ComboBoxDispatcher("after").refresh(g.hit()) && !ComboBoxDispatcher("").refresh(g.hit()),
          "The dispatcher rejects non-combobox and empty IDs");
}
} // namespace

int main() {
    try {
        composition_and_semantics();
        width();
        codecs();
        keyboard_and_pointer();
        rejection();
        std::cout << "ComboBox composition, width, semantics, codecs, input and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
