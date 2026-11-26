#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
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

std::string key(std::size_t i) { return "k" + std::to_string(i); }
std::vector<ui::ListItem> rows(std::size_t count, std::initializer_list<std::size_t> disabled = {}) {
    std::vector<ui::ListItem> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        result.push_back({key(i), "Row " + std::to_string(i), "select",
                          std::find(disabled.begin(), disabled.end(), i) != disabled.end()});
    return result;
}
// `folders` expanded folders f<i> of `leaves` leaves f<i>-<j>, then a collapsed "closed" folder.
std::vector<ui::TreeItem> folders(std::size_t folders, std::size_t leaves, std::size_t collapsed = 1000) {
    std::vector<ui::TreeItem> result;
    for (std::size_t i = 0; i < folders; ++i) {
        ui::TreeItem folder{"f" + std::to_string(i), "Folder " + std::to_string(i), "select", "toggle", i != collapsed};
        for (std::size_t j = 0; j < leaves; ++j)
            folder.children.push_back({folder.key + "-" + std::to_string(j), "Leaf " + std::to_string(j), "select"});
        result.push_back(std::move(folder));
    }
    result.push_back({"closed", "Closed", "select", "toggle", false, false, {{"closed-0", "Hidden", "select"}}});
    return result;
}
ui::VirtualCollectionProps props(std::string id, float height, ui::VirtualWindow window) {
    return {.node = {.id = std::move(id), .style = {.height = Dimension::points(height)}}, .window = std::move(window)};
}

struct View {
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    std::optional<NodeHandle> find(std::string_view id) const { return tree->find(id); }
    NodeHandle node(std::string_view id) const {
        const auto found = tree->find(id);
        check(found.has_value(), "Expected a realized node");
        return *found;
    }
    const LayoutBox& box(std::string_view id) const {
        const auto target = node(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == target; });
        check(found != layout.boxes.end(), "Expected a displayed row");
        return *found;
    }
    std::size_t items() const {
        return static_cast<std::size_t>(std::count_if(
            document.root.children.front().children.begin(), document.root.children.front().children.end(),
            [](const UiNode& node) {
                const auto role = node.properties.find("collection_role");
                return role != node.properties.end() && (std::get<std::string>(role->second) == "list_item" ||
                                                         std::get<std::string>(role->second) == "tree_item");
            }));
    }
};
PlaceholderTextShaper text;

View build(const ui::Element& collection, std::string_view scroll_id, float offset, const StyleSheet& sheet = {},
           Size viewport = {160, 140}) {
    ui::Session session;
    auto authored = take(session.lower(ui::Column({collection}, {.id = "root"}), actions));
    View view;
    view.document = authored.document;
    view.tree = take(UiTree::create(view.document, actions));
    view.styles = take(resolve_styles({view.tree.get(), &sheet, {}, authored.styles}));
    std::vector<Point> offsets(view.tree->size());
    offsets[view.node(scroll_id).index] = {0, offset};
    view.layout = take(compute_layout({view.tree.get(), view.styles, viewport, &text, offsets}));
    return view;
}
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing semantic entry");
    return *at;
}

void bounded_realization() {
    ui::Session session;
    check(!session.lower(ui::ListView("Log", {}, rows(10000), {.node = {.id = "list"}}), actions).value,
          "An ordinary ListView cannot hold this many logical rows within the document node limit");
    const ui::VirtualWindow window{.item_extent = 20, .viewport = 100, .offset = 50000, .overscan = 2};
    const auto view =
        build(ui::VirtualListView("Log", "k2501", rows(10000), props("list", 100, window)), "list", window.offset);
    check(view.items() == 11, "Realize the viewport plus overscan and the first/last rows only");
    check(view.tree->size() < 60, "The realized document stays small");
    const auto& list = view.box("list");
    check(list.scroll && list.scroll->extent.height == 200000 && list.scroll->offset.y == 50000,
          "Spacers keep the scroll extent equal to every logical row stacked");
    for (std::size_t i = 2498; i < 2507; ++i)
        check(view.box("list-item-" + key(i)).border_box.origin.y ==
                  list.border_box.origin.y + (static_cast<float>(i) - 2500) * 20,
              "Realized rows sit at their logical positions");
    check(view.box("list-item-k9999").border_box.origin.y == list.border_box.origin.y + 7499 * 20,
          "The last row stays at its logical position for End");
    check(!view.find("list-item-k2497") && !view.find("list-item-k2507"), "Rows beyond overscan stay unrealized");

    const auto semantics = take(build_semantic_tree(view.semantic()));
    check(entry(semantics, "list").item_count == 10000 && entry(semantics, "list").name == "Log",
          "The list reports its logical count and display-none title");
    check(entry(semantics, "list-item-k2501").item_position == 2502 &&
              entry(semantics, "list-item-k2501").selected == true &&
              entry(semantics, "list-item-k0").item_position == 1,
          "Realized rows report 1-based logical positions and controlled selection");
    const auto exposed = std::count_if(semantics.nodes.begin(), semantics.nodes.end(),
                                       [](const SemanticNode& node) { return node.role == SemanticRole::list_item; });
    check(exposed == 11, "Only realized rows are semantic entries");
    auto snapshot = take(capture_semantics(view.semantic(), 3));
    check(take(load_semantics(take(save_semantics(snapshot)))) == snapshot,
          "Logical count/position survive Semantic JSON");
    auto bad = snapshot;
    bad.nodes.back().item_count = 4;
    check(!validate(bad).empty(), "Only lists and trees carry an item count");
    bad = snapshot;
    for (auto& node : bad.nodes)
        if (node.role == SemanticRole::list_item) node.item_position = 0;
    check(!validate(bad).empty(), "Positions are 1-based");
    check(take(load_document(take(save_document(view.document, actions)), actions)) == view.document,
          "Virtual collections survive UI JSON");

    ReplayRecording recording;
    recording.context = actions;
    recording.viewport = {160, 140};
    recording.document = view.document;
    recording.styles = view.styles;
    recording.steps = {ReplaySemanticAction{SemanticTarget{SemanticRole::list_item, "Row 2500"}, "activate"}};
    const auto replay = take(play_replay(take(load_replay(take(save_replay(recording)))), text));
    check(replay.actions.size() == 1 && replay.actions.front().action == "select" &&
              replay.semantics.front().nodes == own_semantic_records(semantics),
          "Replay acts on realized rows and retains logical observations");

    const auto scrolled =
        take(route_scroll(view.hit(), {{list.border_box.origin.x + 4, list.border_box.origin.y + 4}, {0, 160000}}));
    check(scrolled.updates.size() == 1 && scrolled.updates.front().offset.y == 200000 - 100,
          "Wheel input scrolls across unrealized rows to the logical end");
}

void window_math() {
    ui::VirtualWindow window{.item_extent = 20, .viewport = 50, .offset = 0, .overscan = 2};
    check(ui::virtual_range(0, window) == ui::VirtualRange{}, "No rows realize nothing");
    check(ui::virtual_range(10, window) == ui::VirtualRange{0, 5}, "Leading window plus trailing overscan");
    window.offset = 1000;
    check(ui::virtual_range(10, window) == ui::VirtualRange{5, 10}, "Offsets clamp like layout");
    window = {.item_extent = 20, .viewport = 0, .offset = 0, .overscan = 0};
    check(ui::virtual_range(10, window) == ui::VirtualRange{0, 1}, "A zero viewport realizes the leading row");
    window = {.item_extent = 20, .viewport = 40, .offset = 20, .overscan = 0};
    check(ui::virtual_range(10, window) == ui::VirtualRange{1, 3}, "Rows touching the trailing edge are excluded");
    for (auto invalid : {ui::VirtualWindow{.item_extent = 0}, ui::VirtualWindow{.offset = std::nanf("")},
                         ui::VirtualWindow{.viewport = -1}, ui::VirtualWindow{.overscan = 65}})
        check(ui::virtual_range(10, invalid) == ui::VirtualRange{}, "Invalid windows realize nothing");

    std::vector<std::string> keys;
    for (std::size_t i = 0; i < 10; ++i) keys.push_back(key(i));
    check(ui::virtual_anchor(keys, 20, 45) == ui::VirtualAnchor{"k2", 5}, "Anchor row and local offset");
    check(ui::virtual_anchor(keys, 20, -3) == ui::VirtualAnchor{"k0", 0}, "Negative offsets anchor the first row");
    check(ui::virtual_anchor(keys, 20, 1000) == ui::VirtualAnchor{"k9", 820}, "Offsets past the end anchor the last");
    check(!ui::virtual_anchor({}, 20, 0) && !ui::virtual_anchor(keys, 0, 0), "No rows or extent, no anchor");
    keys.insert(keys.begin(), {"new0", "new1", "new2"});
    check(ui::virtual_offset(keys, 20, {"k2", 5}) == 105.f, "Prepending rows keeps the anchored row in place");
    check(!ui::virtual_offset(keys, 20, {"gone", 0}), "A removed anchor key has no offset");

    window = {.item_extent = 20, .viewport = 50, .offset = 0};
    check(ui::virtual_reveal(keys, window, "new1") == 0.f, "A visible row keeps the offset");
    check(ui::virtual_reveal(keys, window, "k2") == 70.f, "A following row aligns its trailing edge");
    window.offset = 70;
    check(ui::virtual_reveal(keys, window, "new0") == 0.f, "A preceding row aligns its leading edge");
    window.item_extent = 60;
    check(ui::virtual_reveal(keys, window, "k9") == 720.f, "A row taller than the viewport aligns its leading edge");
    check(!ui::virtual_reveal(keys, window, "gone"), "Unknown keys cannot be revealed");

    const auto tree = folders(2, 2, 1);
    check(ui::virtual_keys(tree) == std::vector<std::string>{"f0", "f0-0", "f0-1", "f1", "closed"},
          "Tree keys follow visible preorder only");
    check(ui::virtual_keys(rows(3)) == std::vector<std::string>{"k0", "k1", "k2"}, "List keys follow item order");
}

void full_frame_parity() {
    StyleSheet sheet;
    for (std::size_t i = 0; i < 200; ++i)
        sheet.rules.push_back({StyleSelector::of_id("list-item-" + key(i)),
                               {.background = Color{static_cast<float>(i % 7) / 7, static_cast<float>(i % 11) / 11,
                                                    static_cast<float>(i) / 200, 1}}});
    for (const float offset : {0.f, 35.f, 1990.f, 3900.f, 5000.f}) {
        auto capture = [&](ui::VirtualWindow window) {
            auto view = build(ui::VirtualListView("Rows", {}, rows(200, {3}), props("list", 100, window)), "list",
                              offset, sheet);
            for (auto& style : view.styles) style.color.a = 0;
            const auto paint = take(build_paint_list({view.tree.get(), view.styles, &view.layout, &text}));
            CpuFrameCapture frames;
            return std::pair{take(frames.capture({0, {160, 140}, 1, {160, 140}, &paint})), std::move(view)};
        };
        const auto full = capture({.item_extent = 20, .viewport = 4000, .offset = 0, .overscan = 0});
        const auto bounded = capture({.item_extent = 20, .viewport = 100, .offset = offset, .overscan = 1});
        check(full.second.items() == 200 && bounded.second.items() < 12, "Reference realizes every row");
        check(full.first == bounded.first, "A virtualized frame matches the fully realized frame");
        for (const auto& node : bounded.second.document.root.children.front().children)
            if (node.id && node.id->starts_with("list-item-"))
                check(bounded.second.box(*node.id).border_box == full.second.box(*node.id).border_box,
                      "Realized rows keep their full-frame geometry");
    }
}

// Host loop: recompose with focus pinned after every scroll or focus change.
struct Host {
    std::vector<ui::ListItem> items = rows(50, {0, 10, 11, 12, 49});
    float offset = 0;
    std::optional<std::string> focus_key;
    FocusDispatcher focus;
    View view;
    std::chrono::microseconds time{};
    ui::VirtualWindow window() const {
        ui::VirtualWindow result{.item_extent = 20, .viewport = 60, .offset = offset, .overscan = 0};
        if (focus_key) result.pinned.push_back(*focus_key);
        return result;
    }
    void rebuild() {
        view = build(ui::VirtualListView("Rows", {}, items, props("list", 60, window())), "list", offset);
        offset = view.box("list").scroll->offset.y;
        settle(take(focus.refresh(view.hit())).focused);
    }
    void settle(std::optional<NodeHandle> focused) {
        const auto id = focused ? view.tree->get(*focused)->id : std::nullopt;
        if (id == (focus_key ? std::optional("list-item-" + *focus_key) : std::nullopt)) return;
        focus_key = id ? std::optional(id->substr(10)) : std::nullopt;
        if (focus_key) {
            for (const auto& update : take(scroll_into_view(view.hit(), *focused))) offset = update.offset.y;
        }
        rebuild();
    }
    template<class Event> void send(Event event) {
        settle(take(focus.dispatch(view.hit(), {time += 1us, std::move(event)})).focused);
    }
    void focus_on(std::string_view row) { settle(take(focus.focus(view.hit(), view.node(row))).focused); }
    bool visible(std::string_view row) const {
        const auto& list = view.box("list").border_box;
        const auto& box = view.box(row).border_box;
        return box.origin.y >= list.origin.y && box.origin.y + box.size.height <= list.origin.y + list.size.height;
    }
};

void navigation_through_realization() {
    Host host;
    host.rebuild();
    check(host.view.items() == 4 && host.view.find("list-item-k48"),
          "Realize the window and the first/last enabled rows");
    host.focus_on("list-item-k1");
    host.send(KeyDown{Key::end});
    check(host.focus_key == "k48" && host.visible("list-item-k48") && host.offset == 48 * 20 + 20 - 60,
          "End reaches the last enabled logical row and reveals it");
    host.send(KeyDown{Key::home});
    check(host.focus_key == "k1" && host.offset == 20, "Home reaches the first enabled logical row");
    // A host operation targeting an unrealized row reveals it first, then focuses the realized row.
    check(!host.view.find("list-item-k9"), "Rows outside the window are unrealized");
    host.offset = *ui::virtual_reveal(ui::virtual_keys(host.items), host.window(), "k9");
    host.rebuild();
    host.focus_on("list-item-k9");
    check(host.focus_key == "k9" && host.offset == 140 && host.visible("list-item-k9"), "Reveal realizes the row");
    host.offset = 600;
    host.rebuild();
    check(host.focus_key == "k9" && !host.visible("list-item-k9") && host.view.find("list-item-k13") &&
              host.view.find("list-item-k8"),
          "Scrolling away keeps the focused row and its enabled neighbors realized");
    host.send(Navigate{Direction::down});
    check(host.focus_key == "k13" && host.visible("list-item-k13"),
          "Down skips disabled rows to an unrealized neighbor through the pinned window");
    host.send(FocusNext{});
    check(host.focus_key == "k14", "Tab reaches the next row");
    host.send(FocusPrevious{});
    host.send(Navigate{Direction::up});
    check(host.focus_key == "k9", "Up skips the disabled run backward");
    // Removing the focused row: pin its following survivor so neighbor recovery lands where a full list would.
    host.items.erase(host.items.begin() + 9);
    host.focus_key = "k13";
    host.rebuild();
    check(host.focus_key == "k13", "Neighbor recovery reaches the following surviving row");
}

void tree_realization() {
    const ui::VirtualWindow window{.item_extent = 20, .viewport = 60, .offset = (10 * 21 + 1 + 15) * 20, .overscan = 0};
    auto view = build(ui::VirtualTreeView("Scene", "closed-0", folders(30, 20), props("tree", 60, window)), "tree",
                      window.offset);
    check(view.items() == 3 + 1 + 1 + 1, "Window, first, last and the window's ancestor");
    check(view.box("tree-item-f10").border_box.origin.y < view.box("tree").border_box.origin.y,
          "An ancestor scrolled past is realized at its logical position");
    auto semantics = take(build_semantic_tree(view.semantic()));
    const auto& leaf = entry(semantics, "tree-item-f10-15");
    check(semantics.nodes[leaf.parent].id == "tree-item-f10" && leaf.item_position == 227 &&
              entry(semantics, "tree").item_count == 30 * 21 + 1,
          "Logical hierarchy, positions and the visible count are preserved");

    FocusDispatcher focus;
    std::chrono::microseconds time{};
    (void)take(focus.focus(view.hit(), view.node("tree-item-f10-15")));
    check(take(focus.dispatch(view.hit(), {time += 1us, Navigate{Direction::left}})).focused ==
              view.node("tree-item-f10"),
          "Left reaches a parent outside the viewport");
    const auto collapse = take(focus.dispatch(view.hit(), {time += 1us, Navigate{Direction::left}}));
    check(collapse.actions.size() == 1 && collapse.actions.front().binding == "collapse",
          "Left on an open branch requests its ordinary collapse command");

    (void)take(focus.focus(view.hit(), view.node("tree-item-f10-15")));
    auto pinned = window;
    pinned.pinned = {"f10-15"};
    pinned.offset = 0;
    view = build(ui::VirtualTreeView("Scene", {}, folders(30, 20, 10), props("tree", 60, pinned)), "tree", 0);
    const auto concealed = view.find("tree-item-f10-15");
    check(view.find("tree-item-f10") && concealed &&
              std::none_of(view.layout.boxes.begin(), view.layout.boxes.end(),
                           [&](const LayoutBox& box) { return box.node == *concealed; }),
          "A collapsed pinned row stays as a display-none row after its visible ancestor");
    semantics = take(build_semantic_tree(view.semantic()));
    check(std::none_of(semantics.nodes.begin(), semantics.nodes.end(),
                       [](const SemanticNode& node) { return node.id == "tree-item-f10-15"; }),
          "A concealed row has no semantic entry");
    check(take(focus.refresh(view.hit())).focused == view.node("tree-item-f10"),
          "Focus recovers to the collapsed ancestor");
}

void rejection() {
    ui::Session session;
    auto reject = [&](const ui::Element& element, std::string_view code) {
        check(has(session.lower(element, actions).diagnostics, code, "/root"), std::string("Expected ") += code);
    };
    reject(ui::VirtualListView("Rows", {}, rows(3), {.node = {.id = "list"}, .window = {.item_extent = 0}}),
           "virtual_window");
    reject(ui::VirtualListView("Rows", {}, rows(3), {.node = {.id = "list"}, .window = {.overscan = 65}}),
           "virtual_window");
    reject(ui::VirtualListView("Rows", {}, rows(3),
                               {.node = {.id = "list"}, .window = {.pinned = std::vector<std::string>(65, "k0")}}),
           "virtual_window");
    reject(ui::VirtualListView("Rows", {}, rows(20), {.node = {.id = "list"}, .window = {.item_extent = 1000000}}),
           "virtual_extent");
    reject(ui::VirtualListView("Rows", {}, rows(2000), {.node = {.id = "list"}, .window = {.viewport = 30000}}),
           "collection_limit");
    reject(ui::VirtualListView("Rows", {}, rows(3)), "collection_id");
    reject(ui::VirtualListView("", {}, rows(3), {.node = {.id = "list"}}), "collection_label");
    reject(ui::VirtualListView("Rows", "missing", rows(3), {.node = {.id = "list"}}), "collection_selection");
    auto duplicate = folders(2, 2, 1);
    duplicate.back().children.front().key = "f0-0"; // Hidden under a collapsed branch, still validated.
    reject(ui::VirtualTreeView("Scene", {}, duplicate, {.node = {.id = "tree"}}), "collection_keys");
    auto leaf = folders(1, 1);
    leaf.front().children.front().toggle_action = "toggle";
    reject(ui::VirtualTreeView("Scene", {}, leaf, {.node = {.id = "tree"}}), "tree_leaf");
    check(session.lower(ui::VirtualListView("Rows", {}, {}, {.node = {.id = "list"}}), actions).value.has_value(),
          "An empty virtual collection is valid");

    UiDocument document;
    document.root.properties["collection_role"] = std::string("list");
    document.root.properties["item_count"] = 3.0;
    for (const double position : {2.0, 1.0}) {
        UiNode item;
        item.properties["collection_role"] = std::string("list_item");
        item.properties["item_position"] = position;
        document.root.children.push_back(std::move(item));
    }
    check(has(validate(document), "item_position", "/root/children/1/properties/item_position"),
          "Positions must increase in collection order");
    document.root.children[1].properties["item_position"] = 4.0;
    check(has(validate(document), "item_position", "/root/children/1/properties/item_position"),
          "Positions must stay within the count");
    document.root.children[1].properties.erase("item_position");
    check(validate(document).empty(), "Display-none rows of a counted collection may omit positions");
    document.root.properties["item_count"] = 2.5;
    check(has(validate(document), "item_count", "/root/properties/item_count"), "Counts must be whole");
    document.root.properties.erase("item_count");
    document.root.children.pop_back();
    check(has(validate(document), "item_position", "/root/children/0/properties/item_position"),
          "A position requires the collection's count");
    document.root.properties["collection_role"] = std::string("menu");
    document.root.properties["item_count"] = 1.0;
    check(has(validate(document), "collection_state", "/root/properties/item_count"),
          "Only lists and trees report a count");
}
} // namespace

int main() {
    try {
        bounded_realization();
        window_math();
        full_frame_parity();
        navigation_through_realization();
        tree_realization();
        rejection();
        std::cout << "Virtual collection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
