#include <tessera/ui/authoring.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/commands/commands.hpp>
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
std::vector<ui::TreeItem> items(bool open, bool disabled = false) {
    return {{"a",
             "Folder",
             "select",
             "toggle",
             open,
             disabled,
             {{"disabled", "Disabled", "select", {}, false, true, {}},
              {"b", "Branch", "select", "toggle", true, false, {{"c", "Child", "select", {}, false, false, {}}}}}},
            {"d", "Last", "select", {}, false, false, {}}};
}
struct Fixture {
    ui::Session session;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    PlaceholderTextShaper text;
    Size viewport{200, 80};
    std::vector<Point> offsets;
    void build(bool open = true, bool disabled = false) {
        auto authored = take(session.lower(
            ui::Column(
                {ui::TreeView("Hierarchy", "c", items(open, disabled),
                              {.node = {.id = "tree", .style = {.height = Dimension::points(60)}}}),
                 ui::ListView(
                     "Items", "two",
                     {{"one", "First", "select"}, {"off", "Unavailable", "select", true}, {"two", "Second", "select"}},
                     {.node = {.id = "list", .style = {.height = Dimension::points(20)}}})}),
            actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        offsets.assign(tree->size(), {});
        recompute();
    }
    void recompute() { layout = take(compute_layout({tree.get(), styles, viewport, &text, offsets})); }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox& box(std::string_view id) const {
        const auto target = node(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == target; });
        check(found != layout.boxes.end(), "Expected displayed collection row");
        return *found;
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing semantic collection entry");
    return *at;
}

void projection_and_codecs() {
    Fixture f;
    f.build();
    const auto semantics = take(build_semantic_tree(f.semantic()));
    check(entry(semantics, "tree").role == SemanticRole::tree && entry(semantics, "tree").name == "Hierarchy",
          "Tree must have its explicit collection label");
    const auto& folder = entry(semantics, "tree-item-a");
    const auto& branch = entry(semantics, "tree-item-b");
    const auto& child = entry(semantics, "tree-item-c");
    check(folder.name == "Folder" && folder.expanded == true && folder.selected == false && child.name == "Child" &&
              child.selected == true && !child.expanded,
          "Names must exclude disclosure/selection markers; leaves have no expansion state");
    check(semantics.nodes[child.parent].node == branch.node && semantics.nodes[branch.parent].node == folder.node,
          "Flat layout rows must project the logical tree hierarchy");
    check(f.box("tree-item-a").border_box.size.height < f.box("tree").scroll->extent.height &&
              f.box("tree-item-c").border_box.origin.x == f.box("tree-item-a").border_box.origin.x,
          "Focus/hit bounds must cover a row, not the whole descendant subtree");
    check(entry(semantics, "list").role == SemanticRole::list && entry(semantics, "list-item-two").selected == true,
          "List selection must be semantic state");
    check(entry(semantics, "tree-item-disabled").actions.empty(), "Disabled items expose no actions");
    check(take(request_semantic_action(f.semantic(), folder.node, "collapse")).action == "toggle",
          "An expanded branch must expose the ordinary collapse command");
    check(
        has(request_semantic_action(f.semantic(), folder.node, "expand").diagnostics, "unsupported_action", "/binding"),
        "The opposite expansion action must be rejected");
    auto snapshot = take(capture_semantics(f.semantic(), 7));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Collection semantic state must round trip");
    auto bad = snapshot;
    bad.nodes.front().expanded = true;
    check(!validate(bad).empty(), "Non-tree entries cannot claim expansion state");
    bad = snapshot;
    for (auto& node : bad.nodes)
        if (node.id == "tree-item-a") node.actions.push_back({"expand", "toggle"});
    check(!validate(bad).empty(), "Conflicting branch actions must be rejected");
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Collection roles, state and logical relationships must survive UI JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget target{SemanticRole::tree_item, "Child"};
    check(take(resolve_target(inspection, target)) == child.node, "Inspection must resolve item role/name");
    const std::vector<UiInvariant> invariants{{"child-name", SemanticNameInvariant{target, "Child"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant validation must accept collection roles and state");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::tree_item, "Folder"}},
                           InputEvent{1us, Navigate{Direction::left}},
                           ReplaySemanticAction{SemanticTarget{SemanticRole::list_item, "Second"}, "activate"},
                           ReplaySemanticAction{SemanticTarget{SemanticRole::tree_item, "Folder"}, "collapse"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain collection properties and targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 3 && output.actions[0].binding == "collapse" &&
                  output.actions[1].action == "select" && output.actions[2].binding == "collapse",
              "Replay must use ordinary collection navigation and semantic commands");
        check(output.semantics.front().nodes == own_semantic_records(semantics),
              "Replay must retain selected/expanded observations");
    }
    const auto stale = child.node;
    f.build(false);
    check(has(request_semantic_action(f.semantic(), stale, "activate").diagnostics, "stale_target", "/target"),
          "Replacement must reject old item handles");
    check(has(request_semantic_action(f.semantic(), f.node("tree-item-c"), "activate").diagnostics, "hidden_target",
              "/target"),
          "Collapsed descendants must not be actionable");
    check(entry(take(build_semantic_tree(f.semantic())), "tree-item-a").expanded == false,
          "Controlled collapse must update semantic state");
}

void navigation_and_recovery() {
    Fixture f;
    f.build();
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("tree-item-a")));
    auto move = [&](Direction direction) { return take(focus.dispatch(f.hit(), {1us, Navigate{direction}})); };
    check(move(Direction::down).focused == f.node("tree-item-b"), "Down must skip disabled rows");
    check(move(Direction::right).focused == f.node("tree-item-c"), "Right on an open branch must focus its child");
    check(move(Direction::left).focused == f.node("tree-item-b"), "Left on a leaf must focus its parent");
    const auto collapse = move(Direction::left);
    check(collapse.actions.size() == 1 && collapse.actions[0].binding == "collapse",
          "Left on an open branch requests collapse without changing selection");
    check(take(focus.dispatch(f.hit(), {1us, KeyDown{Key::end}})).focused == f.node("tree-item-d"),
          "End must use collection order rather than geometry or another collection");
    check(move(Direction::down).focused == f.node("tree-item-d"),
          "Down at the last item must not leave the collection");
    check(take(focus.dispatch(f.hit(), {1us, KeyDown{Key::home}})).focused == f.node("tree-item-a"),
          "Home must focus the first eligible item");
    (void)take(focus.focus(f.hit(), f.node("tree-item-c")));
    f.build(false);
    check(take(focus.refresh(f.hit())).focused == f.node("tree-item-a"),
          "Collapsing an ancestor must recover focus to the nearest visible eligible logical ancestor");
    check(move(Direction::right).actions.front().binding == "expand", "Right on a closed branch requests expansion");
    (void)take(focus.focus(f.hit(), f.node("list-item-one")));
    check(move(Direction::down).focused == f.node("list-item-two"), "List navigation must skip disabled items");
    check(move(Direction::left).focused == f.node("list-item-two"), "List Left must not escape to an adjacent control");
    check(take(focus.dispatch(f.hit(), {1us, Activate{}})).actions.front().action == "select",
          "Logical gamepad/keyboard activation selects through the same command");
    check(has(focus.dispatch(f.hit(), {0us, Navigate{Direction::up}}).diagnostics, "event_order", "/timestamp"),
          "Invalid navigation must preserve dispatcher clock/state");
    check(take(focus.refresh(f.hit())).focused == f.node("list-item-two"), "Rejected navigation must not move focus");
    f.build(true, true);
    const auto semantics = take(build_semantic_tree(f.semantic()));
    check(!entry(semantics, "tree-item-c").enabled &&
              has(focus.focus(f.hit(), f.node("tree-item-c")).diagnostics, "disabled_target", "/target"),
          "A disabled logical ancestor must disable its flattened descendants");
}

void pointer_scroll_and_resize() {
    Fixture f;
    f.build();
    PointerDispatcher pointer;
    auto click = [&](std::string_view id) {
        const auto rect = f.box(id).border_box;
        const Point point{rect.origin.x + 1, rect.origin.y + 1};
        (void)take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, point}}));
        return take(pointer.dispatch(f.hit(), {2us, PointerUp{{1}, point}})).actions;
    };
    check(click("tree-toggle-a").front().action == "toggle", "Disclosure must use a separate ordinary pointer action");
    pointer.reset();
    check(click("tree-label-a").front().action == "select", "Row labels must select rather than toggle");
    pointer.reset();
    check(click("tree-item-disabled").empty(), "A disabled child must not fall through to its logical parent's action");
    const auto updates = take(scroll_into_view(f.hit(), f.node("tree-item-d")));
    check(!updates.empty() && updates.front().container == f.node("tree"),
          "Focus reveal must reach a clipped item through ordinary scroll offsets");
    for (const auto& update : updates) f.offsets[update.container.index] = update.offset;
    f.recompute();
    check(take(scroll_into_view(f.hit(), f.node("tree-item-d"))).empty(), "Revealed row must remain stable");
    const auto before = take(build_semantic_tree(f.semantic()));
    f.viewport = {100, 80};
    f.recompute();
    check(entry(take(build_semantic_tree(f.semantic())), "tree-item-c").selected ==
              entry(before, "tree-item-c").selected,
          "Resize must not mutate controlled selection");
    check(!take(route_scroll(f.hit(), {{10, 10}, {0, -100}})).updates.empty(),
          "Wheel routing must use the same collection clip and scroll bounds");
    auto modal = f.document;
    UiNode dialog;
    dialog.id = "modal";
    dialog.properties["modal"] = true;
    modal.root.children.push_back(std::move(dialog));
    auto tree = take(UiTree::create(modal, actions));
    auto styles = f.styles;
    styles.emplace_back();
    const auto layout = take(compute_layout({tree.get(), styles, f.viewport, &f.text}));
    check(
        has(request_semantic_action({tree.get(), styles, &layout}, *tree->find("tree-item-a"), "collapse").diagnostics,
            "disabled_target", "/target"),
        "Tree expansion cannot bypass an active modal");
}

void rejected_authoring() {
    ui::Session session;
    check(!session.lower(ui::ListView("Items", {}, {}, {}), actions), "Collections require a stable ID");
    check(!session.lower(ui::ListView("Items", "missing", {}, {.node = {.id = "list"}}), actions),
          "Unknown selections must not silently clear");
    check(!session.lower(
              ui::ListView("Items", {}, {{"same", "A", "select"}, {"same", "B", "select"}}, {.node = {.id = "list"}}),
              actions),
          "Duplicate keys must be rejected");
    check(!session.lower(ui::TreeView("Tree", {}, {{"a", "A", "select", {}, true}}, {.node = {.id = "tree"}}), actions),
          "An expanded leaf must be rejected");
    auto invalid = items(true);
    invalid.front().toggle_action.clear();
    check(!session.lower(ui::TreeView("Tree", {}, invalid, {.node = {.id = "tree"}}), actions),
          "Branches require a toggle command");
    Fixture f;
    f.build();
    auto bad = f.document;
    auto& child = bad.root.children[0].children[4];
    child.properties["tree_parent"] = NodeReference{"tree-item-d"};
    check(!validate(bad, actions).empty(), "Forward/cyclic/nonancestor tree relationships must be rejected");
    bad = f.document;
    bad.root.children[0].children[1].properties["selected"] = true;
    check(!validate(bad, actions).empty(), "Multiple selected items must be rejected by the shared document boundary");
    bad = f.document;
    bad.root.children[0].events["activate"] = "select";
    check(!validate(bad, actions).empty(), "Collection containers cannot expose item actions");
    bad = f.document;
    UiNode interruption;
    interruption.kind = NodeKind::text;
    interruption.properties["text"] = std::string("Interruption");
    bad.root.children[0].children.insert(bad.root.children[0].children.begin() + 2, std::move(interruption));
    check(!validate(bad, actions).empty(), "Non-item entries cannot split a logical subtree");
    auto deep = ui::TreeItem{"leaf", "Leaf", "select"};
    for (int i = 0; i < 63; ++i)
        deep = {std::to_string(i), "Branch", "select", "toggle", true, false, {std::move(deep)}};
    check(static_cast<bool>(session.lower(ui::TreeView("Tree", {}, {deep}, {.node = {.id = "tree"}}), actions)),
          "The maximum logical depth of 64 must be accepted with flat row geometry");
    deep = {"overflow", "Branch", "select", "toggle", true, false, {std::move(deep)}};
    check(!session.lower(ui::TreeView("Tree", {}, {std::move(deep)}, {.node = {.id = "tree"}}), actions),
          "Logical depth 65 must fail before unbounded recursive lowering");
    const auto empty = take(session.lower(ui::ListView("Empty", {}, {}, {.node = {.id = "empty"}}), actions));
    check(empty.document.root.children.size() == 1, "An empty list with no selection must retain its label");
    check(!session.lower(ui::ListView("", {}, {}, {.node = {.id = "empty"}}), actions),
          "An empty collection label must be rejected");
    const auto disabled = take(session.lower(
        ui::ListView("Disabled", {}, {{"a", "A", "select"}}, {.node = {.id = "disabled"}, .disabled = true}), actions));
    auto tree = take(UiTree::create(disabled.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, disabled.styles}));
    const auto layout = take(compute_layout({tree.get(), styles, {100, 100}, &f.text}));
    check(has(request_semantic_action({tree.get(), styles, &layout}, *tree->find("disabled-item-a"), "activate")
                  .diagnostics,
              "disabled_target", "/target"),
          "Collection-wide disabled state must block its items");
}

void command_eligibility() {
    Fixture f;
    f.build();
    const auto registry = take(CommandRegistry::create({{"items.select", "Select"}, {"items.toggle", "Toggle"}},
                                                       {{"select", "items.select"}, {"toggle", "items.toggle"}}));
    const auto commands = take(CommandSnapshot::publish(registry, 1, CommandMode::production,
                                                        {{"items.select", {true}}, {"items.toggle", {false}}}));
    const auto document = take(commands.apply_eligibility(f.document, actions));
    auto tree = take(UiTree::create(document, actions));
    const auto child = *tree->find("tree-item-c");
    const Color disabled_color{1, 0, 0, 1};
    const Color focused_color{0, 1, 0, 1};
    const StyleSheet sheet{{{StyleSelector::of_id("tree-item-c", {.disabled = true}), {.background = disabled_color}},
                            {StyleSelector::of_id("tree-item-c", {.focus = true}), {.background = focused_color}}}};
    const auto styles = take(resolve_styles({tree.get(), &sheet, {.focused = child}, {}}));
    const auto layout = take(compute_layout({tree.get(), styles, f.viewport, &f.text}));
    check(styles[child.index].background == disabled_color,
          "Logical ancestor command eligibility must participate in disabled styling and suppress focus styling");
    check(has(request_semantic_action({tree.get(), styles, &layout}, child, "activate").diagnostics, "disabled_target",
              "/target"),
          "A disabled branch command must disable its logical descendants");
    check(
        has(request_semantic_action({tree.get(), styles, &layout}, *tree->find("tree-item-a"), "collapse").diagnostics,
            "disabled_target", "/target"),
        "Unavailable expansion cannot remain semantically eligible");
    const auto enabled = take(CommandSnapshot::publish(registry, 2, CommandMode::production,
                                                       {{"items.select", {true}}, {"items.toggle", {true}}}));
    check(take(enabled.apply_eligibility(f.document, actions)) == f.document,
          "Reapplying eligibility from authored state must restore the whole item");
}
} // namespace

int main() {
    try {
        projection_and_codecs();
        navigation_and_recovery();
        pointer_scroll_and_resize();
        rejected_authoring();
        command_eligibility();
        std::cout << "Collection authoring, navigation, semantics, scroll and codec checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
