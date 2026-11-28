#include <tessera/inspection/semantic_diff.hpp>
#include <tessera/layout/layout_box.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
using K = SemanticChangeKind;

SemanticRecord entry(std::uint32_t node, std::optional<std::string> id, std::uint32_t parent, SemanticRole role,
                     std::string name = {}) {
    SemanticRecord r;
    r.node = node;
    r.id = std::move(id);
    r.parent = parent;
    r.role = role;
    r.name_source = name.empty() ? NameSource::none : NameSource::content;
    r.name = std::move(name);
    return r;
}
SemanticRecord control(SemanticRecord r, std::string action) {
    r.focusable = true;
    r.actions = {{"activate", std::move(action)}};
    return r;
}
SemanticRecord field(std::uint32_t node, std::string id, std::uint32_t parent, SemanticRole role, std::string name,
                     std::string value) {
    auto r = entry(node, std::move(id), parent, role, std::move(name));
    r.focusable = true;
    r.value = std::move(value);
    return r;
}

// An Inspector-like screen: a toolbar with Undo, a name and a position field, a checkbox and a list.
SemanticSnapshot screen(std::uint64_t generation) {
    SemanticSnapshot s{semantic_version, generation, 20, {}};
    auto& n = s.nodes;
    n.push_back(entry(0, "app", no_semantic_parent, SemanticRole::root));
    n.push_back(entry(1, "toolbar", 0, SemanticRole::toolbar));
    auto undo = entry(2, "undo", 1, SemanticRole::button, "Undo");
    undo.enabled = false;
    n.push_back(undo);
    auto name = field(4, "name", 0, SemanticRole::text_input, "Name", "Cube");
    name.focused = true;
    n.push_back(name);
    n.push_back(field(5, "position-x", 0, SemanticRole::number_input, "Position X", "0"));
    n.push_back(field(6, "position-y", 0, SemanticRole::number_input, "Position Y", "0"));
    auto enabled = control(entry(7, "enabled", 0, SemanticRole::checkbox, "Enabled"), "item.enabled");
    enabled.selected = false;
    n.push_back(enabled);
    n.push_back(entry(8, "items", 0, SemanticRole::list));
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::string label(1, char('a' + i));
        auto item = control(entry(9 + i, "item-" + label, 7, SemanticRole::list_item, label), "select." + label);
        item.selected = i == 0;
        n.push_back(item);
    }
    n.push_back(entry(12, std::nullopt, 0, SemanticRole::text, "Ready"));
    n.push_back(entry(13, std::nullopt, 0, SemanticRole::text, "Cube selected"));
    return s;
}
SemanticRecord& at(SemanticSnapshot& s, std::string_view id) {
    return *std::find_if(s.nodes.begin(), s.nodes.end(), [&](const SemanticRecord& r) { return r.id == id; });
}
SemanticDiff diff(const SemanticSnapshot& before, const SemanticSnapshot& after, SemanticDiffOptions options = {}) {
    auto result = diff_semantics(before, after, options);
    check(static_cast<bool>(result) && result.diagnostics.empty(), "Valid snapshots compare");
    return std::move(*result.value);
}
std::vector<K> kinds(const SemanticDiff& d) {
    std::vector<K> out;
    for (const auto& change : d.changes()) out.push_back(change.kind);
    return out;
}
SemanticNodeKey id(std::string value) { return {std::move(value)}; }
std::string saved(const SemanticDiff& d) {
    auto json = save_semantic_diff(d);
    check(static_cast<bool>(json), "A diff saves");
    return std::move(*json.value);
}

void unchanged_screens_have_no_changes() {
    const auto d = diff(screen(128), screen(129));
    check(d.empty() && d.from_generation() == 128 && d.to_generation() == 129 && !d.scope() && d.kinds().empty(),
          "An unchanged screen in a later generation has no semantic changes");
    check(saved(d) == R"({"changes":[],"format":"tessera-semantic-diff","from_generation":"128","kinds":null,)"
                      R"("scope":null,"to_generation":"129","version":1})"
                      "\n",
          "An empty diff saves as canonical JSON with its generations");
    // Preorder indices and parent entry indices are not identities: renumbering alone is not a change.
    auto renumbered = screen(129);
    for (auto& r : renumbered.nodes)
        if (r.node) r.node += 3;
    check(diff(screen(128), renumbered).empty(), "Renumbered preorder indices are not semantic changes");
}

void edit_reports_value_enabled_and_focus() {
    const auto before = screen(128);
    auto after = screen(129);
    at(after, "position-x").value = "2.5";
    auto& undo = at(after, "undo");
    undo = control(undo, "edit.undo");
    undo.enabled = true;
    at(after, "name").focused = false;
    at(after, "position-x").focused = true;
    const auto d = diff(before, after);
    check(kinds(d) == std::vector{K::focus_changed, K::enabled_changed, K::focusable_changed, K::action_added,
                                  K::value_changed},
          "An edit reports focus first, then each entry's changes in preorder and kind order");
    const auto& focus = d.changes()[0];
    check(!focus.target && focus.before == SemanticChangeValue{id("name")} &&
              focus.after == SemanticChangeValue{id("position-x")} && focus.before_node == 4u && focus.after_node == 5u,
          "Focus is one snapshot-level change between entry keys");
    const auto* value = d.find("position-x", K::value_changed);
    check(value && value->before == SemanticChangeValue{std::string("0")} &&
              value->after == SemanticChangeValue{std::string("2.5")} && value->before_node == 5u &&
              value->after_node == 5u,
          "A value change carries both presented values and both preorder indices");
    const auto* enabled = d.find("undo", K::enabled_changed);
    check(enabled && enabled->before == SemanticChangeValue{false} && enabled->after == SemanticChangeValue{true},
          "Undo becoming available is an enabled change");
    const auto* action = d.find("undo", K::action_added);
    check(action && action->after == SemanticChangeValue{SemanticAction{"activate", "edit.undo"}} &&
              std::holds_alternative<std::monostate>(action->before),
          "A new eligible action is reported with its binding");
    check(!d.find("position-y", K::value_changed) && !d.find("name", K::value_changed),
          "Untouched fields report nothing, so side effects are visible by absence");
    const auto json = saved(d);
    for (
        const auto* expected :
        {R"({"after":{"id":"position-x"},"after_node":5,"before":{"id":"name"},"before_node":4,"kind":"focus_changed","target":null})",
         R"({"after":"2.5","after_node":5,"before":"0","before_node":5,"kind":"value_changed","target":{"id":"position-x"}})",
         R"({"after":{"action":"edit.undo","binding":"activate"},"after_node":2,"before":null,"before_node":2,"kind":"action_added","target":{"id":"undo"}})",
         R"({"after":true,"after_node":2,"before":false,"before_node":2,"kind":"enabled_changed","target":{"id":"undo"}})"})
        check(json.find(expected) != std::string::npos, std::string("Saved diff contains ") + expected);
    check(saved(diff(before, after)) == json && diff(before, after) == d, "Equal inputs give equal diffs and JSON");
    // Losing focus entirely is reported with an empty after value.
    auto unfocused = screen(129);
    at(unfocused, "name").focused = false;
    const auto lost = diff(before, unfocused);
    check(kinds(lost) == std::vector{K::focus_changed} &&
              std::holds_alternative<std::monostate>(lost.changes()[0].after) && !lost.changes()[0].after_node,
          "Focus leaving every entry has an empty after key");
}

void toggles_selection_and_expansion() {
    auto before = screen(1);
    auto after = screen(2);
    at(after, "enabled").selected = true;
    at(after, "item-a").selected = false;
    at(after, "item-c").selected = true;
    at(after, "items").multiselectable = true;
    const auto d = diff(before, after);
    check(kinds(d) ==
              std::vector{K::checked_changed, K::multiselectable_changed, K::selection_changed, K::selection_changed},
          "A checkbox is checked; list items are selected; multiselectable appears");
    check(d.find("enabled", K::checked_changed)->after == SemanticChangeValue{true} &&
              !d.find("enabled", K::selection_changed),
          "Checkbox and switch state is reported as checked, never as selection");
    const auto* multi = d.find("items", K::multiselectable_changed);
    check(std::holds_alternative<std::monostate>(multi->before) && multi->after == SemanticChangeValue{true},
          "An optional field appearing has an empty before value");

    // A tree branch expanding trades its expand action for collapse.
    SemanticSnapshot tree{semantic_version, 1, 4, {entry(0, "app", no_semantic_parent, SemanticRole::root)}};
    tree.nodes.push_back(entry(1, "tree", 0, SemanticRole::tree));
    auto branch = entry(2, "branch", 1, SemanticRole::tree_item, "Branch");
    branch.focusable = true;
    branch.expanded = false;
    branch.actions = {{"activate", "select.branch"}, {"expand", "toggle.branch"}};
    tree.nodes.push_back(branch);
    auto open = tree;
    open.generation = 2;
    open.nodes[2].expanded = true;
    open.nodes[2].actions[1].binding = "collapse";
    open.nodes.push_back(entry(3, "leaf", 2, SemanticRole::tree_item, "Leaf"));
    const auto expanded = diff(tree, open);
    check(kinds(expanded) == std::vector{K::expanded_changed, K::action_added, K::action_removed, K::node_added},
          "Expanding reports the state, the traded actions and the revealed child");
    check(expanded.find("branch", K::action_added)->after ==
                  SemanticChangeValue{SemanticAction{"collapse", "toggle.branch"}} &&
              expanded.find("branch", K::action_removed)->before ==
                  SemanticChangeValue{SemanticAction{"expand", "toggle.branch"}},
          "Added and removed actions name their bindings");
}

void additions_removals_and_unnamed_identity() {
    const auto before = screen(1);
    auto after = screen(2);
    // Remove item-b, then add item-d and an unnamed button ahead of the unnamed status texts.
    after.nodes.erase(
        std::find_if(after.nodes.begin(), after.nodes.end(), [](const SemanticRecord& r) { return r.id == "item-b"; }));
    for (auto& r : after.nodes)
        if (r.parent != no_semantic_parent && r.parent > 9) --r.parent;
    auto d_item = control(entry(12, "item-d", 7, SemanticRole::list_item, "d"), "select.d");
    d_item.selected = false;
    auto apply = control(entry(13, std::nullopt, 0, SemanticRole::button, "Apply"), "apply");
    const auto texts = std::find_if(after.nodes.begin(), after.nodes.end(),
                                    [](const SemanticRecord& r) { return r.role == SemanticRole::text; });
    after.nodes.insert(texts, {d_item, apply});
    after.nodes[after.nodes.size() - 2].node = 14;
    after.nodes[after.nodes.size() - 1].node = 15;
    const auto d = diff(before, after);
    check(kinds(d) == std::vector{K::node_added, K::node_added, K::node_removed},
          "Insertions and a removal are the only changes; unnamed texts keep their identity by role");
    const auto& added = d.changes()[0];
    const auto* entry_value = std::get_if<SemanticEntry>(&added.after);
    check(added.target == id("item-d") && entry_value && entry_value->record.name == "d" &&
              entry_value->placement == SemanticPlacement{id("items"), 2} && !added.before_node &&
              added.after_node == 12u,
          "An added entry carries its record and its placement among its siblings");
    const auto& button = d.changes()[1];
    check(button.target == SemanticNodeKey{std::nullopt, "app", SemanticRole::button, 0},
          "An unnamed entry is keyed by role and ordinal within its nearest identified ancestor");
    const auto& removed = d.changes()[2];
    check(removed.target == id("item-b") && std::holds_alternative<SemanticEntry>(removed.before) &&
              removed.before_node == 10u && !removed.after_node,
          "A removed entry is reported after the after snapshot's entries, with its before record");
    const auto json = saved(d);
    check(json.find(R"("target":{"ordinal":0,"role":"button","within":"app"})") != std::string::npos &&
              json.find(R"("after":{"actions":[{"action":"select.d","binding":"activate"}],"enabled":true,)"
                        R"("focusable":true,"focused":false,"id":"item-d","name":"d","name_source":"content",)"
                        R"("placement":{"index":2,"parent":{"id":"items"}},"role":"list_item","selected":false})") !=
                  std::string::npos,
          "Entries save in Semantic JSON v1 spelling with their placement");

    // An unnamed text whose name changes is a name change, not a removal and addition.
    auto renamed = screen(2);
    renamed.nodes[renamed.nodes.size() - 1].name = "Sphere selected";
    const auto names = diff(before, renamed);
    check(kinds(names) == std::vector{K::name_changed} &&
              names.changes()[0].target == SemanticNodeKey{std::nullopt, "app", SemanticRole::text, 1},
          "Unnamed entries of one role match by ordinal");
}

void moves_report_new_placement() {
    const auto before = screen(1);
    // Move item-a to the end of its list: only item-a moved, b and c keep their relative order.
    auto after = screen(2);
    auto& n = after.nodes;
    std::rotate(n.begin() + 8, n.begin() + 9, n.begin() + 11);
    for (std::uint32_t i = 8; i < 11; ++i) n[i].node = 9 + (i - 8);
    auto d = diff(before, after);
    check(kinds(d) == std::vector{K::node_moved}, "A reorder reports one move for the entry that moved");
    const auto& moved = d.changes()[0];
    check(moved.target == id("item-a") && moved.before == SemanticChangeValue{SemanticPlacement{id("items"), 0}} &&
              moved.after == SemanticChangeValue{SemanticPlacement{id("items"), 2}} && moved.before_node == 9u &&
              moved.after_node == 11u,
          "A move carries the before and after placements");

    // [a, b, c] -> [d, b, a]: a keeps its order relative to b only by moving b, whose index stays 1.
    auto mixed = screen(2);
    auto d_item = control(entry(9, "item-d", 7, SemanticRole::list_item, "d"), "select.d");
    d_item.selected = false;
    auto b_item = mixed.nodes[9], a_item = mixed.nodes[8];
    b_item.node = 10;
    a_item.node = 11;
    mixed.nodes[8] = d_item;
    mixed.nodes[9] = b_item;
    mixed.nodes[10] = a_item;
    const auto reorder = diff(before, mixed);
    check(kinds(reorder) == std::vector{K::node_added, K::node_moved, K::node_removed} &&
              reorder.changes()[1].target == id("item-b") && reorder.changes()[1].before == reorder.changes()[1].after,
          "A move is reported even when additions and removals leave its index unchanged");

    // Reparent position-y into the toolbar, after Undo; preorder stays contiguous.
    auto reparented = screen(2);
    auto& r = reparented.nodes;
    auto position_y = r[5];
    position_y.parent = 1;
    position_y.node = 3;
    r.erase(r.begin() + 5);
    r.insert(r.begin() + 3, position_y);
    const auto moved_out = diff(before, reparented);
    const auto* change = moved_out.find("position-y", K::node_moved);
    check(change && change->before == SemanticChangeValue{SemanticPlacement{id("app"), 3}} &&
              change->after == SemanticChangeValue{SemanticPlacement{id("toolbar"), 1}},
          "A new parent is a move with both parents");
    check(kinds(moved_out) == std::vector{K::node_moved}, "Siblings shifted by a reparent are not moves");
}

void scope_and_kind_filters() {
    const auto before = screen(1);
    auto after = screen(2);
    at(after, "position-x").value = "2.5";
    at(after, "item-a").selected = false;
    at(after, "item-b").selected = true;
    at(after, "name").focused = false;
    at(after, "item-b").focused = true;
    const auto scoped = diff(before, after, {.scope = "items"});
    check(scoped.scope() == "items" &&
              kinds(scoped) == std::vector{K::focus_changed, K::selection_changed, K::selection_changed},
          "A scope compares only its subtree, and focus entering it is reported");
    check(!scoped.find("position-x", K::value_changed), "Changes outside the scope are left out");
    const auto selected = diff(before, after, {.kinds = {K::selection_changed, K::value_changed, K::value_changed}});
    check(kinds(selected) == std::vector{K::value_changed, K::selection_changed, K::selection_changed} &&
              std::vector(selected.kinds().begin(), selected.kinds().end()) ==
                  std::vector{K::selection_changed, K::value_changed},
          "A kind filter reports only those kinds and records them sorted and unique");
    check(saved(selected).find(R"("kinds":["selection_changed","value_changed"])") != std::string::npos &&
              saved(scoped).find(R"("scope":"items")") != std::string::npos,
          "Filters are recorded in the saved diff");
    std::vector<K> every;
    for (std::size_t k = 0; k < semantic_change_kind_count; ++k) every.push_back(static_cast<K>(k));
    check(diff(before, after, {.kinds = every}).kinds().empty(), "Listing every kind means no filter");

    // An entry that leaves the scope is reported as moved; a scope present on one side only still compares.
    auto removed = screen(2);
    removed.nodes.erase(removed.nodes.begin() + 7, removed.nodes.begin() + 11);
    for (std::size_t i = 7; i < removed.nodes.size(); ++i) removed.nodes[i].parent = 0;
    const auto gone = diff(before, removed, {.scope = "items"});
    check(kinds(gone) == std::vector{K::node_removed, K::node_removed, K::node_removed, K::node_removed},
          "Removing the scoped subtree reports each removed entry");
}

void rejects_invalid_input() {
    auto broken = screen(1);
    broken.nodes[3].parent = 9; // A parent must precede its child.
    auto result = diff_semantics(broken, screen(2));
    check(!result && has(result.diagnostics, "invalid_parent", "/before/nodes/3/parent"),
          "An invalid before snapshot is rejected with relocated diagnostics");
    result = diff_semantics(screen(1), broken);
    check(!result && has(result.diagnostics, "invalid_parent", "/after/nodes/3/parent"),
          "An invalid after snapshot is rejected with relocated diagnostics");
    result = diff_semantics(screen(1), screen(2), {.scope = "missing"});
    check(!result && has(result.diagnostics, "target_not_found", "/options/scope"),
          "A scope absent from both snapshots is rejected");
    result = diff_semantics(screen(1), screen(2), {.scope = ""});
    check(!result && has(result.diagnostics, "invalid_identifier", "/options/scope"), "An empty scope is rejected");
    result = diff_semantics(screen(1), screen(2), {.kinds = {K::name_changed, static_cast<K>(200)}});
    check(!result && has(result.diagnostics, "unknown_value", "/options/kinds/1"), "An undeclared kind is rejected");
    check(semantic_change_name(K::multiselectable_changed) == "multiselectable_changed" &&
              semantic_change_name(static_cast<K>(200)).empty(),
          "Kind names are the JSON spellings");
}

// The same comparison over snapshots captured from two generations of a real document.
void captured_generations() {
    const ValidationContext context{{"toggle", "save"}};
    const auto capture = [&](std::string_view json, std::uint64_t generation) {
        auto loaded = load_document(json, context);
        check(static_cast<bool>(loaded), "Fixture document loads");
        auto tree = UiTree::create(*loaded.value, context);
        check(static_cast<bool>(tree), "Fixture tree validates");
        std::vector<ResolvedStyle> styles((*tree.value)->size());
        PlaceholderTextShaper text;
        auto layout = compute_layout({tree.value->get(), styles, {240, 160}, &text});
        check(static_cast<bool>(layout), "Fixture layout computes");
        auto captured = capture_semantics({tree.value->get(), styles, &*layout.value}, generation);
        check(static_cast<bool>(captured), "Fixture semantics capture");
        return std::move(*captured.value);
    };
    const auto before = capture(R"({"version":1,"root":{"type":"Box","id":"panel","children":[
 {"type":"Box","id":"local","properties":{"toggle_role":"checkbox","selected":false,"focusable":true},
  "events":{"activate":"toggle"},"children":[{"type":"Text","properties":{"text":"Local"}}]},
 {"type":"Box","id":"save","properties":{"disabled":true},"events":{"activate":"save"},
  "children":[{"type":"Text","properties":{"text":"Save"}}]}]}})",
                                7);
    const auto after = capture(R"({"version":1,"root":{"type":"Box","id":"panel","children":[
 {"type":"Box","id":"local","properties":{"toggle_role":"checkbox","selected":true,"focusable":true},
  "events":{"activate":"toggle"},"children":[{"type":"Text","properties":{"text":"Local"}}]},
 {"type":"Box","id":"save","events":{"activate":"save"},
  "children":[{"type":"Text","properties":{"text":"Save"}}]}]}})",
                               8);
    const auto d = diff(before, after);
    check(d.find("local", K::checked_changed) &&
              d.find("local", K::checked_changed)->after == SemanticChangeValue{true},
          "Checking a captured checkbox is a checked change");
    check(d.find("save", K::enabled_changed) && d.find("save", K::action_added),
          "A captured button becoming enabled gains its action");
    check(std::all_of(d.changes().begin(), d.changes().end(),
                      [](const SemanticChange& c) { return c.target == id("local") || c.target == id("save"); }),
          "Only the two edited controls changed");
}
} // namespace

int main() {
    try {
        unchanged_screens_have_no_changes();
        edit_reports_value_enabled_and_focus();
        toggles_selection_and_expansion();
        additions_removals_and_unnamed_identity();
        moves_report_new_placement();
        scope_and_kind_filters();
        rejects_invalid_input();
        captured_generations();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Semantic diff checks passed.\n";
    return 0;
}
