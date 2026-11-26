#include <tessera/ui/authoring.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/semantics/semantics.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

using namespace tessera;
using namespace tessera::authoring;
using tessera::test::check;
using tessera::test::has;

static_assert(!std::is_constructible_v<Value<std::string>, Signal<bool>>);
static_assert(!std::is_copy_constructible_v<Session>);

namespace {
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.diagnostics.front().message);
    return std::move(*result.value);
}
const SourceLocation origin{"fixture://settings.cpp", {20, 45}};
const ValidationContext actions{{"toggle"}};

void composition_and_updates() {
    Session ui;
    const auto enabled = ui.state("enabled", false);
    int calculations = 0;
    const auto summary = ui.computed("summary", [enabled, &calculations] {
        ++calculations;
        return enabled.read() ? std::string("Enabled") : std::string("Disabled");
    });
    const auto disabled = ui.state("disabled", false);
    const auto form = Column(
        {Text(summary, {.id = "summary", .source = origin}),
         Row({Button("Toggle", "toggle", {.node = {.id = "toggle"}, .disabled = disabled})}, {.style = {.gap = 8.f}})},
        {.id = "settings"});
    auto first = take(ui.lower(form, actions));
    check(calculations == 1 &&
              first.document.root.children[0].properties.at("text") == Property{std::string("Disabled")},
          "Initial derived value differs");
    auto tree = take(UiTree::create(first.document, actions));
    check(tree->size() == 5 && first.styles.size() == 5 && first.styles[2].direction == FlexDirection::row,
          "Composition or preorder alignment differs");
    check(first.styles[0].direction == FlexDirection::column && first.styles[2].gap == 8,
          "Row/Column declarations differ");
    check(validate_sources(first.document, first.sources).empty() && first.sources.nodes[1].invocation == origin &&
              !first.sources.nodes[1].mapped,
          "C++ expression provenance must not invent template byte spans");
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, first.styles}));
    check(styles[2].direction == FlexDirection::row && styles[2].gap == 8, "Ordinary style resolution differs");
    ui.publish();
    UiDocument ordinary;
    ordinary.root.id = "settings";
    UiNode summary_node;
    summary_node.kind = NodeKind::text;
    summary_node.id = "summary";
    summary_node.properties["text"] = std::string("Disabled");
    UiNode button_node;
    button_node.id = "toggle";
    button_node.properties = {{"focusable", true}, {"disabled", false}};
    button_node.events["activate"] = "toggle";
    UiNode label_node;
    label_node.kind = NodeKind::text;
    label_node.properties["text"] = std::string("Toggle");
    button_node.children.push_back(label_node);
    UiNode row_node;
    row_node.children.push_back(button_node);
    ordinary.root.children = {summary_node, row_node};
    check(first.document == ordinary, "High-level composition must lower to the ordinary document representation");
    ui.update([&] {
        enabled.write(true);
        disabled.write(true);
    });
    auto second = take(ui.lower(form, actions));
    check(calculations == 2 &&
              second.document.root.children[0].properties.at("text") == Property{std::string("Enabled")} &&
              second.document.root.children[1].children[0].properties.at("disabled") == Property{true},
          "State and computed/disabled bindings must settle together");
    check(first.document.root.children[0].properties.at("text") == Property{std::string("Disabled")},
          "Lowering mutated an accepted candidate");
    (void)take(ui.lower(form, actions));
    check(calculations == 2, "Unchanged descriptions must reuse graph caches");
    ui.publish();
}

void rejection_and_preflight() {
    Session ui;
    int calculations = 0;
    auto text = ui.computed("label", [&] {
        ++calculations;
        return std::string("Accepted");
    });
    auto accepted = take(ui.lower(Text(text, {.id = "value"})));
    auto invalid_style = ui.lower(Text(text, {.style = {.opacity = 2.f}, .source = origin}));
    check(!invalid_style && has(invalid_style.diagnostics, "out_of_range", "/root/style/opacity") &&
              invalid_style.diagnostics[0].source && invalid_style.diagnostics[0].source->location == origin,
          "Style rejection must locate the supplied expression");
    auto invalid_action = ui.lower(Button("Toggle", "unknown", {.node = {.source = origin}}), actions);
    check(!invalid_action && invalid_action.diagnostics[0].source, "Unknown action needs a located diagnostic");
    check(!ui.lower(Column({Text("A", {.id = "duplicate"}), Text("B", {.id = "duplicate"})})),
          "Duplicate identity accepted");
    check(!ui.lower(Text("bad", {.source = SourceLocation{"fixture", {9, 1}}})), "Invalid source span accepted");
    auto invalid_text = ui.lower(Text(std::string("\xff"), {.source = origin}));
    check(!invalid_text && has(invalid_text.diagnostics, "invalid_utf8", "/root/properties/text") &&
              invalid_text.diagnostics[0].source->location == origin,
          "Invalid text needs a located property diagnostic");
    check(accepted.document.root.properties.at("text") == Property{std::string("Accepted")},
          "Rejected candidates changed accepted content");

    Session pending;
    int reads = 0;
    auto value = pending.computed("pending", [&] {
        ++reads;
        return std::string("Pending");
    });
    check(!pending.lower(Text(value, {.style = {.gap = -1.f}})) && reads == 0,
          "Invalid declarations must fail before graph evaluation");
    check(!pending.lower(Column({Text(value, {.id = "same"}), Text("other", {.id = "same"})})) && reads == 0,
          "Document preflight must precede graph evaluation");
    check(!pending.lower(Column({Text(value), Text(std::string("\xff"))})) && reads == 0,
          "Invalid literal props must fail before graph evaluation");
    (void)take(pending.lower(Text(value)));
    check(reads == 1, "Preflight rejection must permit repair");
}

void scroll_view_contract() {
    Session ui;
    // ScrollView keeps ordinary identity/style/source handling and constrains
    // overflow independently of a caller's competing container declarations.
    const auto view = ScrollView({Text("Content", {.id = "content"})}, {.id = "list",
                                                                        .classes = {"pane"},
                                                                        .style = {.direction = FlexDirection::row,
                                                                                  .overflow = Overflow::visible,
                                                                                  .width = Dimension::points(100),
                                                                                  .height = Dimension::points(40),
                                                                                  .gap = 8.f},
                                                                        .source = origin});
    const auto candidate = take(ui.lower(view));
    const auto tree = take(UiTree::create(candidate.document));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, candidate.styles}));
    check(tree->size() == 2 && tree->find("content") &&
              candidate.document.root.classes == std::vector<std::string>{"pane"} &&
              styles[0].direction == FlexDirection::column && styles[0].overflow == Overflow::scroll &&
              styles[0].width == Dimension::points(100) && styles[0].height == Dimension::points(40) &&
              styles[0].gap == 8,
          "ScrollView must use ordinary Column lowering and retain supplied styles/identity");
    check(validate_sources(candidate.document, candidate.sources).empty() &&
              candidate.sources.nodes[0].style_argument == origin,
          "ScrollView lost its caller's source");
    const auto invalid =
        ui.lower(ScrollView({Text("Content")}, {.style = {.height = Dimension::points(-1)}, .source = origin}));
    check(!invalid && invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == origin,
          "Invalid ScrollView constraints need a source-located diagnostic");
}

void stack_contract() {
    Session ui;
    const auto candidate = take(ui.lower(
        Stack({Text("Scene", {.id = "scene"}),
               Row({Button("Tools", "toggle")},
                   {.id = "toolbar", .style = {.stack_horizontal = Align::end, .stack_vertical = Align::start}})},
              {.id = "viewport",
               .classes = {"preview"},
               .style = {.display = Display::flex, .overflow = Overflow::clip, .width = Dimension::points(200)},
               .source = origin}),
        actions));
    const auto tree = take(UiTree::create(candidate.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, candidate.styles}));
    check(candidate.document.root.kind == NodeKind::box && tree->size() == 5 && styles[0].display == Display::stack &&
              styles[0].overflow == Overflow::clip && styles[0].width == Dimension::points(200) &&
              styles[2].direction == FlexDirection::row && styles[2].stack_horizontal == Align::end &&
              styles[2].stack_vertical == Align::start && styles[3].stack_horizontal == Align::stretch,
          "Stack must lower ordinary Box children and non-inherited placement declarations");
    check(candidate.document.root.classes == std::vector<std::string>{"preview"} &&
              validate_sources(candidate.document, candidate.sources).empty() &&
              candidate.sources.nodes[0].style_argument == origin,
          "Stack must preserve ordinary identity and caller provenance");
    const auto hidden = take(ui.lower(Stack({Text("Scene")}, {.style = {.display = Display::none}})));
    check(hidden.styles[0].display == Display::none, "An explicitly non-displayed Stack must remain absent");
    const auto invalid =
        ui.lower(Stack({Text("Scene", {.style = {.stack_horizontal = static_cast<Align>(9)}, .source = origin})}));
    check(!invalid && has(invalid.diagnostics, "unknown_value", "/root/children/0/style/stack_horizontal") &&
              invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == origin,
          "Invalid Stack placement needs a source-located authoring diagnostic");
}

void grid_contract() {
    Session ui;
    const auto candidate = take(ui.lower(
        Grid({Text("Label"),
              Button(
                  "Value", "toggle",
                  {.node = {.id = "value", .style = {.grid_horizontal = Align::end, .grid_vertical = Align::center}}})},
             {.id = "inspector",
              .classes = {"properties"},
              .style = {.display = Display::stack,
                        .grid_columns = std::vector<GridTrack>{GridTrack::points(80), GridTrack::fraction()},
                        .grid_rows = std::vector<GridTrack>{GridTrack{}},
                        .column_gap = 8.f,
                        .row_gap = 4.f},
              .source = origin}),
        actions));
    const auto tree = take(UiTree::create(candidate.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, candidate.styles}));
    check(tree->size() == 4 && styles[0].display == Display::grid && styles[0].grid_columns.size() == 2 &&
              styles[0].grid_rows.size() == 1 && styles[0].column_gap == 8 && styles[0].row_gap == 4 &&
              styles[2].grid_horizontal == Align::end && styles[2].grid_vertical == Align::center &&
              styles[3].grid_horizontal == Align::stretch && styles[3].grid_columns.empty(),
          "Grid must lower ordinary children and non-inherited tracks/placement");
    check(candidate.document.root.id == "inspector" &&
              candidate.document.root.classes == std::vector<std::string>{"properties"} &&
              validate_sources(candidate.document, candidate.sources).empty() &&
              candidate.sources.nodes[0].style_argument == origin,
          "Grid must preserve identity and provenance");
    check(take(ui.lower(Grid({}, {.style = {.display = Display::none}}))).styles[0].display == Display::none,
          "An explicitly non-displayed Grid must remain absent");
    const auto invalid = ui.lower(
        Grid({}, {.style = {.grid_columns = std::vector<GridTrack>{GridTrack::fraction(0)}}, .source = origin}));
    check(!invalid && has(invalid.diagnostics, "out_of_range", "/root/style/grid_columns/0/value") &&
              invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == origin,
          "Invalid Grid tracks need source-located authoring diagnostics");
}

void label_and_spacer() {
    Session ui;
    const auto caption = ui.state("caption", std::string("Display name"));
    const auto form = Row(
        {Label(caption, "name", {.id = "name-caption", .classes = {"muted"}, .source = origin}), Spacer({.id = "fill"}),
         Button("Name", "toggle", {.node = {.id = "name", .style = {.width = Dimension::points(40)}}}),
         Spacer({.style = {.width = Dimension::points(12), .grow = 0.f}})},
        {.style = {.width = Dimension::points(300)}});
    const auto first = take(ui.lower(form, actions));
    const auto& label = first.document.root.children[0];
    check(label.kind == NodeKind::text && label.classes == std::vector<std::string>{"muted"} &&
              label.properties.at("text") == Property{std::string("Display name")} &&
              label.properties.at("label_for") == Property{NodeReference{"name"}} && label.properties.size() == 2,
          "Label must lower to a Text naming its target through label_for alone");
    check(validate_sources(first.document, first.sources).empty() &&
              first.sources.nodes[1].property_arguments.at("label_for") == origin,
          "Label needs its caller's source origin");
    const auto& spacer = first.document.root.children[1];
    check(spacer.kind == NodeKind::box && spacer.children.empty() && spacer.events.empty() &&
              spacer.properties == std::map<std::string, Property, std::less<>>{{"pointer_events", false}} &&
              first.styles[2].grow == 1.f && first.styles[5].grow == 0.f,
          "Spacer must lower to an empty input-transparent Box that grows unless grow is set");
    ui.update([&] { caption.write("Name"); });
    const auto second = take(ui.lower(form, actions));
    check(second.document.root.children[0].properties.at("text") == Property{std::string("Name")} &&
              first.document.root.children[0].properties.at("text") == Property{std::string("Display name")},
          "Label text binding must settle in a new candidate");

    const auto tree = take(UiTree::create(second.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, second.styles}));
    PlaceholderTextShaper shaper;
    const auto layout = take(compute_layout({tree.get(), styles, {400, 100}, &shaper}));
    const auto box = [&](std::string_view id) {
        const auto node = *tree->find(id);
        return std::find_if(layout.boxes.begin(), layout.boxes.end(), [&](const auto& b) { return b.node == node; })
            ->border_box;
    };
    const auto caption_end = box("name-caption").origin.x + box("name-caption").size.width;
    check(box("name").origin.x == 300 - 12 - 40 && box("fill").origin.x == caption_end &&
              box("fill").size.width == box("name").origin.x - caption_end,
          "Spacer must take the free space between its siblings");
    const HitTestInput hit{tree.get(), styles, &layout};
    const auto over = take(hit_test(hit, {box("fill").origin.x + 1, box("fill").origin.y + 1})).target;
    check(over == tree->root(), "Spacer must not intercept pointer hits");
    const auto at = take(hit_test(hit, {box("name-caption").origin.x + 1, box("name-caption").origin.y + 1})).target;
    check(at && take(label_target(*tree, *at)) == tree->find("name") && !take(label_target(*tree, *tree->find("name"))),
          "A press on the Label must resolve its target, and other nodes none");

    const auto empty = ui.lower(Row({Label("Name", "", {.source = origin})}), actions);
    check(!empty && empty.diagnostics.size() == 1 && has(empty.diagnostics, "label_target", "/root/children/0") &&
              empty.diagnostics[0].source && empty.diagnostics[0].source->location == origin,
          "An empty Label target needs one source-located diagnostic");
    const auto missing = ui.lower(Row({Label("Name", "absent", {.source = origin})}), actions);
    check(!missing && has(missing.diagnostics, "invalid_reference", "/root/children/0/properties/label_for/ref"),
          "A Label target must resolve in the document");
}

void property_grid() {
    Session ui;
    const auto caption = ui.state("caption", std::string("Display name"));
    const auto help = Text("Help text", {.classes = {"hint"}});
    const auto narrow = Text("Narrow", {.id = "narrow", .style = {.grid_column_span = std::uint16_t{1}}});
    const auto form = Column(
        {PropertyGrid({{caption,
                        "name",
                        Button("Name", "toggle", {.node = {.id = "name"}}),
                        {.classes = {"own"}, .source = origin}},
                       {"Size", "size-x", Button("Size", "toggle", {.node = {.id = "size-x"}}), {.id = "size-label"}},
                       {Row({Label("On", "enabled")}, {.id = "custom"}),
                        Button("Enabled", "toggle", {.node = {.id = "enabled"}})},
                       InspectorRow(help),
                       InspectorRow(narrow)},
                      {.node = {.id = "properties",
                                .style = {.grid_columns = std::vector<GridTrack>{GridTrack::points(5)},
                                          .grid_rows = std::vector<GridTrack>{GridTrack{}},
                                          .row_gap = 4.f},
                                .source = origin},
                       .caption_classes = {"muted"}}),
         help},
        {.style = {.width = Dimension::points(300)}});
    const auto lowered = take(ui.lower(form, actions));
    const auto& grid = lowered.document.root.children[0];
    const auto& name = grid.children[0];
    check(grid.id == "properties" && grid.children.size() == 8 && name.kind == NodeKind::text &&
              name.id == "name-caption" && name.classes == std::vector<std::string>{"own", "muted"} &&
              name.properties.at("text") == Property{std::string("Display name")} &&
              name.properties.at("label_for") == Property{NodeReference{"name"}} && grid.children[1].id == "name" &&
              grid.children[2].id == "size-label" && grid.children[4].id == "custom" &&
              grid.children[4].classes.empty() && grid.children[5].id == "enabled" &&
              grid.children[6].classes == std::vector<std::string>{"hint"} && grid.children[7].id == "narrow",
          "PropertyGrid must compose captions and controls as its own children in row order");
    check(validate_sources(lowered.document, lowered.sources).empty() &&
              lowered.sources.nodes[2].property_arguments.at("label_for") == origin &&
              lowered.sources.nodes[1].invocation == origin,
          "PropertyGrid and its generated captions need their callers' source origins");
    // Preorder: root, grid, then caption and Button (with its label Text) per row, the custom caption's Label,
    // the wide help (12) and narrow (13) rows, and the help outside the grid last.
    const auto& grid_style = lowered.styles[1];
    check(grid_style.display == Display::grid &&
              grid_style.grid_columns == std::vector<GridTrack>{GridTrack{}, GridTrack::fraction(1)} &&
              grid_style.grid_rows == std::vector<GridTrack>(5) && grid_style.column_gap == 8.f &&
              grid_style.row_gap == 4.f,
          "PropertyGrid must generate its columns and one row per InspectorRow");
    check(lowered.styles.size() == 15 && lowered.styles[12].grid_column_span == std::uint16_t{2} &&
              lowered.styles[13].grid_column_span == std::uint16_t{1} && !lowered.styles[14].grid_column_span,
          "A wide row spans both columns unless it sets a span, without changing other occurrences");

    const auto tree = take(UiTree::create(lowered.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, lowered.styles}));
    PlaceholderTextShaper shaper;
    const auto layout = take(compute_layout({tree.get(), styles, {400, 400}, &shaper}));
    const auto box = [&](NodeHandle node) {
        return std::find_if(layout.boxes.begin(), layout.boxes.end(), [&](const auto& b) { return b.node == node; })
            ->border_box;
    };
    const auto by_id = [&](std::string_view id) { return box(*tree->find(id)); };
    const auto wide = box(tree->children(*tree->find("properties"))[6]);
    check(by_id("name").origin.x == by_id("size-x").origin.x && by_id("size-x").origin.x == by_id("enabled").origin.x &&
              by_id("name").origin.x == by_id("name-caption").size.width + 8 &&
              by_id("name-caption").size.width == 96 && by_id("size-label").size.width == 96 &&
              by_id("size-x").origin.y >= by_id("name").origin.y + by_id("name").size.height + 4 &&
              wide.origin.x == 0 && wide.size.width == 300 &&
              by_id("narrow").size.width == by_id("name-caption").size.width,
          "Rows must share the widest caption's column and a wide row must span both columns");
    const HitTestInput hit{tree.get(), styles, &layout};
    const auto at = take(hit_test(hit, {by_id("size-label").origin.x + 1, by_id("size-label").origin.y + 1})).target;
    check(at && take(label_target(*tree, *at)) == tree->find("size-x"),
          "A generated caption must resolve its control for label press focus");

    const auto empty = ui.lower(PropertyGrid({}, {.node = {.source = origin}}), actions);
    check(!empty && empty.diagnostics.size() == 1 && has(empty.diagnostics, "property_grid_rows", "/root") &&
              empty.diagnostics[0].source && empty.diagnostics[0].source->location == origin,
          "An empty PropertyGrid needs one source-located diagnostic");
    const auto blank = ui.lower(PropertyGrid({{"Name", "", Text("Value")}}), actions);
    check(!blank && blank.diagnostics.size() == 1 && has(blank.diagnostics, "label_target", "/root/children/0"),
          "An empty caption target reports label_target without a generated ID");
}

void separators() {
    Session ui;
    const auto form =
        Column({Text("Above", {.id = "above"}), HorizontalSeparator({.id = "rule", .source = origin}),
                Row({Button("Left", "toggle", {.node = {.id = "left"}}), VerticalSeparator({.id = "bar"}),
                     Button("Right", "toggle", {.node = {.id = "right"}})},
                    {.id = "tools", .style = {.gap = 4.f}}),
                HorizontalSeparator(
                    {.id = "thick", .style = {.height = Dimension::points(3), .background = Color{1, 0, 0, 1}}})},
               {.style = {.width = Dimension::points(200), .gap = 8.f}});
    const auto lowered = take(ui.lower(form, actions));
    const auto& rule = lowered.document.root.children[1];
    check(rule.kind == NodeKind::box && rule.children.empty() && rule.events.empty() &&
              rule.properties ==
                  std::map<std::string, Property, std::less<>>{{"pointer_events", false}, {"separator", true}},
          "A Separator must lower to an empty input-transparent Box with the separator marker alone");
    check(validate_sources(lowered.document, lowered.sources).empty() && lowered.sources.nodes[2].invocation == origin,
          "A Separator needs its caller's source origin");
    check(lowered.styles[2].height == Dimension::points(1) && !lowered.styles[2].width &&
              lowered.styles[2].shrink == 0.f && lowered.styles[2].background &&
              lowered.styles[6].width == Dimension::points(1) && !lowered.styles[6].height &&
              lowered.styles[9].height == Dimension::points(3) && lowered.styles[9].background == Color{1, 0, 0, 1},
          "Separators must default their thickness, shrink and color unless the caller sets them");

    const auto tree = take(UiTree::create(lowered.document, actions));
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, lowered.styles}));
    PlaceholderTextShaper shaper;
    const auto layout = take(compute_layout({tree.get(), styles, {400, 200}, &shaper}));
    const auto box = [&](std::string_view id) {
        const auto node = *tree->find(id);
        return std::find_if(layout.boxes.begin(), layout.boxes.end(), [&](const auto& b) { return b.node == node; })
            ->border_box;
    };
    check(box("rule").size == Size{200, 1} && box("thick").size == Size{200, 3} && box("bar").size.width == 1 &&
              box("bar").size.height == box("tools").size.height &&
              box("bar").origin.x == box("left").origin.x + box("left").size.width + 4 &&
              box("right").origin.x == box("bar").origin.x + 1 + 4,
          "A Separator must keep its thickness and span its parent's cross axis");
    const HitTestInput hit{tree.get(), styles, &layout};
    check(take(hit_test(hit, {box("rule").origin.x + 1, box("rule").origin.y})).target == tree->root() &&
              take(hit_test(hit, {box("bar").origin.x, box("bar").origin.y + 1})).target == tree->find("tools"),
          "A Separator must not intercept pointer hits");
    const auto semantics = build_semantic_tree({tree.get(), styles, &layout});
    check(semantics && semantics.diagnostics.empty(), "An unnamed static Separator must not warn");
    const auto entry = std::find_if(semantics.value->nodes.begin(), semantics.value->nodes.end(),
                                    [&](const auto& node) { return node.node == tree->find("bar"); });
    check(entry != semantics.value->nodes.end() && entry->role == SemanticRole::separator && entry->name.empty() &&
              entry->actions.empty() && !entry->focusable && semantics.value->nodes[entry->parent].node == tree->root(),
          "A Separator must project an unnamed separator entry without actions or focus");
}

void controlled_dropdown() {
    Session ui;
    const ValidationContext menu_actions{{"open", "system", "dark"}};
    const std::vector<DropdownOption> options{{"system", "System", "system"}, {"dark", "Dark", "dark", true}};
    const auto closed = take(ui.lower(
        Dropdown("Theme", "system", options, "open", {.node = {.id = "theme", .source = origin}}), menu_actions));
    const auto& trigger = closed.document.root.children[0];
    check(closed.document.root.children.size() == 1 && trigger.id == "theme-trigger" &&
              trigger.events.at("activate") == "open" &&
              trigger.children[0].properties.at("text") == Property{std::string("Theme: System [+]")},
          "Collapsed Dropdown must expose only its current value and trigger");
    const auto expanded = take(
        ui.lower(Dropdown("Theme", "system", options, "open",
                          {.node = {.id = "theme", .classes = {"control"}, .style = {.gap = 4.f}, .source = origin},
                           .expanded = true}),
                 menu_actions));
    check(expanded.document.root.children.size() == 3 && expanded.styles[0].gap == 4 &&
              expanded.styles[0].direction == FlexDirection::column &&
              expanded.document.root.children[1].id == "theme-option-system" &&
              expanded.document.root.children[1].children[0].properties.at("text") ==
                  Property{std::string("* System")} &&
              expanded.document.root.children[2].properties.at("disabled") == Property{true} &&
              expanded.document.root.children[2].events.at("activate") == "dark",
          "Expanded choices must retain stable keys, selection, disabled state and ordinary actions");
    check(validate_sources(expanded.document, expanded.sources).empty() &&
              expanded.sources.nodes.back().property_arguments.at("text") == origin,
          "Dropdown generated controls lost caller provenance");
    const auto disabled =
        take(ui.lower(Dropdown("Theme", "dark", options, "open", {.expanded = true, .disabled = true}), menu_actions));
    for (const auto& child : disabled.document.root.children)
        check(child.properties.at("disabled") == Property{true}, "Disabled Dropdown left an enabled control");
    check(disabled.document.root.children[0].children[0].properties.at("text") ==
                  Property{std::string("Theme: Dark [-]")} &&
              closed.document.root.children.size() == 1,
          "Controlled Dropdown replacement changed an earlier candidate");
    int reads = 0;
    const auto pending = ui.computed("dropdown-pending", [&] {
        ++reads;
        return std::string("Pending");
    });
    for (const auto& invalid_options :
         {std::vector<DropdownOption>{}, std::vector<DropdownOption>{{"", "Empty", "system"}},
          std::vector<DropdownOption>{{"system", "One", "system"}, {"system", "Two", "dark"}}}) {
        const auto invalid = ui.lower(
            Column({Text(pending), Dropdown("Theme", "system", invalid_options, "open", {.node = {.source = origin}})}),
            menu_actions);
        check(!invalid && reads == 0 && invalid.diagnostics[0].source &&
                  invalid.diagnostics[0].source->location == origin,
              "Invalid Dropdown options must reject before evaluating bindings and locate the caller");
    }
    const auto unknown = ui.lower(Dropdown("Theme", "missing", options, "open"), menu_actions);
    check(!unknown && has(unknown.diagnostics, "dropdown_selection", "/root"), "Unknown selected key accepted");
    auto unknown_action = options;
    unknown_action[1].action = "missing";
    check(!ui.lower(Dropdown("Theme", "system", unknown_action, "open", {.expanded = true}), menu_actions),
          "Unregistered expanded option action accepted");
    check(!ui.lower(Dropdown("Theme", "system", options, "missing"), menu_actions),
          "Unregistered Dropdown trigger accepted");
    auto deepest = Dropdown("Theme", "system", options, "open", {.expanded = true});
    for (std::size_t i = 3; i < max_document_depth; ++i) deepest = Column({deepest});
    check(bool(ui.lower(deepest, menu_actions)) && !ui.lower(Column({deepest}), menu_actions),
          "Dropdown generated controls bypassed document depth bounds");
}

void controlled_slider() {
    Session ui;
    const SliderRange range{-20, 80, 5};
    auto first =
        take(ui.lower(Slider("Volume", 30, range, "toggle", {.node = {.id = "volume", .source = origin}}), actions));
    const auto& root = first.document.root;
    check(root.events.at("activate") == "toggle" && root.properties.at("focusable") == Property{true} &&
              root.children[0].properties.at("text") == Property{std::string("Volume: 30")} &&
              root.children[1].id == "volume-track" && root.children[1].children[1].id == "volume-thumb" &&
              first.styles[3].width == Dimension::points(96),
          "Slider caption, identity or thumb position differs");
    check(validate_sources(first.document, first.sources).empty() &&
              first.sources.nodes.back().style_argument == origin,
          "Slider generated parts lost caller provenance");
    const auto disabled = take(ui.lower(Slider("Volume", 80, range, "toggle", {.disabled = true}), actions));
    check(disabled.document.root.properties.at("disabled") == Property{true} &&
              disabled.styles[3].width == Dimension::points(192) && first.styles[3].width == Dimension::points(96),
          "Slider disabled/max replacement changed earlier candidate");
    check(take(slider_value_at(range, -100)) == -20 && take(slider_value_at(range, 100)) == 80 &&
              take(slider_value_at(range, .525)) == 35 && take(slider_value_at(range, .524)) == 30,
          "Slider clamping or nearest-stop tie policy differs");
    const SliderRange wide{std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1};
    check(take(slider_value_at(wide, 0)) == wide.minimum && take(slider_value_at(wide, 1)) == wide.maximum,
          "Slider full integer range overflowed");
    for (double position : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        check(!slider_value_at(range, position), "Nonfinite Slider position accepted");
    int reads = 0;
    const auto pending = ui.computed("slider-pending", [&] {
        ++reads;
        return std::string("Pending");
    });
    for (SliderRange invalid : {SliderRange{1, 1, 1}, {2, 1, 1}, {0, 10, 0}, {0, 10, -1}, {0, 10, 3}}) {
        const auto rejected = ui.lower(
            Column({Text(pending), Slider("Volume", 0, invalid, "toggle", {.node = {.source = origin}})}), actions);
        check(!rejected && has(rejected.diagnostics, "slider_range", "/root/children/1") && reads == 0 &&
                  rejected.diagnostics[0].source->location == origin && !slider_value_at(invalid, .5),
              "Invalid Slider range must reject before evaluation with caller source");
    }
    for (int value : {-25, 85, 31}) {
        const auto rejected = ui.lower(Slider("Volume", value, range, "toggle", {.node = {.source = origin}}), actions);
        check(!rejected && has(rejected.diagnostics, "slider_value", "/root") &&
                  rejected.diagnostics[0].source->location == origin,
              "Invalid Slider value accepted or unlocated");
    }
    check(!ui.lower(Slider("Volume", 30, range, "unknown"), actions), "Unregistered Slider action accepted");
    auto deepest = Slider("Volume", 30, range, "toggle");
    for (std::size_t i = 3; i < max_document_depth; ++i) deepest = Column({deepest});
    check(bool(ui.lower(deepest, actions)) && !ui.lower(Column({deepest}), actions),
          "Slider generated parts bypassed depth bounds");
}

void controlled_checkbox() {
    Session ui;
    const auto checked = ui.state("checked", false);
    const auto disabled = ui.state("disabled", false);
    const auto label = ui.state("checkbox-label", std::string("Enable feature"));
    const auto form =
        Checkbox(label, checked, "toggle", {.node = {.id = "feature", .source = origin}, .disabled = disabled});
    const auto first = take(ui.lower(form, actions));
    check(first.document.root.children[0].properties.at("text") == Property{std::string("[ ]")},
          "Unchecked control must expose its value through ordinary Text");
    ui.update([&] {
        checked.write(true);
        disabled.write(true);
        label.write("Feature");
    });
    const auto second = take(ui.lower(form, actions));
    check(second.document.root.children[0].properties.at("text") == Property{std::string("[x]")} &&
              second.document.root.children[1].properties.at("text") == Property{std::string("Feature")} &&
              second.document.root.properties.at("disabled") == Property{true},
          "Checked, label and disabled bindings must settle in one candidate");
    check(first.document.root.children[0].properties.at("text") == Property{std::string("[ ]")},
          "Controlled value update mutated the accepted snapshot");
    check(validate_sources(second.document, second.sources).empty() &&
              second.sources.nodes[1].property_arguments.at("text") == origin,
          "Generated checked marker needs its caller's source origin");
    Session foreign;
    const auto wrong = foreign.state("checked", true);
    auto rejected =
        ui.lower(Checkbox("Foreign", wrong, "toggle", {.node = {.id = "foreign", .source = origin}}), actions);
    check(!rejected && rejected.diagnostics[0].source && rejected.diagnostics[0].path == "/root/properties/selected" &&
              rejected.diagnostics[0].source->location == origin,
          "Foreign checked observation needs a located rejection");
    int reads = 0;
    const auto pending = ui.computed("pending-check", [&] {
        ++reads;
        return true;
    });
    check(!ui.lower(Checkbox(std::string("\xff"), pending, "toggle", {.node = {.id = "invalid"}}), actions) &&
              reads == 0,
          "Invalid Checkbox label must reject before checked evaluation");
    check(!ui.lower(Checkbox("Unknown", true, "unknown", {.node = {.id = "unknown", .source = origin}}), actions),
          "Unregistered Checkbox command accepted");
    auto deepest = Checkbox("Bounded", true, "toggle", {.node = {.id = "bounded"}});
    for (std::size_t i = 2; i < max_document_depth; ++i) deepest = Column({deepest});
    check(bool(ui.lower(deepest, actions)) && !ui.lower(Column({deepest}), actions),
          "Generated Checkbox children must count against depth bounds");
}

void lifetimes_and_recovery() {
    Signal<std::string> expired;
    std::optional<Element> description;
    {
        Session ui;
        expired = ui.state("label", std::string("alive"));
        description = Text(expired, {.source = origin});
        (void)take(ui.lower(*description));
    }
    bool stale = false;
    try {
        expired.write("late");
    } catch (const ReactiveError&) {
        stale = true;
    }
    check(stale, "A description must not retain its session lifetime");
    Session other;
    auto rejected = other.lower(*description);
    check(!rejected && rejected.diagnostics[0].path == "/root/properties/text" &&
              rejected.diagnostics[0].source->location == origin,
          "Expired binding needs its authoring location");
    Session live;
    auto foreign = live.state("label", std::string("foreign"));
    check(!other.lower(Text(foreign)), "Foreign-runtime binding accepted");
    auto own = other.state("label", std::string("own"));
    other.update([&] {
        check(!other.lower(Text(own)), "Lowering inside a batch accepted");
        own.write("updated");
    });
    check(take(other.lower(Text(own))).document.root.properties.at("text") == Property{std::string("updated")},
          "Rejected batch entry did not recover");
    bool fail = true;
    auto derived = other.computed("retry", [&] {
        if (fail) throw std::runtime_error("fixture calculation failed");
        return own.read();
    });
    check(!other.lower(Text(derived)), "Calculation failure accepted");
    fail = false;
    (void)take(other.lower(Text(derived)));
    other.publish();
}

void controlled_text_input() {
    Session ui;
    const auto initial = take(make_text_edit_state(1, "日😀"));
    const auto state = ui.state("editing", initial);
    const auto disabled = ui.state("disabled", false);
    const auto field = tessera::authoring::TextInput(
        "Username", state, "toggle", {.node = {.id = "username", .source = origin}, .disabled = disabled});
    const auto first = take(ui.lower(field, actions));
    check(first.document.root.children.size() == 3 && first.styles[0].direction == FlexDirection::column &&
              first.document.root.properties.at("edit_role") == Property{std::string("text_input")} &&
              first.document.root.properties.at("edit_value") == Property{std::string("日😀")} &&
              first.document.root.properties.at("labelled_by") == Property{NodeReference{"username-label"}} &&
              first.document.root.children[0].id == "username-label" &&
              first.document.root.children[1].id == "username-value" &&
              first.document.root.children[1].properties.at("text") == Property{std::string("日😀")} &&
              first.document.root.children[2].properties.at("text") == Property{std::string("Selection 7:7")},
          "Field must compose label, value and byte selection through ordinary Text");
    auto next = take(edit_text(initial, {1, 1, TextEditMove{TextMove::left, true}}));
    ui.update([&] {
        state.write(next);
        disabled.write(true);
    });
    const auto second = take(ui.lower(field, actions));
    check(second.document.root.properties.at("disabled") == Property{true} &&
              second.document.root.children[2].properties.at("text") == Property{std::string("Selection 7:3")} &&
              first.document.root.children[2].properties.at("text") == Property{std::string("Selection 7:7")},
          "Selection/disabled must settle coherently without mutating owned candidates");
    check(second.sources.nodes[0].property_arguments.at("edit_value") == origin &&
              second.sources.nodes[2].property_arguments.at("text") == origin &&
              second.sources.nodes[3].property_arguments.at("text") == origin,
          "Field projections lost origins");
    auto composing = take(edit_text(next, {1, next.revision, TextCompositionStart{}}));
    composing =
        take(edit_text(composing, {1, composing.revision, TextCompositionUpdate{composing.composition->id, "ご", 3}}));
    ui.update([&] { state.write(composing); });
    const auto composed = take(ui.lower(field, actions));
    check(composed.document.root.children[1].properties.at("text") == Property{std::string("日ご")} &&
              composed.document.root.properties.at("edit_value") == Property{std::string("日ご")} &&
              composed.document.root.children[2].properties.at("text") ==
                  Property{std::string("Composition 3:6 caret 6")},
          "Open composition must present preedit over the captured selection");
    next.caret = 4;
    ui.update([&] { state.write(next); });
    const auto invalid = ui.lower(field, actions);
    check(!invalid && has(invalid.diagnostics, "invalid_selection", "/root/children/1/properties/text") &&
              invalid.diagnostics[0].source && invalid.diagnostics[0].source->location == origin,
          "Malformed edit state needs a located field diagnostic");
    Session foreign;
    const auto wrong = foreign.state("editing", initial);
    const auto rejected = ui.lower(
        tessera::authoring::TextInput("Foreign", wrong, "toggle", {.node = {.id = "foreign", .source = origin}}),
        actions);
    check(!rejected && rejected.diagnostics[0].path == "/root/children/1/properties/text" &&
              rejected.diagnostics[0].source,
          "Foreign editing state accepted or unlocated");
    int reads = 0;
    const auto label = ui.computed("pending", [&] {
        ++reads;
        return std::string("Username");
    });
    auto bad = initial;
    bad.text = "multiline\n";
    check(!ui.lower(tessera::authoring::TextInput(label, bad, "toggle", {.node = {.id = "bad"}}), actions) &&
              reads == 0,
          "Invalid literal edit state must fail before graph evaluation");
    check(
        !ui.lower(tessera::authoring::TextInput("Username", initial, "unknown", {.node = {.id = "username"}}), actions),
        "Unknown TextInput action accepted");
    check(has(ui.lower(tessera::authoring::TextInput("Username", initial, "toggle"), actions).diagnostics,
              "text_input_id", "/root"),
          "TextInput requires a stable ID");
    auto deepest = tessera::authoring::TextInput("Username", initial, "toggle", {.node = {.id = "deepest"}});
    for (std::size_t i = 2; i < max_document_depth; ++i) deepest = Column({deepest});
    check(bool(ui.lower(deepest, actions)) && !ui.lower(Column({deepest}), actions),
          "TextInput generated children bypassed expanded bounds");
}

void bounded_descriptions() {
    Session ui;
    Element nested = Text("deep");
    for (std::size_t i = 1; i < max_document_depth; ++i) nested = Column({nested});
    check(bool(ui.lower(nested)), "Exact depth bound rejected");
    check(!ui.lower(Column({nested})), "Oversized depth accepted");
    const auto leaf = Text("shared");
    std::vector<Element> children(max_document_nodes - 1, leaf);
    check(bool(ui.lower(Column(children))), "Exact node bound rejected");
    children.push_back(leaf);
    check(!ui.lower(Column(std::move(children))), "Shared descriptions bypassed expanded node bound");
    auto moved = Text("moved");
    const auto retained = std::move(moved);
    check(!ui.lower(moved) && bool(ui.lower(retained)), "Moved-from description handling differs");
}
} // namespace

int main() {
    try {
        composition_and_updates();
        rejection_and_preflight();
        controlled_checkbox();
        controlled_text_input();
        scroll_view_contract();
        controlled_dropdown();
        controlled_slider();
        lifetimes_and_recovery();
        bounded_descriptions();
        stack_contract();
        grid_contract();
        label_and_spacer();
        property_grid();
        separators();
        controlled_dropdown();
        controlled_slider();
        lifetimes_and_recovery();
        bounded_descriptions();
        std::cout << "Authoring composition, state, diagnostics and lifetime checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
