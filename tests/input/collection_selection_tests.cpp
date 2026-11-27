#include <tessera/ui/authoring.hpp>
#include <tessera/input/collection_selection.hpp>
#include <tessera/input/focus.hpp>
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
const ValidationContext actions{{"select", "toggle"}};
using Keys = std::vector<std::string>;

// a (branch) > [disabled, b (branch) > [c]], d.
std::vector<ui::TreeItem> items(bool open) {
    return {{"a",
             "Folder",
             "select",
             "toggle",
             open,
             false,
             {{"disabled", "Disabled", "select", {}, false, true, {}},
              {"b", "Branch", "select", "toggle", true, false, {{"c", "Child", "select", {}, false, false, {}}}}}},
            {"d", "Last", "select", {}, false, false, {}}};
}

CollectionSelection select(const Keys& order, const CollectionSelection& current, std::string_view key,
                           SelectionGesture gesture) {
    const auto next = select_items(order, current, key, gesture);
    check(next.has_value(), "A key in the order must produce a selection");
    return *next;
}

void gestures() {
    check(selection_gesture({}) == SelectionGesture::replace &&
              selection_gesture({.control = true}) == SelectionGesture::toggle &&
              selection_gesture({.shift = true}) == SelectionGesture::extend &&
              selection_gesture({.shift = true, .control = true}) == SelectionGesture::add_range &&
              selection_gesture({.alt = true}) == SelectionGesture::replace &&
              selection_gesture({.control = true, .super = true}) == SelectionGesture::toggle,
          "Modifiers must map to the conventional desktop gestures");

    const Keys order{"a", "b", "c", "d", "e"};
    auto selection = select(order, {}, "b", SelectionGesture::replace);
    check(selection == CollectionSelection{{"b"}, "b"}, "Replace selects only the key and anchors it");
    selection = select(order, selection, "d", SelectionGesture::extend);
    check(selection == CollectionSelection{{"b", "c", "d"}, "b"}, "Extend selects the anchored range");
    selection = select(order, selection, "a", SelectionGesture::extend);
    check(selection == CollectionSelection{{"a", "b"}, "b"}, "Extend replaces the previous range, in either direction");
    selection = select(order, selection, "d", SelectionGesture::toggle);
    check(selection == CollectionSelection{{"a", "b", "d"}, "d"}, "Toggle adds the key and moves the anchor");
    selection = select(order, selection, "e", SelectionGesture::add_range);
    check(selection == CollectionSelection{{"a", "b", "d", "e"}, "d"}, "Add range keeps the rest of the selection");
    selection = select(order, selection, "a", SelectionGesture::toggle);
    check(selection == CollectionSelection{{"b", "d", "e"}, "a"}, "Toggle removes a selected key");
    selection = select(order, selection, "b", SelectionGesture::toggle);
    selection = select(order, selection, "d", SelectionGesture::toggle);
    selection = select(order, selection, "e", SelectionGesture::toggle);
    check(selection == CollectionSelection{{}, "e"}, "Toggling every key off leaves an empty selection");
    check(!select_items(order, selection, "missing", SelectionGesture::replace),
          "A key outside the order must not change the selection");
    check(select(order, {}, "c", SelectionGesture::extend) == CollectionSelection{{"c"}, "c"},
          "Without an anchor, the key starts the range");
    check(select(order, {{"a"}, "gone"}, "c", SelectionGesture::add_range) == CollectionSelection{{"a", "c"}, "c"},
          "An anchor outside the order is replaced by the key");

    // Keys outside the order, such as collapsed descendants, survive toggles but not replacement.
    const CollectionSelection hidden{{"hidden", "c"}, "c"};
    check(select(order, hidden, "a", SelectionGesture::toggle) == CollectionSelection{{"a", "c", "hidden"}, "a"},
          "Toggle keeps hidden keys after the ordered ones");
    check(select(order, hidden, "e", SelectionGesture::add_range) ==
              CollectionSelection{{"c", "d", "e", "hidden"}, "c"},
          "Add range keeps hidden keys");
    check(select(order, hidden, "d", SelectionGesture::extend) == CollectionSelection{{"c", "d"}, "c"},
          "Extend drops hidden keys");
    check(select(order, hidden, "d", SelectionGesture::replace) == CollectionSelection{{"d"}, "d"},
          "Replace drops hidden keys");
    check(select_all(order, hidden) == CollectionSelection{{"a", "b", "c", "d", "e", "hidden"}, "c"},
          "Select all adds every row and keeps the anchor");
    check(select_all({}, {}) == CollectionSelection{}, "Select all over no rows selects nothing");

    check(ui::selectable_keys(items(true)) == Keys{"a", "b", "c", "d"},
          "Selectable tree rows skip disabled items and include expanded descendants");
    check(ui::selectable_keys(items(false)) == Keys{"a", "d"}, "Collapsed descendants are not selectable rows");
    auto blocked = items(true);
    blocked.front().children[1].disabled = true;
    check(ui::selectable_keys(blocked) == Keys{"a", "d"}, "A disabled branch removes its descendants");
    check(ui::selectable_keys(std::vector<ui::ListItem>{{"x", "X", "select"}, {"y", "Y", "select", true}}) == Keys{"x"},
          "Selectable list rows skip disabled items");
}

struct View {
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    PlaceholderTextShaper text;
    explicit View(ui::Element element) {
        ui::Session session;
        auto authored = take(session.lower(ui::Column({std::move(element)}), actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, {200, 200}, &text}));
    }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticNode entry(std::string_view id) const {
        const auto semantics = take(build_semantic_tree(semantic()));
        const auto found = std::find_if(semantics.nodes.begin(), semantics.nodes.end(),
                                        [&](const SemanticNode& node) { return node.id == id; });
        check(found != semantics.nodes.end(), "Expected a semantic entry for " + std::string(id));
        return *found;
    }
    std::string marker(std::string_view id) const {
        const auto& row = *tree->get(*tree->find(id));
        const auto& text = row.children[row.children.size() - 2].properties.at("text");
        return std::get<std::string>(text);
    }
};

void multiselectable_projection() {
    View view(
        ui::TreeView("Hierarchy", ui::MultipleSelection{{"d", "a", "c"}}, items(false), {.node = {.id = "tree"}}));
    const auto tree = view.entry("tree");
    check(tree.role == SemanticRole::tree && tree.multiselectable == true, "The tree must be multiselectable");
    const auto selected = [&](std::string_view id) {
        return view.tree->get(*view.tree->find(id))->properties.at("selected") == Property{true};
    };
    check(view.entry("tree-item-a").selected == true && view.entry("tree-item-d").selected == true &&
              !selected("tree-item-b") && !selected("tree-item-disabled"),
          "Every selected key, and only those, must mark its row");
    check(view.marker("tree-item-a") == "*" && view.marker("tree-item-d") == "*" && view.marker("tree-item-b") == " ",
          "Selected rows show the selection marker");
    check(selected("tree-item-c"), "A collapsed selected descendant keeps its selected state");

    // Navigation and activation are unchanged: arrows move focus and activation requests the item's action.
    FocusDispatcher focus;
    (void)take(focus.focus(view.hit(), *view.tree->find("tree-item-a")));
    auto moved = take(focus.dispatch(view.hit(), {0us, Navigate{Direction::down}}));
    check(moved.focused == view.tree->find("tree-item-d") && moved.actions.empty(),
          "Arrows move focus without requesting selection");
    const auto activated = take(focus.dispatch(view.hit(), {0us, Activate{}}));
    check(activated.actions.size() == 1 && activated.actions[0].action == "select",
          "Activation requests the item's own action");

    check(take(load_document(take(save_document(view.document, actions)), actions)) == view.document,
          "multiselectable and multiple selected items must survive UI JSON");
    const auto snapshot = take(capture_semantics(view.semantic(), 3));
    const auto saved = take(save_semantics(snapshot));
    check(saved.find("\"multiselectable\":true") != std::string::npos, "Semantic JSON must carry multiselectable");
    check(take(load_semantics(saved)) == snapshot, "multiselectable must round trip through Semantic JSON");
    auto bad = snapshot;
    const auto item = std::find_if(bad.nodes.begin(), bad.nodes.end(),
                                   [](const SemanticRecord& node) { return node.id == "tree-item-a"; });
    item->multiselectable = true;
    check(
        has(validate(bad), "invalid_state", "/nodes/" + std::to_string(item - bad.nodes.begin()) + "/multiselectable"),
        "Only lists and trees may be multiselectable in Semantic JSON");

    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = {200, 200};
        recording.document = view.document;
        recording.styles = view.styles;
        recording.steps = {ReplaySemanticAction{SemanticTarget{SemanticRole::tree_item, "Last"}, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay must retain the multiselectable document");
        const auto output = take(play_replay(restored, view.text));
        const auto& nodes = output.semantics.front().nodes;
        const auto owner =
            std::find_if(nodes.begin(), nodes.end(), [](const SemanticRecord& node) { return node.id == "tree"; });
        check(output.actions.size() == 1 && owner != nodes.end() && owner->multiselectable == true,
              "Replay observations must retain multiselectable");
    }

    View empty(ui::ListView("Items", ui::MultipleSelection{{}}, {{"x", "X", "select"}}, {.node = {.id = "list"}}));
    check(empty.entry("list").multiselectable == true && empty.entry("list-item-x").selected == false,
          "An empty multiple selection is still multiselectable");
    View single(ui::ListView("Items", "x", {{"x", "X", "select"}}, {.node = {.id = "list"}}));
    check(!single.entry("list").multiselectable, "Single selection reports no multiselectable state");

    ui::VirtualCollectionProps props{.node = {.id = "virtual"}, .window = {.item_extent = 20, .viewport = 60}};
    View virtual_view(ui::VirtualTreeView("Scene", ui::MultipleSelection{{"a", "d"}}, items(true), props));
    check(virtual_view.entry("virtual").multiselectable == true &&
              virtual_view.entry("virtual-item-a").selected == true &&
              virtual_view.entry("virtual-item-d").selected == true &&
              virtual_view.entry("virtual-item-b").selected == false,
          "Virtual trees must project a multiple selection");
    View virtual_list(
        ui::VirtualListView("Rows", ui::MultipleSelection{{"y"}}, {{"x", "X", "select"}, {"y", "Y", "select"}}, props));
    check(virtual_list.entry("virtual").multiselectable == true &&
              virtual_list.entry("virtual-item-y").selected == true,
          "Virtual lists must project a multiple selection");
}

void rejection() {
    ui::Session session;
    const auto code = [&](ui::Element element) {
        const auto result = session.lower(std::move(element), actions);
        return !result && std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                      [](const Diagnostic& d) { return d.code == "collection_selection"; });
    };
    check(code(ui::TreeView("Tree", ui::MultipleSelection{{"a", "missing"}}, items(true), {.node = {.id = "tree"}})),
          "Unknown selected keys must be rejected");
    check(code(ui::TreeView("Tree", ui::MultipleSelection{{"a", "a"}}, items(true), {.node = {.id = "tree"}})),
          "Repeated selected keys must be rejected");
    check(code(ui::VirtualListView("Rows", ui::MultipleSelection{{"z"}}, {{"x", "X", "select"}},
                                   {.node = {.id = "rows"}, .window = {.item_extent = 20, .viewport = 40}})),
          "Virtual collections reject unknown selected keys");

    View view(ui::TreeView("Tree", ui::MultipleSelection{{"a", "d"}}, items(true), {.node = {.id = "tree"}}));
    auto bad = view.document;
    bad.root.children[0].properties.erase("multiselectable");
    check(has(validate(bad, actions), "collection_selection", "/root/children/0/children/5/properties/selected"),
          "Without multiselectable, a second selected item must be rejected");
    bad = view.document;
    bad.root.children[0].properties["multiselectable"] = false;
    check(!validate(bad, actions).empty(), "multiselectable false keeps single selection");
    bad = view.document;
    bad.root.children[0].children[1].properties["multiselectable"] = true;
    check(has(validate(bad, actions), "collection_state", "/root/children/0/children/1/properties/multiselectable"),
          "Only lists and trees may be multiselectable");
}

} // namespace

int main() {
    try {
        gestures();
        multiselectable_projection();
        rejection();
        std::cout << "Collection multiple selection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
