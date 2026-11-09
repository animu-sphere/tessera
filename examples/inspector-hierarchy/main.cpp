#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/input/split_pane.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include "cell_text.hpp"
#ifdef _WIN32
#include "../settings-starter/native_win32.hpp"
#include "clipboard_win32.hpp"
#endif

using namespace tessera;
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
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr ui::SliderRange amount_range{0, 100, 5};
constexpr ui::NumberRange scale_range{0, 10, 0.1};
// Draft owners stay distinct from every item's name owner.
constexpr std::uint64_t scale_owner_base = 1000;
constexpr ui::SplitPaneLimits pane_limits{140, 320, 8};
const std::vector<std::pair<std::string, std::string>> visibility_choices{
    {"inherited", "Inherited"}, {"visible", "Visible"}, {"hidden", "Hidden"}};
const std::vector<std::pair<std::string, std::string>> sections{{"general", "General"}, {"display", "Display"}};
struct Item {
    std::string id;
    TextEditState name;
    bool enabled = true;
    int amount = 50;
    double scale = 1;          // Accepted number.
    TextEditState scale_draft; // Pending NumberInput text; Enter applies, Escape reverts.
    std::string visibility = "inherited";
    bool shadows = true;
    bool operator==(const Item&) const = default;
};
// True when Reset would change a property; the display name is not reset.
bool modified(const Item& item) {
    return !item.enabled || item.amount != 50 || item.scale != 1 || item.scale_draft.text != ui::format_number(1) ||
           item.visibility != "inherited" || !item.shadows;
}
enum class OpenMenu { none, actions, context };
struct State {
    std::vector<Item> items;
    std::string selected = "cube";
    bool expanded = true;
    std::string section = "general"; // Shown Inspector tab; shared by every item.
    float split = 220;
    OpenMenu menu = OpenMenu::none;
    Point position;
    const Item& item() const {
        const auto found =
            std::find_if(items.begin(), items.end(), [&](const Item& item) { return item.id == selected; });
        require(found != items.end(), "Selected Inspector item is missing");
        return *found;
    }
    Item& item() {
        const auto found =
            std::find_if(items.begin(), items.end(), [&](const Item& item) { return item.id == selected; });
        require(found != items.end(), "Selected Inspector item is missing");
        return *found;
    }
};
State initial_state() {
    State state;
    for (const auto& [id, label] : std::vector<std::pair<std::string, std::string>>{
             {"scene", "Scene"}, {"cube", "Cube"}, {"sphere", "Sphere"}, {"light", "Light"}, {"camera", "Camera"}})
        state.items.push_back({id, take(make_text_edit_state(state.items.size() + 1, label))});
    for (int i = 0; i < 12; ++i)
        state.items.push_back({"extra" + std::to_string(i),
                               take(make_text_edit_state(state.items.size() + 1, "Extra " + std::to_string(i)))});
    for (std::size_t i = 0; i < state.items.size(); ++i)
        state.items[i].scale_draft = take(make_text_edit_state(scale_owner_base + i, ui::format_number(1)));
    return state;
}
ValidationContext action_context(const State& state) {
    ValidationContext context{{"scene.toggle", "item.name", "item.enabled", "item.amount", "item.scale", "item.shadows",
                               "item.rename", "item.reset", "menu.open", "panes.resize"}};
    for (const auto& item : state.items) context.actions.insert("select." + item.id);
    for (const auto& [key, label] : visibility_choices) context.actions.insert("visibility." + key);
    for (const auto& [key, label] : sections) context.actions.insert("section." + key);
    return context;
}
CommandRegistry command_registry(const ValidationContext& context) {
    std::vector<CommandDescriptor> descriptors;
    CommandBindings bindings;
    for (const auto& action : context.actions) {
        descriptors.push_back({action, action});
        bindings.emplace(action, action);
    }
    return take(CommandRegistry::create(std::move(descriptors), std::move(bindings)));
}
CommandStates command_states(const ValidationContext& context) {
    CommandStates states;
    for (const auto& action : context.actions) states.emplace(action, CommandState{true});
    return states;
}
StyleSheet appearance(const State& state) {
    StyleSheet sheet{
        {{StyleSelector::of_class("control"),
          {.min_height = 32.f,
           .border = Edges{1, 1, 1, 1},
           .padding = Edges{6, 10, 6, 10},
           .background = Color{1, 1, 1, 1},
           .border_color = Color{.65f, .72f, .83f, 1},
           .corner_radius = 6.f}},
         {StyleSelector::of_class("control", {.hover = true}), {.background = Color{.9f, .94f, 1, 1}}},
         {StyleSelector::of_class("control", {.active = true}), {.background = Color{.8f, .87f, .98f, 1}}},
         {StyleSelector::of_class("control", {.focus = true}),
          {.border_color = Color{.2f, .4f, .85f, 1}, .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}},
         {StyleSelector::of_class("control", {.disabled = true}),
          {.background = Color{.94f, .95f, .97f, 1}, .color = Color{.45f, .49f, .56f, 1}}},
         {StyleSelector::of_class("section-title"), {.text = {.size = 14.f}, .color = Color{.32f, .4f, .54f, 1}}},
         {StyleSelector::of_class("muted"), {.text = {.size = 12.f}, .color = Color{.32f, .4f, .54f, 1}}},
         {StyleSelector::of_id("name"), {.box_shadow = BoxShadow{{0, 1}, 4, {.18f, .22f, .38f, .2f}, true}}},
         {StyleSelector::of_id("actions"), {.background = Color{.2f, .34f, .67f, 1}, .color = Color{1, 1, 1, 1}}},
         {StyleSelector::of_id("actions", {.hover = true}), {.background = Color{.16f, .28f, .56f, 1}}},
         {StyleSelector::of_id("actions", {.active = true}), {.background = Color{.12f, .22f, .46f, 1}}},
         {StyleSelector::of_id("actions", {.focus = true}),
          {.border_color = Color{.55f, .72f, 1, 1}, .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}},
         {StyleSelector::of_id("enabled"),
          {.background = state.item().enabled ? Color{.88f, .94f, 1, 1} : Color{1, 1, 1, 1}}},
         {StyleSelector::of_id("hierarchy-title"), {.text = {.size = 14.f}, .color = Color{.32f, .4f, .54f, 1}}},
         {StyleSelector::of_id("amount-track"), {.corner_radius = 4.f}},
         {StyleSelector::of_id("scale-decrease"),
          {.height = Dimension::points(20), .background = Color{.9f, .94f, 1, 1}, .corner_radius = 4.f}},
         {StyleSelector::of_id("scale-increase"),
          {.height = Dimension::points(20), .background = Color{.9f, .94f, 1, 1}, .corner_radius = 4.f}},
         {StyleSelector::of_id("amount-fill"), {.corner_radius = 4.f}},
         {StyleSelector::of_id("amount-thumb"), {.corner_radius = 4.f}},
         {StyleSelector::of_id("panes-handle"),
          {.border = Edges{0, 2, 0, 2}, .border_color = Color{.95f, .95f, .98f, 1}}},
         {StyleSelector::of_id("panes-handle", {.hover = true}), {.border_color = Color{.65f, .75f, .94f, 1}}},
         {StyleSelector::of_id("panes-handle", {.active = true}), {.border_color = Color{.2f, .4f, .85f, 1}}},
         {StyleSelector::of_id("panes-handle", {.focus = true}), {.border_color = Color{.2f, .4f, .85f, 1}}},
         {StyleSelector::of_class("popup"),
          {.border = Edges{1, 1, 1, 1},
           .padding = Edges{8, 8, 8, 8},
           .gap = 4.f,
           .background = Color{1, 1, 1, 1},
           .border_color = Color{.65f, .72f, .83f, 1},
           .corner_radius = 8.f,
           .box_shadow = BoxShadow{{0, 3}, 6, {.18f, .22f, .38f, .18f}}}}}};
    auto row = [&](std::string id) {
        sheet.rules.push_back({StyleSelector::of_id(id),
                               {.align = Align::center,
                                .min_height = 30.f,
                                .border = Edges{1, 1, 1, 1},
                                .border_color = Color{0, 0, 0, 0},
                                .corner_radius = 6.f}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.hover = true}), {.background = Color{.9f, .94f, 1, 1}}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.active = true}), {.background = Color{.8f, .87f, .98f, 1}}});
        sheet.rules.push_back(
            {StyleSelector::of_id(std::move(id), {.focus = true}),
             {.border_color = Color{.2f, .4f, .85f, 1}, .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}, 4.f, 2.f}}});
    };
    for (const auto& item : state.items) row("hierarchy-item-" + item.id);
    const auto selected = "hierarchy-item-" + state.selected;
    sheet.rules.push_back(
        {StyleSelector::of_id(selected), {.background = Color{.2f, .34f, .67f, 1}, .color = Color{1, 1, 1, 1}}});
    sheet.rules.push_back(
        {StyleSelector::of_id(selected, {.hover = true}), {.background = Color{.16f, .28f, .56f, 1}}});
    sheet.rules.push_back(
        {StyleSelector::of_id(selected, {.active = true}), {.background = Color{.12f, .22f, .46f, 1}}});
    sheet.rules.push_back({StyleSelector::of_id(selected, {.focus = true}), {.border_color = Color{.55f, .72f, 1, 1}}});
    sheet.rules.push_back({StyleSelector::of_id("visibility-title"), {.display = Display::none}});
    for (const auto& [key, label] : visibility_choices) {
        const auto id = "visibility-option-" + key;
        sheet.rules.push_back({StyleSelector::of_id(id),
                               {.align = Align::center,
                                .min_height = 26.f,
                                .border = Edges{1, 1, 1, 1},
                                .padding = Edges{2, 8, 2, 6},
                                .border_color = Color{0, 0, 0, 0},
                                .corner_radius = 6.f}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.hover = true}), {.background = Color{.9f, .94f, 1, 1}}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.active = true}), {.background = Color{.8f, .87f, .98f, 1}}});
        sheet.rules.push_back(
            {StyleSelector::of_id(id, {.focus = true}),
             {.border_color = Color{.2f, .4f, .85f, 1}, .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}});
    }
    sheet.rules.push_back(
        {StyleSelector::of_id("visibility-option-" + state.item().visibility), {.color = Color{.12f, .26f, .6f, 1}}});
    // Tabs carry their own bottom indicator; the application adds feedback that leaves it intact.
    for (const auto& [key, label] : sections) {
        const auto id = "sections-tab-" + key;
        sheet.rules.push_back({StyleSelector::of_id(id), {.color = Color{.32f, .4f, .54f, 1}}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.hover = true}), {.background = Color{.9f, .94f, 1, 1}}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.active = true}), {.background = Color{.8f, .87f, .98f, 1}}});
        sheet.rules.push_back(
            {StyleSelector::of_id(id, {.focus = true}), {.outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}});
    }
    sheet.rules.push_back(
        {StyleSelector::of_id("sections-tab-" + state.section), {.color = Color{.12f, .26f, .6f, 1}}});
    for (const auto* menu : {"actions-menu", "item-menu"}) {
        sheet.rules.push_back({StyleSelector::of_id(std::string(menu) + "-title"),
                               {.text = {.size = 12.f}, .color = Color{.32f, .4f, .54f, 1}}});
        for (const auto* command : {"rename", "enabled"}) row(std::string(menu) + "-item-" + command);
    }
    return sheet;
}
ui::Element Form(const State& state, Size viewport) {
    const float height = std::max(100.f, viewport.height - 72);
    std::vector<ui::TreeItem> children;
    for (std::size_t i = 1; i < state.items.size(); ++i) {
        const auto& item = state.items[i];
        children.push_back({item.id, item.name.text, "select." + item.id});
    }
    const auto& item = state.item();
    std::vector<ui::RadioOption> visibility;
    for (const auto& [key, label] : visibility_choices) visibility.push_back({key, label, "visibility." + key});
    const auto control = [](const char* id) { return ui::Props{.id = id, .classes = {"control"}}; };
    const std::vector<ui::MenuItem> commands{{"rename", "Edit display name", "item.rename"},
                                             {"enabled", "Toggle enabled", "item.enabled"}};
    const auto popup = [&](const char* id, OpenMenu kind) {
        return ui::PopupProps{.node = {.id = id, .classes = {"popup"}, .style = {.width = Dimension::points(220)}},
                              .open = state.menu == kind};
    };
    std::vector<ui::Element> properties;
    std::vector<GridTrack> rows;
    if (state.section == "general") {
        properties = {
            ui::Text("Display name", {.classes = {"muted"}}),
            ui::TextInput("Name", item.name, "item.name", {.node = control("name")}),
            ui::Row({ui::Column({}, {.id = "enabled-indicator",
                                     .style = {.width = Dimension::points(8),
                                               .height = Dimension::points(8),
                                               .background =
                                                   item.enabled ? Color{.15f, .6f, .4f, 1} : Color{.55f, .6f, .68f, 1},
                                               .background_shape = FillShape::ellipse,
                                               .background_antialias = true}}),
                     ui::Text("Enabled", {.classes = {"muted"}})},
                    {.style = {.align = Align::center, .gap = 4.f}}),
            ui::Checkbox("Enabled", item.enabled, "item.enabled", {.node = control("enabled")}),
            ui::Text("Amount", {.classes = {"muted"}}),
            ui::Slider("Amount", item.amount, amount_range, "item.amount",
                       {.node = {.id = "amount", .classes = {"control"}, .style = {.padding = Edges{6, 0, 6, 0}}}}),
            ui::Text(item.scale_draft.text == ui::format_number(item.scale) ? "Scale" : "Scale *",
                     {.id = "scale-caption", .classes = {"muted"}}),
            ui::NumberInput("Scale", item.scale_draft, "item.scale", {.node = control("scale")}),
            // Contextual help spans the label and control columns below Scale.
            ui::Text("Enter applies, Esc reverts, Up/Down step.",
                     {.id = "scale-hint", .classes = {"muted"}, .style = {.grid_column_span = std::uint16_t{2}}})};
        rows.resize(5);
    } else {
        properties = {ui::Text("Visibility", {.classes = {"muted"}}),
                      ui::RadioGroup("Visibility", item.visibility, std::move(visibility),
                                     {.node = {.id = "visibility", .style = {.gap = 2.f}}}),
                      ui::Text("Shadows", {.classes = {"muted"}}),
                      ui::Switch("Cast shadows", item.shadows, "item.shadows", {.node = control("shadows")})};
        rows.resize(2);
    }
    std::vector<ui::TabOption> tabs;
    for (const auto& [key, label] : sections) tabs.push_back({key, label, "section." + key});
    std::vector<ui::Element> card{
        ui::Button("Selected: " + item.id, "select." + item.id, {.node = control("selected")}),
        ui::Tabs("Property sections", state.section, std::move(tabs),
                 {ui::Grid(std::move(properties),
                           {.id = "properties",
                            .style = {.grid_columns =
                                          std::vector<GridTrack>{GridTrack{.maximum = 72}, GridTrack::fraction(1, 202)},
                                      .grid_rows = std::move(rows),
                                      .column_gap = 8.f,
                                      .row_gap = 12.f}})},
                 {.node = {.id = "sections", .style = {.gap = 12.f}}})};
    // Straddles the card's top edge without moving the card's flow content; painted last, above it.
    if (modified(item))
        card.push_back(ui::Text("Modified", {.id = "modified-badge",
                                             .style = {.padding = Edges{1, 8, 1, 8},
                                                       .background = Color{.2f, .4f, .85f, 1},
                                                       .corner_radius = 9.f,
                                                       .text = {.size = 11.f, .line_height = 16.f},
                                                       .color = Color{1, 1, 1, 1},
                                                       .position = Position::absolute,
                                                       .top = std::optional<float>{-9.f},
                                                       .right = std::optional<float>{12.f}}}));
    return ui::Column(
        {ui::Row({ui::Text("Hierarchy / Inspector", {.style = {.text = {.size = 20.f, .line_height = 24.f}}}),
                  ui::Toolbar("Item tools",
                              {ui::Button("Item actions", "menu.open", {.node = control("actions")}),
                               ui::Button("Rename", "item.rename", {.node = control("tool-rename")}),
                               ui::Button("Reset", "item.reset", {.node = control("tool-reset")})},
                              {.id = "item-tools"})},
                 {.style = {.align = Align::center, .gap = 16.f}}),
         ui::HorizontalSplitPane(
             ui::TreeView("Hierarchy", state.selected,
                          {{"scene", state.items.front().name.text, "select.scene", "scene.toggle", state.expanded,
                            false, std::move(children)}},
                          {.node = {.id = "hierarchy",
                                    .style = {.height = Dimension::points(height),
                                              .border = Edges{1, 1, 1, 1},
                                              .padding = Edges{8, 8, 8, 8},
                                              .gap = 4.f,
                                              .background = Color{1, 1, 1, 1},
                                              .border_color = Color{.8f, .85f, .93f, 1},
                                              .corner_radius = 10.f}}}),
             ui::ScrollView(
                 {ui::Text("Inspector", {.id = "inspector-title", .classes = {"section-title"}}),
                  ui::Column(std::move(card), {.id = "item-card",
                                               .style = {.border = Edges{1, 1, 1, 1},
                                                         .padding = Edges{12, 12, 12, 12},
                                                         .gap = 12.f,
                                                         .border_color = Color{.72f, .8f, .92f, 1},
                                                         .corner_radius = 12.f,
                                                         .background_gradient =
                                                             RadialGradient{.center = {.25f, .2f},
                                                                            .radius = {.85f, .95f},
                                                                            .stops = {{0, {.96f, .99f, 1, 1}},
                                                                                      {.42f, {.84f, .92f, 1, 1}},
                                                                                      {1, {.98f, .9f, .98f, 1}}}},
                                                         .box_shadow = BoxShadow{{0, 4}, 8, {.18f, .22f, .38f, .18f}},
                                                         .background_antialias = true}}),
                  ui::Column({ui::Text("Edits are kept per item.", {.classes = {"muted"}}),
                              ui::Text("Ctrl+A/C/X/V: select/copy/cut/paste.", {.classes = {"muted"}}),
                              ui::Text("Arrows, Home/End, Backspace/Delete.", {.classes = {"muted"}})},
                             {.id = "help", .style = {.gap = 4.f}})},
                 {.id = "inspector",
                  .style = {.height = Dimension::points(height), .padding = Edges{0, 6, 0, 6}, .gap = 12.f}}),
             state.split, "panes.resize",
             {.node = {.id = "panes", .style = {.height = Dimension::points(height)}},
              .limits = pane_limits,
              .label = "Resize hierarchy"}),
         ui::Menu("Item actions", "actions", commands, popup("actions-menu", OpenMenu::actions)),
         ui::ContextMenu("Item actions", "selected", state.position, commands, popup("item-menu", OpenMenu::context))},
        {.id = "application",
         .style = {.overflow = Overflow::clip,
                   .height = Dimension::points(viewport.height),
                   .padding = Edges{12, 12, 12, 12},
                   .gap = 16.f,
                   .background = Color{.95f, .95f, .98f, 1},
                   .text = {.size = 14.f},
                   .color = Color{.16f, .22f, .34f, 1}}});
}
using Offsets = std::map<std::string, Point, std::less<>>;
struct View {
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
    StyleSheet sheet;
    std::vector<StyleDeclarations> overrides;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
    const LayoutBox& box(std::string_view id) const {
        const auto node = tree->find(id);
        const auto found = std::find_if(layout.boxes.begin(), layout.boxes.end(),
                                        [&](const LayoutBox& box) { return node == box.node; });
        require(found != layout.boxes.end(), "Inspector control has no layout");
        return *found;
    }
    Point center(std::string_view id) const {
        const auto rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
    bool contains(NodeHandle owner, NodeHandle node) const {
        if (owner == node) return true;
        for (const auto child : tree->children(owner))
            if (contains(child, node)) return true;
        return false;
    }
    // Center of a control that a pointer press can actually reach; scrolled-out or covered targets fail.
    Point reachable(std::string_view id) const {
        const auto point = center(id);
        const auto target = take(hit_test(hit(), point)).target;
        require(target && contains(*tree->find(id), *target), "Pointer target is clipped or covered");
        return point;
    }
};
struct Visuals {
    std::vector<ResolvedStyle> styles;
    InspectionSnapshot inspection;
    UiDrawList paint;
};
Result<Visuals> prepare_visuals(const View& view, const InteractionState& state, TextShaper& text) {
    auto styles = resolve_styles({view.tree.get(), &view.sheet, state, view.overrides});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    auto paint = build_paint_list({view.tree.get(), *styles.value, &view.layout, &text});
    if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
    auto inspection = capture_inspection({view.tree.get(), *styles.value, &view.layout, nullptr, state.focused});
    if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
    return {Visuals{std::move(*styles.value), std::move(*inspection.value), std::move(*paint.value)}, {}};
}
void accept_visuals(View& view, Visuals visuals) {
    view.styles = std::move(visuals.styles);
    view.inspection = std::move(visuals.inspection);
    view.paint = std::move(visuals.paint);
}
struct TextService final : TextShaper {
    inspector_text::CellTextShaper delegate;
    bool reject = false;
    Result<TextMetrics> measure(std::string_view text, const TextStyle& style,
                                const TextConstraints& constraints) override {
        if (reject)
            return {std::nullopt,
                    {{"inspector_measurement", Severity::error, "/text", "Injected measurement rejection"}}};
        return delegate.measure(text, style, constraints);
    }
    Result<GlyphRun> shape(std::string_view text, const TextStyle& style, const TextConstraints& constraints) override {
        return delegate.shape(text, style, constraints);
    }
};
// One host-normalized IME step for the focused name editor; UTF-8 bytes only.
struct ImeStep {
    enum class Kind : std::uint8_t { start, update, commit, cancel } kind = Kind::start;
    std::string text;      // Update preedit or commit result.
    std::size_t caret = 0; // Byte offset within an update's preedit.
};
// Host requests for the Scale NumberInput, all executed through the one item.scale command.
struct ScaleApply {};  // Enter: parse the draft and accept it.
struct ScaleRevert {}; // Escape: restore the canonical accepted text.
struct ScaleStep {     // Up/Down or a step part: move from the parsed draft and accept.
    int steps = 0;
};
using ScaleRequest = std::variant<TextEditRequest, ScaleApply, ScaleRevert, ScaleStep>;
// Edits change only the draft. Apply/step accept a valid draft and replace it with canonical
// text (caret at the end, next revision); an invalid draft rejects them and stays as typed.
std::vector<Diagnostic> apply_scale(Item& item, const ScaleRequest& request) {
    auto& draft = item.scale_draft;
    if (const auto* edit = std::get_if<TextEditRequest>(&request)) {
        auto next = edit_text(draft, *edit);
        if (!next) return std::move(next.diagnostics);
        draft = std::move(*next.value);
        return {};
    }
    if (!std::holds_alternative<ScaleRevert>(request)) {
        auto parsed = ui::parse_number(scale_range, draft.text);
        if (!parsed) return std::move(parsed.diagnostics);
        item.scale = *parsed.value;
        if (const auto* step = std::get_if<ScaleStep>(&request))
            item.scale = take(ui::step_number(scale_range, item.scale, step->steps));
    }
    draft.text = ui::format_number(item.scale);
    draft.anchor = draft.caret = draft.text.size();
    ++draft.revision;
    return {};
}
class App final : public CommandHost {
public:
    State state = initial_state();
    ValidationContext context = action_context(state);

private:
    CommandRegistry registry = command_registry(context);
    CommandStates eligibility = command_states(context);

public:
    Size viewport{960, 360};
    View view{take(CommandSnapshot::publish(registry, 1, CommandMode::production, eligibility))};
    FocusDispatcher focus;
    TextService text;
    std::uint64_t executions = 0;
    // Synchronous host services, called after dispatch and outside core traversal.
    std::function<Result<std::string>()> clipboard_read;
    std::function<std::vector<Diagnostic>(std::string_view)> clipboard_write;
    std::vector<Diagnostic> clipboard_diagnostics;
    std::vector<Diagnostic> edit_diagnostics;
    // Host feedback must not depend on successfully laying out the rejected edit.
    std::string feedback;
    // Optional host resource preparation, before a command publishes its candidate.
    std::function<std::vector<Diagnostic>(const UiDrawList&)> prepare_paint;

    App() { refresh(); }
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        State candidate = state;
        const auto& command = invocation.command;
        if (command.starts_with("select."))
            candidate.selected = command.substr(7);
        else if (command == "scene.toggle")
            candidate.expanded = !candidate.expanded;
        else if (command == "item.name") {
            require(edit.has_value(), "Name editing needs an owned request");
            auto next = edit_text(candidate.item().name, *edit);
            if (!next) return {CommandOutcomeStatus::error, {}, std::move(next.diagnostics)};
            candidate.item().name = std::move(*next.value);
        } else if (command == "item.enabled")
            candidate.item().enabled = !candidate.item().enabled;
        else if (command.starts_with("visibility."))
            candidate.item().visibility = command.substr(11);
        else if (command.starts_with("section."))
            candidate.section = command.substr(8);
        else if (command == "item.reset") {
            // Restore every property except the display name; a pending Scale draft is discarded.
            auto& item = candidate.item();
            item.enabled = true;
            item.amount = 50;
            item.scale = 1;
            item.scale_draft.text = ui::format_number(1);
            item.scale_draft.anchor = item.scale_draft.caret = item.scale_draft.text.size();
            ++item.scale_draft.revision;
            item.visibility = "inherited";
            item.shadows = true;
        } else if (command == "item.shadows")
            candidate.item().shadows = !candidate.item().shadows;
        else if (command == "item.scale") {
            require(scale_request.has_value(), "Scale input needs an owned request");
            auto rejected = apply_scale(candidate.item(), *scale_request);
            if (!rejected.empty()) return {CommandOutcomeStatus::error, {}, std::move(rejected)};
        } else if (command == "item.amount") {
            candidate.item().amount = amount.value_or(std::min(100, candidate.item().amount + amount_range.step));
        } else if (command == "panes.resize") {
            require(split.has_value(), "Pane resize needs a size");
            candidate.split = *split;
        } else if (command == "menu.open")
            candidate.menu = OpenMenu::actions;
        else if (command != "item.rename")
            throw std::runtime_error("Unknown Inspector command");
        if (command == "item.rename" || command == "item.enabled") candidate.menu = OpenMenu::none;
        // The name editor lives on the General tab; accepting Rename focuses it in the new view.
        if (command == "item.rename") candidate.section = "general";
        auto candidate_focus = focus;
        if (const auto name = view.tree->find("name"); command == "item.rename" && name)
            (void)take(candidate_focus.focus(view.hit(), *name));
        auto prepared = stage(candidate, viewport, offsets, candidate_focus, true);
        if (prepared && prepare_paint) {
            auto diagnostics = prepare_paint(prepared.value->paint);
            if (!diagnostics.empty()) prepared = {std::nullopt, std::move(diagnostics)};
        }
        if (!prepared) {
            if (prepared.diagnostics.size() > max_command_result_diagnostics) {
                prepared.diagnostics.resize(max_command_result_diagnostics);
                prepared.diagnostics.back().message += " Further presentation diagnostics omitted.";
            }
            return {CommandOutcomeStatus::error, {}, std::move(prepared.diagnostics)};
        }
        pending_state = std::move(candidate);
        pending_focus = std::move(candidate_focus);
        pending_view = std::move(*prepared.value);
        return {};
    }
    CommandOutcome operate(const ActionRequest& action, CommandSource source) {
        if (action.action == "item.name" && !edit) return {};
        if (action.action == "item.scale" && !scale_request) return {};
        if (action.action == "panes.resize" && !split) return {};
        const auto semantic = view.semantic();
        const auto invocation =
            take(view.commands.resolve_action(semantic, action, view.commands.generation(), {}, source, ++sequence));
        const auto result = take(executor.execute(view.commands, invocation, *this, &semantic));
        if (pending_view) {
            require(result.outcome.status == CommandOutcomeStatus::success,
                    "A rejected Inspector command cannot publish");
            state = std::move(*pending_state);
            focus = std::move(*pending_focus);
            accept(std::move(*pending_view));
            pending_state.reset();
            pending_focus.reset();
            pending_view.reset();
            ++executions;
            if (invocation.record().command == "item.rename") focus_id("name");
        }
        return result.outcome;
    }
    void action(std::string_view id, CommandSource source = CommandSource::keyboard) {
        const auto request = take(request_semantic_action(view.semantic(), *view.tree->find(id), "activate"));
        const auto outcome = operate(request, source);
        checked(outcome);
    }
    void refresh(bool reveal = false) {
        auto next = state;
        auto candidate_focus = focus;
        auto candidate = take(stage(next, viewport, offsets, candidate_focus, reveal));
        state = std::move(next);
        focus = std::move(candidate_focus);
        accept(std::move(candidate));
    }
    void resize(Size size) {
        auto next = state;
        auto candidate_focus = focus;
        auto candidate = take(stage(next, size, offsets, candidate_focus, true));
        viewport = size;
        state = std::move(next);
        focus = std::move(candidate_focus);
        accept(std::move(candidate));
    }
    void input(InputEvent event, CommandSource source = CommandSource::keyboard) {
        const auto diagnostics = validate(event);
        require(diagnostics.empty(), "Invalid normalized Inspector input");
        // Input that can move focus, selection or menus first closes the preedit; it never commits.
        if (!std::holds_alternative<PointerMove>(event.data) && !std::holds_alternative<PointerUp>(event.data) &&
            !std::holds_alternative<PointerCancel>(event.data) && !std::holds_alternative<Scroll>(event.data))
            cancel_composition(source);
        auto focused = take(focus.refresh(view.hit())).focused;
        auto transition = take(context_menu.dispatch(view.hit(), event, focused));
        if (!transition.consumed && !transition.dismiss) transition = take(menu.dispatch(view.hit(), event, focused));
        if (transition.focus) (void)take(focus.focus(view.hit(), *transition.focus));
        if (transition.open_at || transition.dismiss) {
            state.menu = transition.open_at ? OpenMenu::context : OpenMenu::none;
            if (transition.open_at) state.position = *transition.open_at;
            pointer.reset();
            slider_pointer.reset();
            refresh();
        }
        if (transition.consumed) {
            observe();
            return;
        }
        const auto adjustment = take(splitter.dispatch(view.hit(), event, take(focus.refresh(view.hit())).focused));
        if (adjustment.handled) {
            slider_pointer.reset();
            pointer.reset();
            if (adjustment.focus) (void)take(focus.focus(view.hit(), *adjustment.focus));
            if (adjustment.action) {
                split = adjustment.size;
                checked(operate(*adjustment.action, source));
                split.reset();
            }
            observe();
            return;
        }
        if (const auto* cancel = std::get_if<PointerCancel>(&event.data); cancel && slider_pointer == cancel->pointer) {
            slider_pointer.reset();
            observe();
            return;
        }
        if (const auto* move = std::get_if<PointerMove>(&event.data); move && slider_pointer == move->pointer) {
            set_amount(amount_at(move->position), source);
            observe();
            return;
        }
        if (const auto* up = std::get_if<PointerUp>(&event.data);
            up && slider_pointer == up->pointer && up->button == PointerButton::primary) {
            slider_pointer.reset();
            set_amount(amount_at(up->position), source);
            observe();
            return;
        }
        if (const auto* down = std::get_if<PointerDown>(&event.data); down && down->button == PointerButton::primary) {
            const auto target = take(hit_test(view.hit(), down->position)).target;
            for (const auto& [part, steps] : {std::pair{"scale-decrease", -1}, std::pair{"scale-increase", 1}}) {
                const auto node = view.tree->find(part);
                if (!target || !node || !view.contains(*node, *target)) continue;
                // A step part focuses the input and steps once on press; its release activates nothing.
                pointer.reset();
                (void)take(focus.focus(view.hit(), *view.tree->find("scale")));
                if (try_scale(ScaleStep{steps}, source).status != CommandOutcomeStatus::success) refresh(true);
                return;
            }
            if (target && (target == view.tree->find("amount-track") || target == view.tree->find("amount-fill") ||
                           target == view.tree->find("amount-thumb"))) {
                pointer.reset();
                (void)take(focus.focus(view.hit(), *view.tree->find("amount")));
                slider_pointer = down->pointer;
                const int value = amount_at(down->position);
                if (value == state.item().amount)
                    refresh(true);
                else
                    set_amount(value, source);
                return;
            }
        }
        if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
            auto requested = offsets;
            store_offsets(requested, view, take(route_scroll(view.hit(), *scroll)).updates);
            auto next = state;
            auto candidate_focus = focus;
            auto candidate = take(stage(next, viewport, requested, candidate_focus, false));
            slider_pointer.reset();
            state = std::move(next);
            focus = std::move(candidate_focus);
            accept(std::move(candidate));
            return;
        }
        if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
            std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
            const auto dispatched = take(pointer.dispatch(view.hit(), event));
            if (std::holds_alternative<PointerDown>(event.data))
                for (const auto& state : dispatched.pointers)
                    if (state.pressed) {
                        auto owner = *state.pressed;
                        const auto& id = *view.tree->get(owner)->id;
                        if (id.starts_with("hierarchy-toggle-"))
                            owner = *view.tree->find("hierarchy-item-" + id.substr(17));
                        (void)take(focus.focus(view.hit(), owner));
                    }
            for (const auto& request : dispatched.actions) checked(operate(request, source));
            if (std::holds_alternative<PointerDown>(event.data)) refresh(true);
            observe();
            return;
        }
        slider_pointer.reset();
        focused = take(focus.refresh(view.hit())).focused;
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && clipboard_key(*key, source)) return;
        std::optional<decltype(TextEditRequest::operation)> operation;
        if (focused && focused == view.tree->find("name")) {
            if (const auto* committed = std::get_if<tessera::TextInput>(&event.data))
                operation = TextEditInsert{committed->text};
            if (const auto* key = std::get_if<KeyDown>(&event.data)) {
                if (key->key == Key::a && key->modifiers == Modifiers{.control = true} && !key->repeat)
                    operation = TextEditSelection{0, state.item().name.text.size()};
                if (!key->modifiers.control && !key->modifiers.alt && !key->modifiers.super) switch (key->key) {
                        case Key::left:
                            operation = TextEditMove{TextMove::left, key->modifiers.shift};
                            break;
                        case Key::right:
                            operation = TextEditMove{TextMove::right, key->modifiers.shift};
                            break;
                        case Key::home:
                            operation = TextEditMove{TextMove::home, key->modifiers.shift};
                            break;
                        case Key::end:
                            operation = TextEditMove{TextMove::end, key->modifiers.shift};
                            break;
                        case Key::backspace:
                            operation = TextEditErase{TextErase::backward};
                            break;
                        case Key::delete_key:
                            operation = TextEditErase{TextErase::forward};
                            break;
                        default:
                            break;
                    }
            }
        }
        if (operation) {
            const auto name = state.item().name;
            const auto outcome = try_edit({name.owner, name.revision, std::move(*operation)}, source);
            edit_diagnostics = outcome.diagnostics;
            if (outcome.status != CommandOutcomeStatus::success)
                require(!edit_diagnostics.empty(), "Rejected name edit needs diagnostics");
            report("Name edit", state.selected, edit_diagnostics);
            return;
        }
        if (focused && focused == view.tree->find("scale")) {
            std::optional<ScaleRequest> request;
            const auto& draft = state.item().scale_draft;
            const auto edit_request = [&](decltype(TextEditRequest::operation) operation) {
                request = TextEditRequest{draft.owner, draft.revision, std::move(operation)};
            };
            if (const auto* committed = std::get_if<tessera::TextInput>(&event.data))
                edit_request(TextEditInsert{committed->text});
            if (const auto* key = std::get_if<KeyDown>(&event.data)) {
                if (key->key == Key::a && key->modifiers == Modifiers{.control = true} && !key->repeat)
                    edit_request(TextEditSelection{0, draft.text.size()});
                const bool plain = !key->modifiers.control && !key->modifiers.alt && !key->modifiers.super;
                if (plain) switch (key->key) {
                        case Key::left:
                            edit_request(TextEditMove{TextMove::left, key->modifiers.shift});
                            break;
                        case Key::right:
                            edit_request(TextEditMove{TextMove::right, key->modifiers.shift});
                            break;
                        case Key::home:
                            edit_request(TextEditMove{TextMove::home, key->modifiers.shift});
                            break;
                        case Key::end:
                            edit_request(TextEditMove{TextMove::end, key->modifiers.shift});
                            break;
                        case Key::backspace:
                            edit_request(TextEditErase{TextErase::backward});
                            break;
                        case Key::delete_key:
                            edit_request(TextEditErase{TextErase::forward});
                            break;
                        default:
                            break;
                    }
                if (key->modifiers == Modifiers{}) {
                    if (key->key == Key::up) request = ScaleStep{1};
                    if (key->key == Key::down) request = ScaleStep{-1};
                    if (key->key == Key::enter && !key->repeat) request = ScaleApply{};
                    if (key->key == Key::escape && !key->repeat) request = ScaleRevert{};
                }
            }
            if (request) {
                (void)try_scale(std::move(*request), source);
                return;
            }
        }
        if (focused && focused == view.tree->find("amount")) {
            std::optional<int> value;
            if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
                if (key->key == Key::left || key->key == Key::down) value = state.item().amount - 5;
                if (key->key == Key::right || key->key == Key::up) value = state.item().amount + 5;
                if (key->key == Key::home) value = 0;
                if (key->key == Key::end) value = 100;
            }
            if (const auto* navigation = std::get_if<Navigate>(&event.data))
                value =
                    state.item().amount +
                    ((navigation->direction == Direction::left || navigation->direction == Direction::down) ? -5 : 5);
            if (value) {
                set_amount(std::clamp(*value, 0, 100), source);
                return;
            }
        }
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->key == Key::tab)
            event.data =
                key->modifiers.shift ? decltype(event.data){FocusPrevious{}} : decltype(event.data){FocusNext{}};
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
            switch (key->key) {
                case Key::left:
                    event.data = Navigate{Direction::left};
                    break;
                case Key::right:
                    event.data = Navigate{Direction::right};
                    break;
                case Key::up:
                    event.data = Navigate{Direction::up};
                    break;
                case Key::down:
                    event.data = Navigate{Direction::down};
                    break;
                default:
                    break;
            }
        }
        if (std::holds_alternative<tessera::TextInput>(event.data)) return;
        if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->key != Key::home && key->key != Key::end)
            return;
        const auto dispatched = take(focus.dispatch(view.hit(), event));
        for (const auto& request : dispatched.actions) checked(operate(request, source));
        refresh(true);
    }
    // False when the control is absent, for example on the other Inspector tab.
    bool focused_on(std::string_view id) {
        const auto node = view.tree->find(id);
        return node && take(focus.refresh(view.hit())).focused == node;
    }
    void focus_id(std::string_view id) {
        (void)take(focus.focus(view.hit(), *view.tree->find(id)));
        refresh(true);
    }
    const SemanticNode& observed(std::string id) const {
        const auto target = take(resolve_target(view.inspection, AuthorIdTarget{std::move(id)}));
        const auto semantic = view.inspection.elements[target.index].semantic;
        require(semantic.has_value(), "Inspector observation has no semantic entry");
        return view.inspection.semantics.nodes[*semantic];
    }
    std::string observed_name(std::string id) const { return observed(std::move(id)).name; }
    std::optional<bool> observed_checked(std::string id) const { return observed(std::move(id)).selected; }
    std::string observed_value(std::string id) const { return observed(std::move(id)).value.value_or(""); }
    ReplayReload recorded_view() const { return {view.tree->document(), view.styles}; }
    CommandOutcome try_edit(TextEditRequest request, CommandSource source = CommandSource::keyboard) {
        edit = std::move(request);
        const auto outcome =
            operate(take(request_semantic_action(view.semantic(), *view.tree->find("name"), "activate")), source);
        edit.reset();
        return outcome;
    }
    // Addresses the selected item's Scale draft; rejection keeps the draft and reports feedback.
    CommandOutcome try_scale(ScaleRequest request, CommandSource source = CommandSource::keyboard) {
        scale_request = std::move(request);
        const auto outcome =
            operate(take(request_semantic_action(view.semantic(), *view.tree->find("scale"), "activate")), source);
        scale_request.reset();
        edit_diagnostics = outcome.diagnostics;
        if (outcome.status != CommandOutcomeStatus::success)
            require(!edit_diagnostics.empty(), "Rejected Scale input needs diagnostics");
        report("Scale", state.selected, edit_diagnostics);
        return outcome;
    }
    bool resizing() const { return splitter.dragging(); }
    bool composing() const { return state.item().name.composition.has_value(); }
    // Platform sessions address only the focused, eligible name editor that started them.
    void ime(ImeStep step, CommandSource source = CommandSource::keyboard) {
        using Kind = ImeStep::Kind;
        if (step.kind == Kind::start) {
            cancel_composition(source); // A restarted platform session replaces any open preedit.
            ime_ignored = !eligible_name();
            if (ime_ignored) {
                edit_diagnostics = {{"ime_target", Severity::error, "/composition",
                                     "Composition needs the focused, eligible name editor."}};
                report("Composition", state.selected, edit_diagnostics);
                return;
            }
            ime_item = state.selected;
        } else if (ime_ignored) {
            return;
        }
        if (step.kind == Kind::cancel) return cancel_composition(source);
        const auto name = state.item().name;
        const auto id = name.composition ? name.composition->id : 0;
        std::optional<decltype(TextEditRequest::operation)> operation;
        if (state.selected != ime_item || !eligible_name()) {
            edit_diagnostics = {{"stale_composition", Severity::error, "/composition",
                                 "Composition target changed during the platform session."}};
        } else {
            if (step.kind == Kind::start)
                operation = TextCompositionStart{};
            else if (step.kind == Kind::update)
                operation = TextCompositionUpdate{id, std::move(step.text), step.caret};
            else
                operation = TextCompositionCommit{id, std::move(step.text)};
            edit_diagnostics = try_edit({name.owner, name.revision, std::move(*operation)}, source).diagnostics;
        }
        // A rejected commit still ends the platform session: drop preedit, keep committed bytes.
        if (step.kind == Kind::commit && !edit_diagnostics.empty()) {
            auto rejected = std::move(edit_diagnostics);
            cancel_composition(source);
            edit_diagnostics = std::move(rejected);
        }
        report("Composition", state.selected, edit_diagnostics);
    }
    // Logical caret rectangle in the presented name, for platform candidate placement.
    std::optional<Rect> caret_rect() {
        const auto name = view.tree->find("name");
        if (!name) return {};
        const auto parts = view.tree->children(*name);
        if (parts.size() < 2) return {};
        const auto found = std::find_if(view.layout.boxes.begin(), view.layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == parts[1]; });
        if (found == view.layout.boxes.end() || !found->visible) return {};
        const auto content = found->content_box();
        const auto& style = view.styles[parts[1].index].text;
        const auto shown = take(display_text_edit(state.item().name));
        const auto run = take(text.shape(shown.text, style, {content.size.width}));
        const float advance = style.size * PlaceholderTextShaper::advance_em;
        Point pen{0, run.metrics.baseline};
        for (const auto& glyph : run.glyphs) {
            if (glyph.cluster >= shown.caret) {
                pen = glyph.position;
                break;
            }
            pen = {glyph.position.x + static_cast<float>(inspector_text::cells(glyph.id)) * advance, glyph.position.y};
        }
        const float top = pen.y - run.metrics.baseline;
        return Rect{{content.origin.x + pen.x, content.origin.y + top}, {1, run.metrics.line_height}};
    }

private:
    bool eligible_name() {
        return state.menu == OpenMenu::none && focused_on("name") &&
               static_cast<bool>(request_semantic_action(view.semantic(), *view.tree->find("name"), "activate"));
    }
    void cancel_composition(CommandSource source) {
        const auto name = state.item().name;
        if (!name.composition) return;
        const auto outcome = try_edit({name.owner, name.revision, TextCompositionCancel{name.composition->id}}, source);
        if (outcome.status != CommandOutcomeStatus::success) {
            // Presentation failed; drop preedit anyway so no stale session survives,
            // and replace the view once a presentation succeeds.
            state.item().name.composition.reset();
            ++state.item().name.revision;
            auto next = state;
            auto candidate_focus = focus;
            if (auto candidate = stage(next, viewport, offsets, candidate_focus, false)) {
                state = std::move(next);
                focus = std::move(candidate_focus);
                accept(std::move(*candidate.value));
            }
        }
    }
    bool clipboard_key(const KeyDown& key, CommandSource source) {
        if ((key.key != Key::c && key.key != Key::v && key.key != Key::x) ||
            key.modifiers != Modifiers{.control = true} || key.repeat || state.menu != OpenMenu::none ||
            !focused_on("name"))
            return false;
        clipboard_diagnostics.clear();
        const auto eligible = [&] { return eligible_name(); };
        if (!eligible()) return true;
        const auto selected = state.selected;
        const auto name = state.item().name;
        const auto target_unchanged = [&] {
            return state.selected == selected && state.item().name == name && eligible();
        };
        if (key.key == Key::c || key.key == Key::x) {
            const auto begin = std::min(name.anchor, name.caret), end = std::max(name.anchor, name.caret);
            if (begin == end) return true;
            if (clipboard_write)
                clipboard_diagnostics = clipboard_write(std::string_view(name.text).substr(begin, end - begin));
            else
                clipboard_error("clipboard_unavailable", "No clipboard write service is connected.");
            if (key.key == Key::x && clipboard_diagnostics.empty()) {
                if (!target_unchanged()) {
                    clipboard_error("stale_clipboard_target", "Clipboard target changed during the host request.");
                } else {
                    const auto outcome =
                        try_edit({name.owner, name.revision, TextEditErase{TextErase::forward}}, source);
                    clipboard_diagnostics = outcome.diagnostics;
                }
            }
        } else {
            if (!clipboard_read) {
                clipboard_error("clipboard_unavailable", "No clipboard read service is connected.");
            } else {
                auto text = clipboard_read();
                if (!text) {
                    clipboard_diagnostics = std::move(text.diagnostics);
                } else if (!target_unchanged()) {
                    clipboard_error("stale_clipboard_target", "Clipboard target changed during the host request.");
                } else if (text.value->find('\0') != std::string::npos) {
                    clipboard_error("clipboard_nul", "Clipboard text contains an embedded NUL.");
                } else if (!text.value->empty()) {
                    const auto outcome =
                        try_edit({name.owner, name.revision, TextEditInsert{std::move(*text.value)}}, source);
                    clipboard_diagnostics = outcome.diagnostics;
                }
            }
        }
        report(key.key == Key::c ? "Copy" : key.key == Key::x ? "Cut" : "Paste", selected, clipboard_diagnostics);
        return true;
    }
    void report(std::string_view operation, std::string_view item, const std::vector<Diagnostic>& diagnostics) {
        feedback.clear();
        if (diagnostics.empty()) return;
        feedback = std::string(operation) + " could not finish. ";
        const auto& code = diagnostics.front().code;
        if (code == "text_too_long")
            feedback += "Names must fit within 4096 UTF-8 bytes.";
        else if (code == "multiline_text")
            feedback += "Use a single line for the name.";
        else if (code == "number_syntax" || code == "number_out_of_range")
            feedback += "Enter a number from 0 to 10.";
        else if (code == "invalid_utf8" || code == "invalid_utf16" || code == "clipboard_nul")
            feedback += "Use valid Unicode text.";
        else
            feedback += "Try again.";
        for (const auto& diagnostic : diagnostics)
            std::cerr << operation << " for " << item << ": " << diagnostic.code << " at " << diagnostic.path << ": "
                      << diagnostic.message << '\n';
    }
    void clipboard_error(std::string code, std::string message) {
        clipboard_diagnostics = {{std::move(code), Severity::error, "/clipboard", std::move(message)}};
    }
    ui::Session session;
    CommandExecutor executor;
    PointerDispatcher pointer{PointerSnapshotPolicy::compatible_author_ids};
    SplitPaneDispatcher splitter{"panes"};
    MenuDispatcher menu{"actions-menu"}, context_menu{"item-menu"};
    Offsets offsets;
    std::optional<TextEditRequest> edit;
    std::optional<ScaleRequest> scale_request;
    std::string ime_item;
    bool ime_ignored = false;
    std::optional<int> amount;
    std::optional<float> split;
    std::optional<PointerId> slider_pointer;
    std::optional<State> pending_state;
    std::optional<FocusDispatcher> pending_focus;
    std::optional<View> pending_view;
    std::uint64_t generation = 1, sequence = 0;

    static void checked(const CommandOutcome& outcome) {
        if (outcome.status == CommandOutcomeStatus::success) return;
        require(!outcome.diagnostics.empty(), "Inspector command failed without diagnostics");
        (void)take(Result<int>{std::nullopt, outcome.diagnostics});
    }
    static void store_offsets(Offsets& offsets, const View& view, const std::vector<ScrollUpdate>& updates) {
        for (const auto& update : updates) {
            const auto& id = view.tree->get(update.container)->id;
            require(id.has_value(), "Inspector scroll owners need stable author IDs");
            offsets[*id] = update.offset;
        }
    }
    Result<View> prepare(const State& state, Size viewport, const Offsets& offsets, const CommandSnapshot& commands) {
        auto authored = session.lower(Form(state, viewport), context);
        if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
        auto document = commands.apply_eligibility(authored.value->document, context);
        if (!document) return {std::nullopt, std::move(document.diagnostics)};
        auto tree = UiTree::create(*document.value, context);
        if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
        auto sheet = appearance(state);
        auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
        if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
        std::vector<Point> requested((*tree.value)->size());
        for (const auto& [id, offset] : offsets)
            if (const auto node = (*tree.value)->find(id)) requested[node->index] = offset;
        auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text, requested});
        if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
        auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
        if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
        return {View{commands,
                     std::move(*tree.value),
                     std::move(*styles.value),
                     std::move(*layout.value),
                     {},
                     std::move(*paint.value),
                     std::move(sheet),
                     std::move(authored.value->styles)},
                {}};
    }
    Result<View> stage(State& candidate, Size viewport, Offsets requested, FocusDispatcher& candidate_focus,
                       bool reveal) {
        const auto commands =
            take(CommandSnapshot::publish(registry, ++generation, CommandMode::production, eligibility));
        auto result = prepare(candidate, viewport, requested, commands);
        if (!result) return result;
        const auto geometry = take(split_pane_geometry(result.value->hit(), "panes"));
        require(geometry.has_value(), "Inspector splitter is missing");
        if (candidate.split != geometry->size) {
            candidate.split = geometry->size;
            result = prepare(candidate, viewport, requested, commands);
            if (!result) return result;
        }
        const auto focused = take(candidate_focus.refresh(result.value->hit())).focused;
        if (reveal && focused) {
            const auto updates = take(scroll_into_view(result.value->hit(), *focused));
            if (!updates.empty()) {
                store_offsets(requested, *result.value, updates);
                result = prepare(candidate, viewport, requested, commands);
                if (!result) return result;
            }
        }
        const auto target = take(candidate_focus.refresh(result.value->hit())).focused;
        auto candidate_pointer = pointer;
        auto visuals = prepare_visuals(*result.value, interaction(*result.value, candidate_pointer, target), text);
        if (!visuals) return {std::nullopt, std::move(visuals.diagnostics)};
        accept_visuals(*result.value, std::move(*visuals.value));
        return result;
    }
    void accept(View candidate) {
        view = std::move(candidate);
        for (const auto& box : view.layout.boxes)
            if (box.scroll) offsets[*view.tree->get(box.node)->id] = box.scroll->offset;
        (void)take(pointer.refresh(view.hit()));
        (void)take(splitter.refresh(view.hit()));
        (void)take(focus.refresh(view.hit()));
        bool dismissed = false;
        for (const auto& transition : {take(menu.refresh(view.hit())), take(context_menu.refresh(view.hit()))}) {
            if (transition.focus) (void)take(focus.focus(view.hit(), *transition.focus));
            dismissed = dismissed || transition.dismiss;
        }
        if (dismissed) {
            state.menu = OpenMenu::none;
            pointer.reset();
            slider_pointer.reset();
            refresh();
            return;
        }
        observe();
        session.publish();
    }
    InteractionState interaction(const View& candidate, PointerDispatcher& pointers,
                                 std::optional<NodeHandle> focused) const {
        InteractionState state;
        for (const auto& pointer : take(pointers.refresh(candidate.hit())).pointers) {
            if (pointer.hovered) state.hovered.push_back(*pointer.hovered);
            if (pointer.active) state.active.push_back(*pointer.active);
        }
        state.focused = focused;
        if (slider_pointer) state.active.push_back(*candidate.tree->find("amount"));
        if (splitter.dragging()) state.active.push_back(*candidate.tree->find("panes-handle"));
        return state;
    }
    void observe() {
        accept_visuals(view, take(prepare_visuals(
                                 view, interaction(view, pointer, take(focus.refresh(view.hit())).focused), text)));
    }
    int amount_at(Point point) const {
        const auto& track = view.box("amount-track").border_box;
        require(track.size.width > 8, "Inspector Slider has no travel");
        return take(ui::slider_value_at(amount_range, (point.x - track.origin.x - 4) / (track.size.width - 8)));
    }
    void set_amount(int value, CommandSource source) {
        if (value == state.item().amount) return;
        amount = value;
        const auto outcome =
            operate(take(request_semantic_action(view.semantic(), *view.tree->find("amount"), "activate")), source);
        amount.reset();
        checked(outcome);
    }
};

void composition_smoke() {
    using Kind = ImeStep::Kind;
    App app;
    const auto click = [&](const char* id) {
        const auto point = app.view.reachable(id);
        app.input({0us, PointerDown{{1}, point}}, CommandSource::pointer);
        app.input({0us, PointerUp{{1}, point}}, CommandSource::pointer);
    };
    const auto value_box = [&] {
        const auto parts = app.view.tree->children(*app.view.tree->find("name"));
        const auto found = std::find_if(app.view.layout.boxes.begin(), app.view.layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == parts[1]; });
        require(found != app.view.layout.boxes.end(), "Name value has no layout");
        return std::pair{found->content_box(), app.view.styles[parts[1].index].text.size * .5f};
    };
    app.focus_id("name");
    app.input({0us, KeyDown{Key::end}});
    const auto executions = app.executions;
    app.ime({Kind::start});
    require(app.composing() && app.state.item().name.text == "Cube" && app.edit_diagnostics.empty(),
            "Composition start must open preedit without changing committed bytes");
    app.ime({Kind::update, "\xe3\x81\xab\xe3\x81\xbb", 6});                          // niho preedit
    app.ime({Kind::update, "\xe3\x81\xab\xe3\x81\xbb\xe3\x82\x93\xe3\x81\x94", 12}); // nihongo preedit
    require(app.state.item().name.text == "Cube" && app.observed_name("hierarchy-item-cube") == "Cube" &&
                app.observed_name("name") == "Name" &&
                app.observed_value("name") == "Cube\xe3\x81\xab\xe3\x81\xbb\xe3\x82\x93\xe3\x81\x94" &&
                app.executions == executions + 3,
            "Preedit must appear only in the name editor through ordinary edit commands");
    const auto [content, advance] = value_box();
    auto caret = app.caret_rect();
    require(caret && caret->origin.x == content.origin.x + 12 * advance && caret->origin.y == content.origin.y,
            "Candidate placement must follow the preedit caret across two-cell Japanese glyphs");
    const std::string japanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"; // Japanese kanji
    app.ime({Kind::update, japanese, 3});
    caret = app.caret_rect();
    require(caret && caret->origin.x == content.origin.x + 6 * advance, "Preedit caret may sit inside the preedit");
    app.ime({Kind::commit, japanese});
    auto name = app.state.item().name;
    require(!name.composition && name.text == "Cube" + japanese && name.anchor == 13 && name.caret == 13 &&
                app.observed_name("hierarchy-item-cube") == "Cube" + japanese && app.feedback.empty(),
            "Commit must replace the captured range once and reach the Hierarchy");

    app.input({0us, KeyDown{Key::a, {.control = true}}});
    app.ime({Kind::start});
    app.ime({Kind::update, "x", 1});
    app.ime({Kind::cancel});
    require(app.state.item().name.text == name.text && app.state.item().name.anchor == 0 &&
                app.state.item().name.caret == 13 && !app.composing(),
            "Cancel must keep the committed bytes and selection");
    const std::string katakana = "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88"; // katakana
    app.ime({Kind::start});
    app.ime({Kind::update, katakana, 9});
    app.ime({Kind::commit, katakana});
    require(app.state.item().name.text == katakana, "Commit must replace the selected committed text");
    name = app.state.item().name;
    app.ime({Kind::update, "late", 4});
    require(app.state.item().name == name && app.edit_diagnostics.front().code == "no_composition" &&
                app.feedback.starts_with("Composition could not finish."),
            "Late platform steps for a closed composition must fail without editing");

    app.ime({Kind::start});
    app.ime({Kind::update, "\xe3\x81\x94", 3});
    app.input({0us, KeyDown{Key::tab}});
    require(!app.state.item().name.composition && app.state.item().name.text == katakana &&
                take(app.focus.refresh(app.view.hit())).focused != app.view.tree->find("name"),
            "Focus traversal must cancel preedit before moving focus");
    app.ime({Kind::start});
    app.ime({Kind::update, "x", 1});
    require(app.edit_diagnostics.front().code == "ime_target" && !app.composing() &&
                app.state.item().name.text == katakana,
            "A session outside the name editor must be ignored");
    app.focus_id("name");
    app.ime({Kind::start});
    app.ime({Kind::update, "\xe3\x81\x94", 3});
    click("hierarchy-item-sphere");
    require(app.state.selected == "sphere" && !app.state.items[1].name.composition &&
                app.state.items[1].name.text == katakana,
            "A pointer press must cancel preedit before changing selection");
    app.ime({Kind::commit, "late"});
    require(app.state.items[2].name.text == "Sphere" && app.state.items[1].name.text == katakana,
            "A stale commit must not redirect to the newly selected item");

    app.focus_id("name");
    app.input({0us, KeyDown{Key::a, {.control = true}}});
    app.input({0us, tessera::TextInput{std::string(max_text_edit_bytes - 3, 'x')}});
    name = app.state.item().name;
    app.ime({Kind::start});
    app.ime({Kind::commit, "\xe3\x81\x82\xe3\x82\x8a"}); // Two scalars exceed the remaining three bytes.
    require(app.state.item().name.text == name.text && !app.composing() &&
                app.edit_diagnostics.front().code == "text_too_long" &&
                app.feedback.find("4096 UTF-8 bytes") != std::string::npos,
            "A rejected commit must close preedit and retain committed bytes with feedback");
    app.input({0us, KeyDown{Key::a, {.control = true}}});
    app.input({0us, tessera::TextInput{"Sphere"}});
    app.ime({Kind::start});
    app.text.reject = true;
    const auto open = app.state.item().name;
    app.ime({Kind::update, "\xe3\x81\x82", 3});
    require(app.state.item().name == open && app.edit_diagnostics.front().code == "inspector_measurement",
            "Rejected preedit presentation must retain the accepted composition");
    app.text.reject = false;
    app.ime({Kind::update, "\xe3\x81\x82", 3});
    app.ime({Kind::commit, "\xe3\x81\x82"});
    require(app.state.item().name.text == "Sphere\xe3\x81\x82" && app.feedback.empty(),
            "Composition must recover after a rejected presentation");
}
void edit_rejection_smoke() {
    App app;
    app.focus_id("name");
    app.input({0us, KeyDown{Key::a, {.control = true}}});
    app.input({0us, tessera::TextInput{"A\xe3\x81\x82\xf0\x9f\x98\x80Z"}});
    app.input({0us, KeyDown{Key::left}});
    app.input({0us, KeyDown{Key::left, {.shift = true}}});
    app.input({0us, KeyDown{Key::left, {.shift = true}}});
    const auto accepted = app.state.items;
    const auto name = app.state.item().name;
    require(name.anchor == 8 && name.caret == 1, "Rejection fixture needs a reversed Unicode selection");
    const auto generation = app.view.commands.generation(), executions = app.executions;
    const auto inspection_generation = app.view.inspection.generation;
    const auto* tree = app.view.tree.get();
    const auto focused = take(app.focus.refresh(app.view.hit())).focused;
    const auto unchanged = [&] {
        require(app.state.items == accepted && app.state.selected == "cube" && app.view.tree.get() == tree &&
                    app.view.commands.generation() == generation && app.executions == executions &&
                    app.view.inspection.generation == inspection_generation &&
                    take(app.focus.refresh(app.view.hit())).focused == focused &&
                    app.observed_name("hierarchy-item-cube") == name.text,
                "Rejected input must retain items, selection, focus and the accepted presentation");
    };
    for (const auto& text : {std::string("two\nlines"), std::string("two\rlines"), std::string(4097, 'x')}) {
        app.input({0us, tessera::TextInput{text}});
        require(!app.edit_diagnostics.empty() && app.feedback.starts_with("Name edit could not finish."),
                "Rejected committed text needs recoverable host feedback");
        unchanged();
    }
    app.text.reject = true;
    for (const auto& event : std::vector<InputEvent>{
             {0us, tessera::TextInput{"retry"}}, {0us, KeyDown{Key::backspace}}, {0us, KeyDown{Key::right}}}) {
        app.input(event);
        require(app.edit_diagnostics.front().code == "inspector_measurement" && !app.feedback.empty(),
                "Rejected text, deletion and selection staging must preserve the original diagnostic");
        unchanged();
    }
    app.text.reject = false;
    app.input({0us, tessera::TextInput{"retry"}});
    require(app.state.item().name.text == "AretryZ" && app.state.item().name.revision == name.revision + 1 &&
                app.executions == executions + 1 && app.edit_diagnostics.empty() && app.feedback.empty(),
            "A successful retry must replace the retained selection once and clear feedback");
    const auto cube = app.state.item();
    app.action("hierarchy-item-sphere");
    app.focus_id("name");
    app.input({0us, tessera::TextInput{"!"}});
    app.action("hierarchy-item-cube");
    require(app.state.item() == cube && app.state.items[2].name.text == "Sphere!",
            "Recovered editing must retain independent items across selection changes");
}

void clipboard_smoke() {
    App app;
    app.focus_id("name");
    auto shortcut = [&](Key key, Modifiers modifiers = {.control = true}, bool repeat = false) {
        app.input({0us, KeyDown{key, modifiers, repeat}});
    };
    auto edit = [&](auto operation) {
        const auto name = app.state.item().name;
        require(app.try_edit({name.owner, name.revision, operation}).status == CommandOutcomeStatus::success,
                "Clipboard fixture edit failed");
    };
    std::string clipboard;
    unsigned reads = 0, writes = 0;
    bool fail_read = false, fail_write = false;
    const std::vector<Diagnostic> unavailable{
        {"clipboard_unavailable", Severity::error, "/clipboard", "Injected failure"}};
    app.clipboard_read = [&]() -> Result<std::string> {
        ++reads;
        if (fail_read) return {std::nullopt, unavailable};
        return {clipboard, {}};
    };
    app.clipboard_write = [&](std::string_view text) {
        ++writes;
        if (fail_write) return unavailable;
        clipboard = text;
        return std::vector<Diagnostic>{};
    };
    edit(TextEditSelection{0, app.state.item().name.text.size()});
    edit(TextEditInsert{"A\xe3\x81\x82\xf0\x9f\x98\x80Z"}); // A, hiragana, emoji, Z.
    edit(TextEditSelection{8, 1});                          // Reverse selection of the two non-ASCII scalars.
    const auto selected = app.state.item().name;
    const auto generation = app.view.commands.generation(), executions = app.executions;
    shortcut(Key::c);
    require(clipboard == "\xe3\x81\x82\xf0\x9f\x98\x80" && writes == 1 && app.state.item().name == selected &&
                app.view.commands.generation() == generation && app.executions == executions,
            "Copy must preserve exact selected UTF-8 bytes and accepted state");
    fail_write = true;
    shortcut(Key::c);
    require(app.clipboard_diagnostics.front().code == "clipboard_unavailable" && app.state.item().name == selected &&
                app.view.commands.generation() == generation,
            "Failed copy must retain the editor and view");
    fail_write = false;
    clipboard = "\xe6\x97\xa5\xe6\x9c\xac\xf0\x9f\x98\x80"; // Japanese plus a supplementary scalar.
    const auto before_items = app.state.items;
    shortcut(Key::v);
    const auto pasted = app.state.item().name;
    require(pasted.text == "A" + clipboard + "Z" && pasted.anchor == 11 && pasted.caret == 11 &&
                pasted.owner == selected.owner && pasted.revision == selected.revision + 1 &&
                app.executions == executions + 1 && app.clipboard_diagnostics.empty() &&
                app.observed_name("hierarchy-item-cube") == pasted.text,
            "Paste must replace the captured selection once and publish shared observations");
    auto expected_items = before_items;
    expected_items[1].name = pasted;
    require(app.state.items == expected_items, "Paste must retain other items and properties");
    app.action("hierarchy-item-sphere");
    app.action("hierarchy-item-cube");
    app.focus_id("name");
    require(app.state.item().name == pasted, "Select and return must retain pasted bytes and caret");
    shortcut(Key::c); // Empty selection does not overwrite clipboard.
    require(writes == 2, "Collapsed selection must not call the clipboard writer");
    const auto guarded_reads = reads, guarded_writes = writes;
    for (const auto key : {Key::c, Key::v}) {
        shortcut(key, {});
        shortcut(key, {.shift = true, .control = true});
        shortcut(key, {.control = true, .alt = true});
        shortcut(key, {.control = true, .super = true});
        shortcut(key, {.control = true}, true);
    }
    app.focus_id("hierarchy-item-cube");
    shortcut(Key::c);
    shortcut(Key::v);
    app.action("actions");
    shortcut(Key::v);
    app.input({0us, KeyDown{Key::escape}});
    require(reads == guarded_reads && writes == guarded_writes && app.state.item().name == pasted,
            "Clipboard shortcuts require exact modifiers, initial keydown and the focused editor outside menus");
    app.focus_id("name");
    edit(TextEditSelection{1, 11});
    const auto accepted = app.state.item();
    const auto accepted_generation = app.view.commands.generation(), accepted_executions = app.executions;
    auto unchanged = [&] {
        require(app.state.item() == accepted && app.view.commands.generation() == accepted_generation &&
                    app.executions == accepted_executions,
                "Rejected paste must preserve accepted state and generation");
    };
    for (const auto& text : std::vector<std::string>{"two\nlines", "two\rlines", std::string(4097, 'x'),
                                                     std::string("\xc0\xaf", 2), std::string("a\0b", 3)}) {
        clipboard = text;
        shortcut(Key::v);
        require(!app.clipboard_diagnostics.empty(), "Invalid clipboard text needs a diagnostic");
        unchanged();
    }
    clipboard.clear();
    shortcut(Key::v);
    require(app.clipboard_diagnostics.empty(), "Empty clipboard text is a no-op");
    unchanged();
    fail_read = true;
    shortcut(Key::v);
    require(app.clipboard_diagnostics.front().code == "clipboard_unavailable", "Read failure must be reported");
    unchanged();
    fail_read = false;
    clipboard = "retry";
    app.text.reject = true;
    shortcut(Key::v);
    require(app.clipboard_diagnostics.front().code == "inspector_measurement", "Presentation failure must be reported");
    unchanged();
    app.text.reject = false;
    shortcut(Key::v);
    require(app.state.item().name.text == "AretryZ" && app.state.item().name.revision == accepted.name.revision + 1,
            "Rejected paste must retry against the retained revision");
    app.clipboard_read = [&]() -> Result<std::string> {
        app.action("hierarchy-item-sphere");
        return {"wrong item", {}};
    };
    shortcut(Key::v);
    require(app.state.selected == "sphere" && app.state.item().name.text == "Sphere" &&
                app.clipboard_diagnostics.front().code == "stale_clipboard_target",
            "Host callbacks must never redirect paste to another selected item");
    app.focus_id("name");
    app.clipboard_read = [&]() -> Result<std::string> {
        edit(TextEditSelection{0, 0});
        return {"stale selection", {}};
    };
    shortcut(Key::v);
    require(app.state.item().name.text == "Sphere" && app.state.item().name.anchor == 0 &&
                app.clipboard_diagnostics.front().code == "stale_clipboard_target",
            "Changed revisions must reject a clipboard completion");
    app.clipboard_read = [&]() -> Result<std::string> {
        app.focus_id("enabled");
        return {"lost focus", {}};
    };
    shortcut(Key::v);
    require(app.state.item().name.text == "Sphere" &&
                app.clipboard_diagnostics.front().code == "stale_clipboard_target",
            "Focus loss must reject a clipboard completion");
    app.focus_id("name");
    app.clipboard_read = {};
    shortcut(Key::v);
    require(app.clipboard_diagnostics.front().code == "clipboard_unavailable", "Missing service must fail closed");
}

void cut_smoke() {
    App app;
    app.focus_id("name");
    auto edit = [&](auto operation) {
        const auto name = app.state.item().name;
        require(app.try_edit({name.owner, name.revision, operation}).status == CommandOutcomeStatus::success,
                "Cut fixture edit failed");
    };
    auto cut = [&](Modifiers modifiers = {.control = true}, bool repeat = false) {
        app.input({0us, KeyDown{Key::x, modifiers, repeat}});
    };
    std::string clipboard = "retained clipboard";
    unsigned writes = 0;
    bool fail_write = false;
    app.clipboard_write = [&](std::string_view text) {
        ++writes;
        if (fail_write)
            return std::vector<Diagnostic>{{"clipboard_write", Severity::error, "/clipboard", "Injected failure"}};
        clipboard = text;
        return std::vector<Diagnostic>{};
    };
    edit(TextEditSelection{0, app.state.item().name.text.size()});
    edit(TextEditInsert{"A\xe3\x81\x82\xf0\x9f\x98\x80Z"});
    edit(TextEditSelection{8, 1});
    const auto selected = app.state.item().name;
    const auto items = app.state.items;
    const auto generation = app.view.commands.generation(), executions = app.executions;
    auto unchanged = [&] {
        require(app.state.items == items && app.view.commands.generation() == generation &&
                    app.executions == executions,
                "Rejected cut must preserve accepted items, selection and presentation");
    };
    for (const auto modifiers : {Modifiers{}, Modifiers{.shift = true, .control = true},
                                 Modifiers{.control = true, .alt = true}, Modifiers{.control = true, .super = true}})
        cut(modifiers);
    cut({.control = true}, true);
    require(writes == 0, "Cut requires exact modifiers and an initial keydown");
    unchanged();
    fail_write = true;
    cut();
    require(app.clipboard_diagnostics.front().code == "clipboard_write" && clipboard == "retained clipboard",
            "Failed clipboard write must not delete the selected text");
    unchanged();
    fail_write = false;
    app.text.reject = true;
    cut();
    require(app.clipboard_diagnostics.front().code == "inspector_measurement" &&
                clipboard == "\xe3\x81\x82\xf0\x9f\x98\x80",
            "Rejected cut presentation must report failure after copying the original selection");
    unchanged();
    app.text.reject = false;
    cut();
    const auto cut_name = app.state.item().name;
    auto expected = items;
    expected[1].name = cut_name;
    require(cut_name.text == "AZ" && cut_name.owner == selected.owner && cut_name.revision == selected.revision + 1 &&
                cut_name.anchor == 1 && cut_name.caret == 1 && app.state.items == expected &&
                clipboard == "\xe3\x81\x82\xf0\x9f\x98\x80" && app.executions == executions + 1 &&
                app.clipboard_diagnostics.empty() && app.observed_name("hierarchy-item-cube") == "AZ",
            "Cut retry must erase exactly the reverse Unicode selection once through the name command");
    const auto empty_generation = app.view.commands.generation(), empty_executions = app.executions;
    cut();
    require(writes == 3 && app.state.item().name == cut_name && app.view.commands.generation() == empty_generation &&
                app.executions == empty_executions,
            "Collapsed cut must preserve the following scalar, clipboard and generation");
    app.action("hierarchy-item-sphere");
    app.action("hierarchy-item-cube");
    app.focus_id("name");
    require(app.state.item().name == cut_name, "Select and return must retain cut text and selection");
    edit(TextEditSelection{0, 2});
    app.focus_id("enabled");
    cut();
    app.focus_id("name");
    app.action("actions");
    cut();
    app.input({0us, KeyDown{Key::escape}});
    require(writes == 3 && app.state.item().name.text == "AZ", "Cut must respect focus and open menu routing");
    app.focus_id("name");
    app.clipboard_write = {};
    const auto missing = app.state.item().name;
    cut();
    require(app.state.item().name == missing && app.clipboard_diagnostics.front().code == "clipboard_unavailable",
            "Missing write service must leave cut selection intact");

    for (unsigned change = 0; change < 4; ++change) {
        App reentrant;
        reentrant.focus_id("name");
        const auto name = reentrant.state.item().name;
        require(reentrant.try_edit({name.owner, name.revision, TextEditSelection{0, name.text.size()}}).status ==
                    CommandOutcomeStatus::success,
                "Reentrant cut fixture selection failed");
        auto accepted = reentrant.state.items;
        auto accepted_generation = reentrant.view.commands.generation();
        auto accepted_executions = reentrant.executions;
        reentrant.clipboard_write = [&](std::string_view text) {
            if (change == 0)
                reentrant.action("hierarchy-item-sphere");
            else if (change == 1) {
                const auto observed = reentrant.state.item().name;
                require(reentrant.try_edit({observed.owner, observed.revision, TextEditSelection{0, 0}}).status ==
                            CommandOutcomeStatus::success,
                        "Reentrant selection edit failed");
            } else if (change == 2)
                reentrant.focus_id("enabled");
            else
                reentrant.action("actions");
            require(text == "Cube", "Cut callback must borrow captured bytes even after changing the target");
            accepted = reentrant.state.items;
            accepted_generation = reentrant.view.commands.generation();
            accepted_executions = reentrant.executions;
            return std::vector<Diagnostic>{};
        };
        reentrant.input({0us, KeyDown{Key::x, {.control = true}}});
        require(reentrant.state.items == accepted && reentrant.view.commands.generation() == accepted_generation &&
                    reentrant.executions == accepted_executions &&
                    reentrant.clipboard_diagnostics.front().code == "stale_clipboard_target",
                "Changed item, revision, focus or menu must reject deletion after the clipboard write");
    }
}

void appearance_smoke() {
    App app;
    const auto style = [&](std::string_view id) -> const ResolvedStyle& {
        return app.view.styles[app.view.tree->find(id)->index];
    };
    const auto row = app.view.box("hierarchy-item-sphere").border_box;
    const auto sphere = app.view.center("hierarchy-item-sphere");
    const auto accepted = app.state.items;
    const auto generation = app.view.commands.generation();
    const auto* tree = app.view.tree.get();
    const auto normal = style("hierarchy-item-sphere").background;
    require(row.size.height >= 30 && style("name").corner_radius == 6 && style("amount-track").corner_radius == 4,
            "Inspector must use comfortable hierarchy rows and rounded controls");
    require(style("hierarchy-item-cube").background != style("hierarchy-item-sphere").background &&
                style("hierarchy-label-cube").color == Color{1, 1, 1, 1},
            "Controlled selection must have a distinct fill and inherited readable label color");
    std::vector<UiDrawList> states{app.view.paint};
    app.input({0us, PointerMove{{1}, sphere}}, CommandSource::pointer);
    states.push_back(app.view.paint);
    require(app.view.tree.get() == tree && app.view.commands.generation() == generation &&
                app.state.items == accepted && app.state.selected == "cube" && app.executions == 0,
            "Hover feedback must retain the accepted tree, commands and item values");
    const auto hovered = style("hierarchy-item-sphere").background;
    app.input({0us, PointerDown{{1}, sphere}}, CommandSource::pointer);
    states.push_back(app.view.paint);
    require(style("hierarchy-item-sphere").background != hovered && app.state.selected == "cube",
            "Press feedback must survive focus publication without prematurely selecting");
    const auto away = app.view.center("actions");
    app.input({0us, PointerMove{{1}, away}}, CommandSource::pointer);
    require(style("hierarchy-item-sphere").background == normal, "Leaving a pressed row must remove its active fill");
    app.input({0us, PointerUp{{1}, away}}, CommandSource::pointer);
    require(app.executions == 0 && app.state.selected == "cube",
            "Releasing outside the pressed row must not select an item");
    app.input({0us, PointerDown{{1}, sphere}}, CommandSource::pointer);
    app.input({0us, PointerCancel{{1}}}, CommandSource::pointer);
    app.input({0us, PointerUp{{1}, sphere}}, CommandSource::pointer);
    require(app.executions == 0 && app.state.selected == "cube",
            "Cancelled visual presses must not activate on release");
    app.input({0us, PointerDown{{1}, sphere}}, CommandSource::pointer);
    app.input({0us, PointerUp{{1}, sphere}}, CommandSource::pointer);
    require(app.executions == 1 && app.state.selected == "sphere" && app.state.items == accepted,
            "A styled press/release must select exactly once without editing item values");
    app.input({0us, PointerMove{{1}, away}}, CommandSource::pointer);
    app.focus_id("name");
    states.push_back(app.view.paint);
    app.focus_id("hierarchy-item-sphere");
    states.push_back(app.view.paint);
    require(app.view.box("hierarchy-item-sphere").border_box == row &&
                style("hierarchy-item-sphere").border_color != Color{0, 0, 0, 0},
            "Focus must remain distinct from selection without moving hierarchy geometry");
    CpuFrameCapture frames;
    for (auto& paint : states) {
        std::erase_if(paint.commands,
                      [](const auto& command) { return std::holds_alternative<DrawGlyphRun>(command); });
        paint.commands.insert(paint.commands.begin(),
                              PushTransform{Affine2D{.tx = -row.origin.x, .ty = -row.origin.y}});
        paint.commands.push_back(PopTransform{});
    }
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        std::vector<CapturedImage> images;
        for (const auto& paint : states)
            images.push_back(take(frames.capture({0, row.size, scale, take(capture_extent(row.size, scale)), &paint})));
        for (const auto [before, after] :
             std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {1, 2}, {0, 3}, {3, 4}}) {
            const auto& a = images[before];
            const auto& b = images[after];
            std::size_t changes = 0;
            for (unsigned y = 0; y < a.extent.height; ++y)
                for (unsigned x = 0; x < a.extent.width; ++x) {
                    const auto at = (std::size_t(y) * a.extent.width + x) * 4;
                    if (a.pixels[at] != b.pixels[at] || a.pixels[at + 1] != b.pixels[at + 1] ||
                        a.pixels[at + 2] != b.pixels[at + 2])
                        ++changes;
                }
            require(changes >= 32, "Hover, press, selection and focus must change actual row pixels at every scale");
        }
    }
    app.focus_id("amount");
    const auto track = app.view.box("amount-track").border_box;
    const auto amount = app.state.item().amount;
    app.input({0us, PointerDown{{1}, {track.origin.x + 100, track.origin.y + 6}}}, CommandSource::pointer);
    require(style("amount").background == Color{.8f, .87f, .98f, 1} && app.state.item().amount == amount,
            "A stationary Slider drag must show active feedback without changing its value");
    app.input({0us, PointerCancel{{1}}}, CommandSource::pointer);
    require(style("amount").background != Color{.8f, .87f, .98f, 1},
            "Slider cancellation must clear its active feedback");
    app.resize({492, 360});
    app.focus_id("amount");
    const auto pane = app.view.box("inspector").padding_box();
    const auto& control = app.view.box("amount").border_box;
    const auto& narrow_track = app.view.box("amount-track").border_box;
    require(narrow_track.origin.x >= pane.origin.x &&
                narrow_track.origin.x + narrow_track.size.width <= pane.origin.x + pane.size.width &&
                narrow_track.origin.x + narrow_track.size.width <= control.origin.x + control.size.width,
            "The complete Slider track must fit the 320-unit minimum Inspector pane");
    require(app.state.items == accepted && app.state.selected == "sphere",
            "Visual layout and drag feedback must preserve independent item data");
}

void gradient_card_smoke() {
    App app;
    const auto card = app.view.box("item-card").border_box;
    const auto fill =
        std::find_if(app.view.paint.commands.begin(), app.view.paint.commands.end(), [&](const DrawCommand& command) {
            const auto* rect = std::get_if<DrawRect>(&command);
            return rect && rect->rect == card && rect->gradient;
        });
    require(fill != app.view.paint.commands.end(), "Inspector card must paint a gradient through ordinary styles");
    auto rect = std::get<DrawRect>(*fill);
    const auto& gradient = std::get<RadialGradient>(*rect.gradient);
    require(gradient.stops.size() == 3 && gradient.stops[1].offset == .42f,
            "Inspector card must retain its authored nonuniform interior stop");
    rect.rect.origin = {};
    CpuFrameCapture frames;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        UiDrawList paint{{rect}};
        const auto image = take(frames.capture({0, card.size, scale, take(capture_extent(card.size, scale)), &paint}));
        require(image.pixels[3] == 0, "Inspector gradient must retain transparent rounded corners");
        const auto y = image.extent.height / 2;
        const auto left = (std::size_t(y) * image.extent.width + image.extent.width / 4) * 4;
        const auto right = (std::size_t(y) * image.extent.width + image.extent.width * 3 / 4) * 4;
        require(image.pixels[left] < image.pixels[right] && image.pixels[left + 3] == 255 &&
                    image.pixels[right + 3] == 255,
                "Inspector gradient must visibly change color across the card at every scale");
        auto shorthand = rect;
        auto& two = std::get<RadialGradient>(*shorthand.gradient);
        two.start_color = gradient.stops.front().color;
        two.end_color = gradient.stops.back().color;
        two.stops.clear();
        const UiDrawList two_colors{{shorthand}};
        const auto two_color_image =
            take(frames.capture({0, card.size, scale, take(capture_extent(card.size, scale)), &two_colors}));
        require(image != two_color_image, "Inspector's interior stop must change actual pixels, not only style data");
        auto linear = rect;
        linear.gradient = LinearGradient{.start = {0, 0}, .end = {1, 1}, .stops = gradient.stops};
        const UiDrawList linear_paint{{linear}};
        const auto linear_image =
            take(frames.capture({0, card.size, scale, take(capture_extent(card.size, scale)), &linear_paint}));
        require(image != linear_image, "Inspector must render a radial highlight rather than a linear substitute");
    }
}

void analytic_fill_smoke() {
    App app;
    const auto box = app.view.box("enabled-indicator").border_box;
    const auto style = app.view.styles[app.view.tree->find("enabled-indicator")->index];
    require(style.background_shape == FillShape::ellipse && style.background_antialias && box.size == Size{8, 8},
            "Enabled indicator must resolve an ordinary smooth circular background");
    CpuFrameCapture frames;
    auto smooth_paint = app.view.paint;
    std::erase_if(smooth_paint.commands,
                  [](const auto& command) { return std::holds_alternative<DrawGlyphRun>(command); });
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        auto hard_paint = smooth_paint;
        for (auto& command : hard_paint.commands)
            if (auto* fill = std::get_if<DrawRect>(&command)) fill->antialias = false;
        const auto extent = take(capture_extent(app.viewport, scale));
        const auto actual = take(frames.capture({0, app.viewport, scale, extent, &smooth_paint}));
        const auto hard = take(frames.capture({0, app.viewport, scale, extent, &hard_paint}));
        require(actual != hard, "Inspector analytic coverage must change actual decoration pixels at every scale");
    }
    const auto enabled = app.state.item().enabled;
    app.action("enabled");
    require(app.view.box("enabled-indicator").border_box == box &&
                app.view.styles[app.view.tree->find("enabled-indicator")->index].background != style.background &&
                app.state.item().enabled != enabled,
            "Enabled indicator must follow accepted edits without changing layout");
}

void shadow_card_smoke() {
    App app;
    // Leave room below the card, which now includes the Scale input and Visibility choices.
    app.resize({960, 540});
    const auto& box = app.view.box("item-card");
    const auto fill =
        std::find_if(app.view.paint.commands.begin(), app.view.paint.commands.end(), [&](const auto& command) {
            const auto* rect = std::get_if<DrawRect>(&command);
            return rect && rect->rect == box.border_box && rect->gradient;
        });
    require(fill != app.view.paint.commands.end() && fill != app.view.paint.commands.begin() &&
                std::holds_alternative<DrawShadow>(*(fill - 1)),
            "Inspector card must paint its shadow immediately before its ordinary gradient");
    const auto shadow = std::get<DrawShadow>(*(fill - 1));
    require(shadow.rect == Rect{{box.border_box.origin.x, box.border_box.origin.y + 4}, box.border_box.size} &&
                shadow.softness == 8 && shadow.corner_radius == 12 && shadow.color.a == .18f,
            "Inspector must retain the authored shadow geometry and opacity");
    CpuFrameCapture frames;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        UiDrawList plain{{DrawRect{{{}, app.viewport}, {.95f, .95f, .98f, 1}}}};
        if (box.clip) plain.commands.push_back(PushClip{*box.clip});
        auto decorated = plain;
        decorated.commands.push_back(shadow);
        plain.commands.push_back(*fill);
        decorated.commands.push_back(*fill);
        if (box.clip) {
            plain.commands.push_back(PopClip{});
            decorated.commands.push_back(PopClip{});
        }
        const auto extent = take(capture_extent(app.viewport, scale));
        const auto baseline = take(frames.capture({0, app.viewport, scale, extent, &plain}));
        const auto actual = take(frames.capture({0, app.viewport, scale, extent, &decorated}));
        std::size_t outside_changes = 0;
        for (unsigned y = 0; y < extent.height; ++y)
            for (unsigned x = 0; x < extent.width; ++x) {
                const auto at = (std::size_t(y) * extent.width + x) * 4;
                const double lx = (x + .5) / scale, ly = (y + .5) / scale;
                if (ly >= box.border_box.origin.y + box.border_box.size.height &&
                    actual.pixels[at] != baseline.pixels[at])
                    ++outside_changes;
                if (lx > box.border_box.origin.x + 12 &&
                    lx < box.border_box.origin.x + box.border_box.size.width - 12 &&
                    ly > box.border_box.origin.y + 12 && ly < box.border_box.origin.y + box.border_box.size.height - 12)
                    require(actual.pixels[at] == baseline.pixels[at],
                            "Opaque card fill must retain its pixels above the shadow");
            }
        require(outside_changes >= 16,
                "Inspector shadow must visibly darken actual pixels below the card at every scale");
    }
}

void inset_name_smoke() {
    App app;
    const auto box = app.view.box("name");
    const auto shadow_at =
        std::find_if(app.view.paint.commands.begin(), app.view.paint.commands.end(), [&](const auto& command) {
            const auto* shadow = std::get_if<DrawShadow>(&command);
            return shadow && shadow->inset && shadow->rect == box.border_box;
        });
    require(shadow_at != app.view.paint.commands.end() && shadow_at != app.view.paint.commands.begin() &&
                shadow_at + 1 != app.view.paint.commands.end() && std::holds_alternative<DrawRect>(*(shadow_at - 1)) &&
                std::holds_alternative<DrawBorder>(*(shadow_at + 1)),
            "Inspector name inset must paint between its fill and inside border");
    auto shadow = std::get<DrawShadow>(*shadow_at);
    auto fill = std::get<DrawRect>(*(shadow_at - 1));
    auto border = std::get<DrawBorder>(*(shadow_at + 1));
    require(shadow.softness == 4 && shadow.offset == Point{0, 1} && shadow.corner_radius == 6 && shadow.color.a == .2f,
            "Inspector name must retain its authored inset softness, direction, radius and alpha");
    shadow.rect.origin = fill.rect.origin = border.rect.origin = {4, 4};
    const Size viewport{shadow.rect.size.width + 8, shadow.rect.size.height + 8};
    const UiDrawList plain{{DrawRect{{{}, viewport}, {1, 1, 1, 1}}, fill, border}};
    const UiDrawList decorated{{DrawRect{{{}, viewport}, {1, 1, 1, 1}}, fill, shadow, border}};
    CpuFrameCapture frames;
    for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
        const auto extent = take(capture_extent(viewport, scale));
        const auto baseline = take(frames.capture({0, viewport, scale, extent, &plain}));
        const auto actual = take(frames.capture({0, viewport, scale, extent, &decorated}));
        std::size_t darkened = 0;
        for (unsigned y = 0; y < extent.height; ++y)
            for (unsigned x = 0; x < extent.width; ++x) {
                const auto at = (std::size_t(y) * extent.width + x) * 4;
                const double lx = (x + .5) / scale - 4, ly = (y + .5) / scale - 4;
                if (actual.pixels[at] < baseline.pixels[at]) ++darkened;
                if (lx < 0 || ly < 0 || lx >= shadow.rect.size.width || ly >= shadow.rect.size.height ||
                    (lx > 8 && ly > 8 && lx < shadow.rect.size.width - 8 && ly < shadow.rect.size.height - 8))
                    require(actual.pixels[at] == baseline.pixels[at],
                            "Name inset must preserve outside pixels and the readable center");
            }
        require(darkened >= 16, "Name inset must visibly darken actual interior pixels at every scale");
    }
    const auto accepted = app.state.items;
    app.focus_id("name");
    app.input({0us, PointerMove{{1}, app.view.center("name")}}, CommandSource::pointer);
    require(app.view.styles[app.view.tree->find("name")->index].box_shadow ==
                    BoxShadow{{0, 1}, 4, {.18f, .22f, .38f, .2f}, true} &&
                app.state.items == accepted && app.view.box("name").border_box == box.border_box,
            "Focus and hover must retain the inset, editing data and name geometry");
}

void outline_smoke() {
    App app;
    const auto items = app.state.items;
    for (const auto* id : {"name", "hierarchy-item-sphere"}) {
        const auto box = app.view.box(id).border_box;
        app.focus_id(id);
        require(app.view.box(id).border_box == box && app.state.items == items,
                "Focus outlines must preserve editing data and layout geometry");
        const auto& style = app.view.styles[app.view.tree->find(id)->index];
        require(style.outline && style.outline->width == 1.5f && style.outline->offset == 1,
                "Inspector focus must resolve an ordinary outside outline");
        const bool dashed = std::string_view(id).starts_with("hierarchy");
        require(style.outline->dash_length == (dashed ? 4.f : 0.f) && style.outline->gap_length == (dashed ? 2.f : 0.f),
                "Hierarchy focus must use a continuous rounded dash pattern distinct from control focus");
        const auto found =
            std::find_if(app.view.paint.commands.begin(), app.view.paint.commands.end(), [&](const auto& command) {
                const auto* stroke = std::get_if<DrawStroke>(&command);
                return stroke && stroke->rect.origin == Point{box.origin.x - 2.5f, box.origin.y - 2.5f};
            });
        require(found != app.view.paint.commands.end(), "Inspector focus must emit the smooth stroke command");
        auto stroke = std::get<DrawStroke>(*found);
        stroke.rect.origin = {4, 4};
        const Size viewport{stroke.rect.size.width + 8, stroke.rect.size.height + 8};
        const UiDrawList paint{{stroke}};
        CpuFrameCapture frames;
        for (float scale : {1.f, 1.25f, 1.5f, 2.f}) {
            const auto extent = take(capture_extent(viewport, scale));
            const auto image = take(frames.capture({0, viewport, scale, extent, &paint}));
            std::size_t partial = 0, opaque = 0;
            for (std::size_t at = 3; at < image.pixels.size(); at += 4) {
                partial += image.pixels[at] > 0 && image.pixels[at] < 255;
                opaque += image.pixels[at] == 255;
            }
            require(partial >= 16 && opaque >= 16,
                    "Inspector focus must have actual smooth edge pixels and visible interiors at every scale");
        }
        app.focus_id("actions");
        require(!app.view.styles[app.view.tree->find(id)->index].outline && app.state.items == items,
                "Moving focus must remove the previous outline without changing item data");
    }
}

void visibility_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    const auto semantics = [&] { return app.view.inspection.semantics.nodes; };
    const auto entry = [&](std::string_view id) {
        const auto nodes = semantics();
        const auto found =
            std::find_if(nodes.begin(), nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
        require(found != nodes.end(), "Visibility observation is missing");
        return *found;
    };
    app.action("sections-tab-display");
    const auto group = entry("visibility");
    const auto inherited = entry("visibility-option-inherited");
    require(group.role == SemanticRole::radio_group && group.name == "Visibility" &&
                inherited.role == SemanticRole::radio && inherited.name == "Inherited" && inherited.selected == true &&
                entry("visibility-option-hidden").selected == false,
            "Visibility must expose a named radio group with the accepted choice checked");
    app.focus_id("sections-tab-display");
    input(KeyDown{Key::tab});
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("visibility-option-inherited"),
            "Tab must enter Visibility at the checked option");
    const auto executions = app.executions;
    input(KeyDown{Key::down});
    require(app.state.item().visibility == "visible" && app.executions == executions + 1 &&
                take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("visibility-option-visible") &&
                entry("visibility-option-visible").selected == true &&
                entry("visibility-option-inherited").selected == false,
            "An arrow must move focus and select through one ordinary command");
    input(KeyDown{Key::up});
    input(KeyDown{Key::up});
    require(app.state.item().visibility == "hidden", "Arrows must wrap through the options");
    input(KeyDown{Key::tab, {.shift = true}});
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("sections-tab-display") &&
                app.state.item().visibility == "hidden",
            "Shift+Tab must leave the group without changing the choice");
    const auto point = app.view.reachable("visibility-option-visible");
    input(PointerDown{{1}, point}, CommandSource::pointer);
    input(PointerUp{{1}, point}, CommandSource::pointer);
    require(app.state.item().visibility == "visible" &&
                take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("visibility-option-visible"),
            "A pointer press must focus and select an option");
    const auto label = [&](std::string_view key) {
        return app.view.styles[app.view.tree->find("visibility-label-" + std::string(key))->index].color;
    };
    require(label("visible") != label("hidden"), "The checked option label must be emphasized");
    app.action("hierarchy-item-sphere");
    require(app.state.item().visibility == "inherited" && entry("visibility-option-inherited").selected == true,
            "Another item must show its own visibility");
    app.action("visibility-option-hidden");
    app.action("hierarchy-item-cube");
    require(app.state.item().visibility == "visible", "Returning to an item must retain its visibility");
    app.action("hierarchy-item-sphere");
    require(app.state.item().visibility == "hidden", "Each item must retain an independent visibility");
}
void shadows_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    app.action("sections-tab-display");
    const auto& on = app.observed("shadows");
    require(on.role == SemanticRole::switch_ && on.name == "Cast shadows" && on.selected == true &&
                on.actions == std::vector<SemanticAction>{{"activate", "item.shadows"}},
            "Shadows must expose a named switch with the accepted on state");
    const auto track = app.view.box("shadows-track").border_box;
    const auto on_thumb = app.view.box("shadows-thumb").border_box;
    const auto on_color = app.view.styles[app.view.tree->find("shadows-track")->index].background;
    app.focus_id("visibility-option-inherited");
    input(KeyDown{Key::tab});
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("shadows"),
            "Tab must reach the switch after Visibility");
    const auto executions = app.executions;
    input(Activate{});
    require(!app.state.item().shadows && app.executions == executions + 1 && app.observed_checked("shadows") == false,
            "Activate must toggle the switch through one ordinary command");
    // Focus reveal may scroll the card, so compare the thumb with its own track.
    const auto off_track = app.view.box("shadows-track").border_box;
    const auto off_thumb = app.view.box("shadows-thumb").border_box;
    require(off_track.size == track.size && off_thumb.origin.x - off_track.origin.x == 3 &&
                on_thumb.origin.x - track.origin.x == track.size.width - 3 - on_thumb.size.width &&
                off_thumb.origin.y - off_track.origin.y == on_thumb.origin.y - track.origin.y &&
                app.view.styles[app.view.tree->find("shadows-track")->index].background != on_color,
            "An off switch must move its thumb to the start and change its track color without resizing the track");
    input(Navigate{Direction::right});
    require(!app.state.item().shadows, "Arrows must not toggle the switch");
    const auto point = app.view.reachable("shadows-track");
    input(PointerDown{{1}, point}, CommandSource::pointer);
    input(PointerUp{{1}, point}, CommandSource::pointer);
    require(app.state.item().shadows && app.observed_checked("shadows") == true,
            "A pointer press on the track must toggle the switch");
    app.action("shadows");
    app.action("hierarchy-item-sphere");
    require(app.state.item().shadows && app.observed_checked("shadows") == true,
            "Another item must show its own shadows value");
    app.action("hierarchy-item-cube");
    require(!app.state.item().shadows, "Returning to an item must retain its shadows value");
}
void scale_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    const auto caption = [&] {
        return std::get<std::string>(app.view.tree->get(*app.view.tree->find("scale-caption"))->properties.at("text"));
    };
    const auto& initial = app.observed("scale");
    require(initial.role == SemanticRole::number_input && initial.name == "Scale" && initial.value == "1" &&
                initial.actions == std::vector<SemanticAction>{{"activate", "item.scale"}} && caption() == "Scale",
            "Scale must expose a named number input with its accepted text");
    app.focus_id("amount");
    input(KeyDown{Key::tab});
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("scale"),
            "Tab must reach Scale after Amount");
    auto executions = app.executions;
    input(KeyDown{Key::up});
    require(app.state.item().scale == 1.1 && app.state.item().scale_draft.text == "1.1" &&
                app.observed_value("scale") == "1.1" && app.executions == executions + 1,
            "Up must step and accept through one ordinary command");
    input(KeyDown{Key::down});
    input(KeyDown{Key::down});
    require(app.state.item().scale == .9 && app.observed_value("scale") == "0.9",
            "Down must step without binary noise");

    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"2.5"});
    require(app.state.item().scale == .9 && app.state.item().scale_draft.text == "2.5" &&
                app.observed_value("scale") == "2.5" && caption() == "Scale *",
            "Typing must change only the draft and mark it pending");
    input(KeyDown{Key::home});
    input(KeyDown{Key::end});
    input(KeyDown{Key::backspace});
    input(tessera::TextInput{"7"});
    require(app.state.item().scale_draft.text == "2.7", "Draft editing keys must edit the Scale text");
    executions = app.executions;
    input(KeyDown{Key::enter});
    require(app.state.item().scale == 2.7 && app.state.item().scale_draft.caret == 3 && caption() == "Scale" &&
                app.executions == executions + 1 && app.feedback.empty(),
            "Enter must apply a valid draft");

    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"2,5"});
    const auto invalid = app.state.item();
    executions = app.executions;
    for (const auto key : {Key::enter, Key::up}) {
        input(KeyDown{key});
        require(app.state.item() == invalid && app.executions == executions &&
                    app.edit_diagnostics.front().code == "number_syntax" &&
                    app.feedback == "Scale could not finish. Enter a number from 0 to 10." &&
                    app.observed_value("scale") == "2,5",
                "An invalid draft must reject apply and step, keeping its text and the accepted value");
    }
    input(KeyDown{Key::escape});
    require(app.state.item().scale == 2.7 && app.state.item().scale_draft.text == "2.7" && app.feedback.empty() &&
                caption() == "Scale",
            "Escape must restore the accepted text and clear feedback");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"12"});
    input(KeyDown{Key::enter});
    require(app.state.item().scale == 2.7 && app.edit_diagnostics.front().code == "number_out_of_range",
            "An out-of-range draft must be rejected");
    input(KeyDown{Key::backspace});
    input(KeyDown{Key::enter});
    input(KeyDown{Key::up});
    require(app.state.item().scale == 1.1, "A corrected draft must apply and step");

    app.focus_id("name");
    for (const auto* part : {"scale-increase", "scale-increase", "scale-decrease"}) {
        const auto point = app.view.reachable(part);
        executions = app.executions;
        input(PointerDown{{1}, point}, CommandSource::pointer);
        input(PointerUp{{1}, point}, CommandSource::pointer);
        require(app.executions == executions + 1, "A step part must step once per press");
    }
    require(app.state.item().scale == 1.2 && app.observed_value("scale") == "1.2" &&
                take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("scale"),
            "Step parts must focus Scale and step from its accepted text");
    const auto field = app.view.reachable("scale-field");
    executions = app.executions;
    input(PointerDown{{1}, field}, CommandSource::pointer);
    input(PointerUp{{1}, field}, CommandSource::pointer);
    require(app.executions == executions && app.state.item().scale == 1.2, "Clicking the field must only focus it");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"10"});
    input(KeyDown{Key::enter});
    input(KeyDown{Key::up});
    require(app.state.item().scale == 10 && app.observed_value("scale") == "10", "Steps must clamp to the maximum");
    input(KeyDown{Key::backspace});
    const auto pending = app.state.item();

    app.action("hierarchy-item-sphere");
    require(app.state.item().scale == 1 && app.observed_value("scale") == "1", "Another item must show its own Scale");
    app.action("hierarchy-item-cube");
    require(app.state.item() == pending && app.observed_value("scale") == "1" && caption() == "Scale *",
            "Returning to an item must retain its accepted Scale and pending draft");
}
void sections_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    const auto focused = [&](std::string_view id) {
        return take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id);
    };
    const auto& list = app.observed("sections-list");
    require(list.role == SemanticRole::tab_list && list.name == "Property sections",
            "Sections must expose a named tab list");
    const auto& general = app.observed("sections-tab-general");
    require(general.role == SemanticRole::tab && general.name == "General" && general.selected == true &&
                app.observed_checked("sections-tab-display") == false && app.view.tree->find("name") &&
                !app.view.tree->find("visibility"),
            "General must be selected and show only its properties");
    app.focus_id("selected");
    input(KeyDown{Key::tab});
    require(focused("sections-tab-general"), "Tab must enter the sections at the selected tab");
    input(KeyDown{Key::tab});
    require(focused("name"), "Tab must continue into the shown panel");
    input(KeyDown{Key::tab, {.shift = true}});
    require(focused("sections-tab-general"), "Shift+Tab must return to the selected tab");
    const auto indicator = [&](std::string_view id) {
        return app.view.styles[app.view.tree->find(id)->index].border_color;
    };
    const auto executions = app.executions;
    input(KeyDown{Key::right});
    require(app.state.section == "display" && app.executions == executions + 1 && focused("sections-tab-display") &&
                app.observed_checked("sections-tab-display") == true && !app.view.tree->find("name") &&
                app.view.tree->find("visibility") && app.view.tree->find("shadows"),
            "Right must select Display through one command and replace the panel");
    require(indicator("sections-tab-display") != indicator("sections-tab-general"),
            "Only the selected tab shows its indicator");
    input(KeyDown{Key::tab});
    require(focused("visibility-option-inherited"), "Tab must reach the Display panel");
    app.focus_id("sections-tab-display");
    input(KeyDown{Key::right});
    require(app.state.section == "general" && focused("sections-tab-general"), "Right must wrap to General");
    input(KeyDown{Key::left});
    require(app.state.section == "display", "Left must wrap to Display");
    input(KeyDown{Key::down});
    require(focused("visibility-option-inherited") && app.state.section == "display",
            "Down must leave the tabs into the panel without selecting");
    const auto point = app.view.reachable("sections-tab-general");
    input(PointerDown{{1}, point}, CommandSource::pointer);
    input(PointerUp{{1}, point}, CommandSource::pointer);
    require(app.state.section == "general" && focused("sections-tab-general"),
            "A pointer press must focus and select a tab");
    app.action("sections-tab-display");
    app.action("hierarchy-item-sphere");
    require(app.state.section == "display" && app.view.tree->find("visibility"),
            "The shown section must survive item selection");
    app.focus_id("visibility-option-inherited");
    app.action("tool-rename");
    require(app.state.section == "general" && focused("name"), "Rename must show General and focus the name editor");
}
void toolbar_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    const auto focused = [&](std::string_view id) {
        return take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id);
    };
    const auto& tools = app.observed("item-tools");
    require(tools.role == SemanticRole::toolbar && tools.name == "Item tools" && tools.actions.empty() &&
                app.observed("tool-reset").role == SemanticRole::button &&
                app.view.inspection.semantics.nodes[app.observed("tool-reset").parent].id == "item-tools",
            "The header must expose a named toolbar of ordinary buttons");
    app.focus_id("hierarchy-item-scene");
    input(KeyDown{Key::tab, {.shift = true}});
    require(focused("actions"), "Shift+Tab must enter the toolbar at its first control");
    input(KeyDown{Key::tab});
    require(focused("hierarchy-item-scene"), "Tab must leave the toolbar after one stop");
    app.focus_id("actions");
    const auto executions = app.executions;
    input(KeyDown{Key::right});
    require(focused("tool-rename") && app.executions == executions && app.state.menu == OpenMenu::none,
            "Right must move to Rename without running a command");
    input(KeyDown{Key::right});
    input(KeyDown{Key::right});
    require(focused("actions") && app.executions == executions, "Right must wrap to the first tool");
    input(KeyDown{Key::left});
    require(focused("tool-reset"), "Left must wrap to the last tool");
    input(KeyDown{Key::home});
    require(focused("actions"), "Home must reach the first tool");
    input(KeyDown{Key::end});
    require(focused("tool-reset"), "End must reach the last tool");

    const auto name = app.state.item().name;
    app.action("enabled");
    app.action("amount");
    require(app.try_scale(ScaleStep{5}).status == CommandOutcomeStatus::success, "Scale must step before reset");
    app.action("sections-tab-display");
    app.action("visibility-option-hidden");
    app.action("shadows");
    const auto edited = app.state.item();
    require(!edited.enabled && edited.amount == 55 && edited.scale == 1.5 && edited.visibility == "hidden" &&
                !edited.shadows,
            "Reset fixture must change every property");
    app.focus_id("tool-reset");
    const auto before_reset = app.executions;
    input(Activate{});
    const auto reset = app.state.item();
    require(app.executions == before_reset + 1 && reset.name == name && reset.enabled && reset.amount == 50 &&
                reset.scale == 1 && reset.scale_draft.text == "1" &&
                reset.scale_draft.revision == edited.scale_draft.revision + 1 && reset.visibility == "inherited" &&
                reset.shadows && focused("tool-reset"),
            "Reset must restore every property except the name through one command");
    app.action("shadows");
    app.action("hierarchy-item-sphere");
    app.action("shadows");
    const auto point = app.view.reachable("tool-reset");
    input(PointerDown{{1}, point}, CommandSource::pointer);
    input(PointerUp{{1}, point}, CommandSource::pointer);
    require(app.state.item().shadows, "A pointer press must reset the selected item");
    app.action("hierarchy-item-cube");
    require(!app.state.item().shadows, "Reset must leave other items unchanged");
    app.focus_id("tool-rename");
    input(Activate{});
    require(app.state.section == "general" && focused("name") && app.state.menu == OpenMenu::none,
            "The toolbar Rename shares the menu command");
    app.focus_id("actions");
    input(Activate{});
    require(app.state.menu == OpenMenu::actions, "The menu trigger keeps its command inside the toolbar");
}
void placement_smoke() {
    App app;
    // Leave room below the card for every General row.
    app.resize({960, 540});
    const auto spans = [&](const char* when) {
        const auto grid = app.view.box("properties").content_box();
        const auto hint = app.view.box("scale-hint").border_box;
        const auto scale = app.view.box("scale").border_box;
        const auto name = app.view.box("name").border_box;
        // Starts in the label column and ends with the control column.
        require(hint.origin.x == grid.origin.x && name.origin.x > grid.origin.x &&
                    hint.origin.x + hint.size.width == name.origin.x + name.size.width &&
                    hint.origin.y >= scale.origin.y + scale.size.height + 12,
                when);
    };
    spans("The Scale hint must span the label and control columns in its own row");
    app.resize({492, 540});
    spans("The spanning hint must follow the narrow Inspector columns");
    app.resize({960, 540});
    require(!app.view.tree->find("modified-badge"), "An item at its reset values must show no Modified badge");
    const auto card = app.view.box("item-card").border_box;
    const auto selected = app.view.box("selected").border_box;
    const auto sections = app.view.box("sections").border_box;
    const auto glyphless = [&] {
        auto paint = app.view.paint;
        std::erase_if(paint.commands,
                      [](const auto& command) { return std::holds_alternative<DrawGlyphRun>(command); });
        return paint;
    };
    CpuFrameCapture frames;
    const auto plain_paint = glyphless();
    const auto plain = take(frames.capture({0, app.viewport, 1, take(capture_extent(app.viewport, 1)), &plain_paint}));
    app.action("enabled");
    const auto& placed = app.view.box("item-card");
    const auto badge = app.view.box("modified-badge").border_box;
    const auto padding = placed.padding_box();
    require(placed.border_box == card && app.view.box("selected").border_box == selected &&
                app.view.box("sections").border_box == sections,
            "The absolute badge must not move the card or its flow content");
    require(badge.origin.y == padding.origin.y - 9 &&
                badge.origin.x + badge.size.width == padding.origin.x + padding.size.width - 12 &&
                badge.origin.y < card.origin.y && badge.origin.y + badge.size.height <= selected.origin.y,
            "The badge must straddle the card's top edge at its trailing offset without covering Selected");
    const auto paint = glyphless();
    const auto image = take(frames.capture({0, app.viewport, 1, take(capture_extent(app.viewport, 1)), &paint}));
    const auto pixel = [](const CapturedImage& from, float x, float y) {
        const auto at = (std::size_t(y) * from.extent.width + std::size_t(x)) * 4;
        return std::array<std::uint8_t, 4>{from.pixels[at], from.pixels[at + 1], from.pixels[at + 2],
                                           from.pixels[at + 3]};
    };
    const Point edge{badge.origin.x + 3, card.origin.y};
    const Point inside{badge.origin.x + 3, badge.origin.y + badge.size.height / 2};
    require(pixel(image, edge.x, edge.y) == pixel(image, inside.x, inside.y) &&
                pixel(image, edge.x, edge.y) != pixel(plain, edge.x, edge.y),
            "The later badge must paint over the card border it straddles");
    require(take(hit_test(app.view.hit(), {edge.x, edge.y + 1})).target == app.view.tree->find("modified-badge"),
            "Pointer hits on the badge must reach the badge, not the card below");
    app.action("hierarchy-item-sphere");
    require(!app.view.tree->find("modified-badge"), "The badge must follow the selected item's own values");
    app.action("hierarchy-item-cube");
    require(app.view.tree->find("modified-badge").has_value(), "The badge must return with the modified item");
    app.action("tool-reset");
    require(!app.view.tree->find("modified-badge") && app.view.box("item-card").border_box == card,
            "Reset must clear the badge without moving the card");
}
void smoke() {
    sections_smoke();
    placement_smoke();
    toolbar_smoke();
    scale_smoke();
    visibility_smoke();
    shadows_smoke();
    appearance_smoke();
    gradient_card_smoke();
    analytic_fill_smoke();
    shadow_card_smoke();
    inset_name_smoke();
    outline_smoke();
    edit_rejection_smoke();
    composition_smoke();
    clipboard_smoke();
    cut_smoke();
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    auto click = [&](std::string_view id) {
        const auto point = app.view.reachable(id);
        input(PointerDown{{1}, point}, CommandSource::pointer);
        input(PointerUp{{1}, point}, CommandSource::pointer);
    };
    ReplayRecording recording;
    recording.context = app.context;
    recording.viewport = app.viewport;
    recording.document = app.view.tree->document();
    recording.styles = app.view.styles;
    auto replay = take(ReplaySession::open(recording, app.text));
    auto record = [&] { (void)take(replay.apply(app.recorded_view())); };
    const auto cube_point = app.view.center("hierarchy-item-cube");
    input(PointerMove{{1}, cube_point}, CommandSource::pointer);
    record();
    input(PointerDown{{1}, cube_point}, CommandSource::pointer);
    record();
    input(PointerUp{{1}, cube_point}, CommandSource::pointer);
    record();
    app.focus_id("name");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"Edited cube"});
    const auto edited_name = app.state.item().name;
    require(app.observed_name("hierarchy-item-cube") == "Edited cube" && app.observed_value("name") == "Edited cube",
            "Accepted name must agree between Hierarchy and Inspector");
    click("enabled");
    app.focus_id("amount");
    input(KeyDown{Key::right});
    const auto track = app.view.box("amount-track").border_box;
    const Point amount_point{track.origin.x + 4 + .75f * (track.size.width - 8),
                             track.origin.y + track.size.height / 2};
    input(PointerDown{{1}, amount_point}, CommandSource::pointer);
    input(PointerUp{{1}, amount_point}, CommandSource::pointer);
    require(app.state.item().amount == 75, "Pointer Slider input must accept the requested stop");
    for (int i = 0; i < 4; ++i) input(KeyDown{Key::left});
    const auto cube = app.state.item();
    require(!cube.enabled && cube.amount == 55, "Checkbox and numeric edits must be accepted");
    record();
    (void)take(replay.apply(ReplaySemanticAction{AuthorIdTarget{"hierarchy-item-sphere"}}));
    click("hierarchy-item-sphere");
    require(app.state.selected == "sphere" && app.state.item().name.text == "Sphere",
            "Selection must show independent values");
    app.focus_id("name");
    input(tessera::TextInput{"!"});
    input(KeyDown{Key::home, {.shift = true}});
    const auto sphere = app.state.item();
    record();
    app.focus_id("hierarchy-item-cube");
    input(Activate{}, CommandSource::gamepad);
    require(app.state.item() == cube && app.state.item().name == edited_name,
            "Returning to an item must retain name, selection, revision, enabled and amount");
    app.action("hierarchy-item-sphere");
    require(app.state.item() == sphere && sphere.name.anchor != sphere.name.caret,
            "Returning to another item must retain its independent noncollapsed byte selection");
    app.action("hierarchy-item-cube");
    const auto stale = app.try_edit({sphere.name.owner, sphere.name.revision, TextEditInsert{"wrong"}});
    require(stale.status == CommandOutcomeStatus::error && app.state.item() == cube,
            "An edit from another selected item must reject without changing values");
    app.text.reject = true;
    const auto accepted_generation = app.view.inspection.generation;
    const auto rejected = app.try_edit({cube.name.owner, cube.name.revision, TextEditInsert{"retry"}});
    require(rejected.status == CommandOutcomeStatus::error && app.state.item() == cube &&
                app.view.inspection.generation == accepted_generation,
            "Rejected presentation must retain the accepted item and inspection");
    app.text.reject = false;
    const auto retry = app.try_edit({cube.name.owner, cube.name.revision, TextEditInsert{"!"}});
    require(retry.status == CommandOutcomeStatus::success && app.state.item().name.text == "Edited cube!",
            "Rejected name edits must remain retryable");
    const auto saved = app.state.item();
    app.focus_id("hierarchy-item-scene");
    input(Navigate{Direction::left}, CommandSource::gamepad);
    require(!app.state.expanded && app.state.selected == "cube" && app.state.item() == saved,
            "Collapse must retain a selected descendant and its edits");
    input(Navigate{Direction::right}, CommandSource::gamepad);
    app.resize({520, 220});
    app.focus_id("amount");
    input(Scroll{app.view.center("inspector"), {0, -1000}}, CommandSource::pointer);
    require(app.view.box("inspector").scroll->offset.y == 0, "Inspector must scroll back to the top");
    input(Scroll{app.view.center("inspector"), {0, 80}}, CommandSource::pointer);
    require(app.view.box("inspector").scroll->offset.y > 0, "Inspector wheel input must change the accepted offset");
    input(Scroll{app.view.center("hierarchy"), {0, 1000}}, CommandSource::pointer);
    require(app.view.box("hierarchy").scroll->offset.y > 0 && app.state.selected == "cube",
            "Hierarchy scrolling must retain the selected item");
    input(Scroll{app.view.center("hierarchy"), {0, -1000}}, CommandSource::pointer);
    app.focus_id("panes-handle");
    const auto split_before = app.state.split;
    input(KeyDown{Key::left});
    require(app.state.split == split_before - 8, "Keyboard pane resize must accept the requested step");
    app.resize({960, 360});
    const auto pointer_split_before = app.state.split;
    const auto handle = app.view.center("panes-handle");
    input(PointerDown{{1}, handle}, CommandSource::pointer);
    input(PointerMove{{1}, {handle.x + 30, handle.y}}, CommandSource::pointer);
    input(PointerUp{{1}, {handle.x + 30, handle.y}}, CommandSource::pointer);
    require(app.state.split == pointer_split_before + 30, "Pointer pane resize must retain the drag distance");
    require(app.state.item() == saved && app.state.selected == "cube",
            "Scroll and pane/window resizing must retain edits");
    click("actions");
    click("actions-menu-item-enabled");
    require(app.state.item().enabled && app.state.menu == OpenMenu::none,
            "Menu must route the ordinary toggle command");
    app.focus_id("selected");
    input(KeyDown{Key::f10, {.shift = true}});
    require(app.state.menu == OpenMenu::context, "Shift+F10 must open selected-item actions");
    input(KeyDown{Key::escape});
    require(app.state.menu == OpenMenu::none, "Escape must dismiss without changing item values");
    app.focus_id("selected");
    const auto position = app.view.center("selected");
    input(PointerDown{{1}, position, PointerButton::secondary}, CommandSource::pointer);
    input(PointerUp{{1}, position, PointerButton::secondary}, CommandSource::pointer);
    require(app.state.menu == OpenMenu::context, "Context action must open at the selected item");
    click("item-menu-item-rename");
    require(take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("name"),
            "Rename command must focus the retained name editor");
    app.focus_id("selected");
    input(KeyDown{Key::f10, {.shift = true}});
    app.resize({520, 220});
    const auto inspector = app.view.box("inspector").border_box;
    input(Scroll{{inspector.origin.x + inspector.size.width - 4, inspector.origin.y + inspector.size.height - 4},
                 {0, 1000}},
          CommandSource::pointer);
    require(app.state.menu == OpenMenu::none, "Scrolling the context anchor away must dismiss its menu");
    app.resize({960, 360});
    app.focus_id("name");
    record();
    const auto target = take(resolve_target(app.view.inspection, AuthorIdTarget{"hierarchy-item-cube"}));
    const auto semantic = app.view.inspection.elements[target.index].semantic;
    require(semantic && app.view.inspection.semantics.nodes[*semantic].selected == true &&
                app.observed_name("enabled") == "Enabled" && app.observed_checked("enabled") == true &&
                app.observed_name("amount").find("55") != std::string::npos,
            "Shared inspection and semantics must expose selection and edited properties");
    const auto loaded = take(load_replay(take(save_replay(replay.recording()))));
    const auto output = take(play_replay(loaded, app.text));
    require(compare_replay(replay.output(), output).empty() && output.actions.size() == 1 &&
                output.actions.front().action == "select.sphere",
            "Recorded host views and selection action must reproduce through ordinary replay");
    const auto& nodes = output.semantics.back().nodes;
    require(std::any_of(nodes.begin(), nodes.end(),
                        [](const auto& node) {
                            return node.id == "hierarchy-item-cube" && node.selected == true &&
                                   node.name == "Edited cube!";
                        }),
            "Replay must retain the selected item's edited display name");
    for (const auto& [id, value] :
         std::vector<std::pair<std::string, std::string>>{{"enabled", "Enabled"}, {"amount", "55"}})
        require(
            std::any_of(nodes.begin(), nodes.end(),
                        [&](const auto& node) { return node.id == id && node.name.find(value) != std::string::npos; }),
            "Replay must reproduce the accepted enabled and numeric properties");
    std::cout
        << "Inspector: select/edit/return, independent items, rejected staging, input, menus, inspection and replay passed.\n";
}

#ifdef _WIN32
int native(bool smoke) {
    App app;
    settings_native::Glyphs glyphs;
    GlyphCache cache(glyphs);
    CpuFrameCapture frames(&cache);
    app.prepare_paint = [&](const UiDrawList& paint) { return prepare_glyph_atlas(paint, cache, 1, {1}).diagnostics; };
    settings_native::Window window;
    app.clipboard_read = [&] { return inspector_clipboard::read(window.handle); };
    app.clipboard_write = [&](std::string_view text) { return inspector_clipboard::write(window.handle, text); };
    window.title = L"Tessera Hierarchy / Inspector - select an item, edit its properties";
    glyphs.cells = inspector_text::cells;
    const auto feedback = [&](const std::string& previous) {
        if (app.feedback == previous) return;
        // Feedback uses fixed ASCII phrases and remains available if text measurement failed.
        const auto title = app.feedback.empty() ? std::wstring(window.title)
                                                : L"Tessera Hierarchy / Inspector - " +
                                                      std::wstring(app.feedback.begin(), app.feedback.end());
        SetWindowTextW(window.handle, title.c_str());
    };
    window.input = [&](InputEvent event, CommandSource source) {
        const auto previous = app.feedback;
        app.input(std::move(event), source);
        feedback(previous);
    };
    window.composition = [&](settings_native::Window::ImeEvent event) {
        using Kind = settings_native::Window::ImeKind;
        const auto previous = app.feedback;
        app.ime({event.kind == Kind::start    ? ImeStep::Kind::start
                 : event.kind == Kind::update ? ImeStep::Kind::update
                 : event.kind == Kind::commit ? ImeStep::Kind::commit
                                              : ImeStep::Kind::cancel,
                 std::move(event.text), event.caret});
        feedback(previous);
    };
    window.caret = [&] { return app.caret_rect(); };
    window.capture = [&] {
        return take(frames.capture({app.view.commands.generation(), app.viewport, 1,
                                    take(capture_extent(app.viewport, 1)), &app.view.paint, 0us}));
    };
    window.focused = [&]() -> std::optional<Rect> {
        const auto focused = take(app.focus.refresh(app.view.hit())).focused;
        if (!focused || !take(scroll_into_view(app.view.hit(), *focused)).empty()) return {};
        return app.view.box(*app.view.tree->get(*focused)->id).border_box;
    };
    // Both inputs take committed WM_CHAR text and Return; only the name editor attaches the IME.
    window.editing = [&] { return app.focused_on("name") || app.focused_on("scale"); };
    window.ime_editing = [&] { return app.focused_on("name"); };
    window.resized = [&](Size viewport) { app.resize(viewport); };
    window.resize_cursor = [&](Point point) {
        const auto geometry = take(split_pane_geometry(app.view.hit(), "panes"));
        return geometry && geometry->enabled &&
               (app.resizing() || take(hit_test(app.view.hit(), point)).target == geometry->handle);
    };
    auto verify = [&](settings_native::Window& host) {
        // Exercise key mapping without changing the user's live OS clipboard.
        std::string clipboard;
        app.clipboard_read = [&]() -> Result<std::string> { return {clipboard, {}}; };
        app.clipboard_write = [&](std::string_view text) {
            clipboard = text;
            return std::vector<Diagnostic>{};
        };
        auto control_key = [&](WPARAM key, LPARAM flags = 1) {
            BYTE original[256]{}, modified[256]{};
            require(GetKeyboardState(original) != FALSE, "Cannot read smoke keyboard state");
            std::copy(std::begin(original), std::end(original), std::begin(modified));
            modified[VK_CONTROL] = 0x80;
            require(SetKeyboardState(modified) != FALSE, "Cannot inject smoke Control key");
            try {
                host.message(WM_KEYDOWN, key, flags);
            } catch (...) {
                SetKeyboardState(original);
                throw;
            }
            require(SetKeyboardState(original) != FALSE, "Cannot restore smoke keyboard state");
        };
        const std::wstring unicode{L'\x65e5', L'\x672c', L'\xd83d', L'\xde00'};
        const char* japanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"; // Japanese kanji
        tessera::GlyphRasterRequest wide{{0}, 0x65e5, 16 * 64}, narrow{{0}, 'N', 16 * 64};
        require(take(glyphs.rasterize(wide)).width == 2 * take(glyphs.rasterize(narrow)).width,
                "Native glyphs must give wide Japanese scalars two cells");
        const std::string utf8 = "\xe6\x97\xa5\xe6\x9c\xac\xf0\x9f\x98\x80";
        require(take(inspector_clipboard::decode(unicode)) == utf8 &&
                    take(inspector_clipboard::encode(utf8)) == unicode &&
                    !inspector_clipboard::decode(std::wstring{L'\xd800'}) &&
                    !inspector_clipboard::encode(std::string("\xc0\xaf", 2)) &&
                    !inspector_clipboard::decode(std::wstring(1400, L'\x65e5')) &&
                    !inspector_clipboard::encode(std::string("a\0b", 3)),
                "Native clipboard conversion must preserve Unicode and reject invalid or oversized values");
        auto click = [&](const char* id) {
            const auto point = host.physical(app.view.reachable(id));
            host.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
            host.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        };
        // Wheel a scroll view back to its top without moving focus.
        auto wheel_top = [&](const char* id) {
            const auto center = host.physical(app.view.center(id));
            POINT screen{center.x, center.y};
            require(ClientToScreen(host.handle, &screen) != FALSE, "Cannot map native wheel position");
            host.message(WM_MOUSEWHEEL, MAKEWPARAM(0, 20 * WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
            require(app.view.box(id).scroll->offset.y == 0, "Native wheel must reach the top of the scroll view");
        };
        auto seek = [&](const char* id, bool backward = false) {
            for (int i = 0; i < 32; ++i) {
                if (take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id)) return;
                if (backward) {
                    BYTE original[256]{}, modified[256]{};
                    require(GetKeyboardState(original) != FALSE, "Cannot read smoke keyboard state");
                    std::copy(std::begin(original), std::end(original), std::begin(modified));
                    modified[VK_SHIFT] = 0x80;
                    require(SetKeyboardState(modified) != FALSE, "Cannot inject smoke Shift key");
                    try {
                        host.message(WM_KEYDOWN, VK_TAB, 1);
                    } catch (...) {
                        SetKeyboardState(original);
                        throw;
                    }
                    require(SetKeyboardState(original) != FALSE, "Cannot restore smoke keyboard state");
                } else {
                    host.message(WM_KEYDOWN, VK_TAB, 1);
                }
            }
            throw std::runtime_error("Native Tab cannot reach Inspector control");
        };
        const auto caption = [&] {
            wchar_t title[256]{};
            require(GetWindowTextW(host.handle, title, 256) > 0, "Cannot read Inspector feedback");
            return std::wstring(title);
        };
        require(host.presentations > 0, "Inspector window must present");
        click("hierarchy-item-cube");
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.selected == "sphere", "Native arrow/Activate must select the next hierarchy item");
        host.message(WM_KEYDOWN, VK_UP, 1);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.selected == "cube", "Native hierarchy navigation must return to the previous item");
        seek("name");
        host.message(WM_KEYDOWN, VK_END, 1);
        const auto before_edit_rejection = app.state.item().name;
        const auto edit_generation = app.view.commands.generation();
        app.text.reject = true;
        host.message(WM_CHAR, '?');
        require(app.state.item().name == before_edit_rejection && app.view.commands.generation() == edit_generation &&
                    caption().find(L"Name edit could not finish.") != std::wstring::npos && window.editing(),
                "Rejected native typing must retain the editor and show feedback without closing");
        app.text.reject = false;
        host.message(WM_CHAR, '!');
        require(app.edit_diagnostics.empty() && caption() == window.title,
                "Successful native typing must clear rejection feedback");
        control_key('A');
        const auto before_copy = app.state.item().name;
        control_key('C');
        host.message(WM_CHAR, 3);
        require(clipboard == "Cube!" && app.state.item().name == before_copy,
                "Native Control+C must copy the selection without inserting its WM_CHAR");
        const auto before_paste = app.executions;
        clipboard = utf8;
        control_key('V');
        host.message(WM_CHAR, 22);
        control_key('V', 1 | (LPARAM{1} << 30));
        require(app.state.item().name.text == utf8 && app.executions == before_paste + 1,
                "Native Control+V must replace once and ignore character/repeat messages");
        control_key('A');
        const auto before_cut = app.state.item().name;
        const auto cut_executions = app.executions;
        control_key('X');
        host.message(WM_CHAR, 24);
        control_key('X', 1 | (LPARAM{1} << 30));
        require(clipboard == utf8 && app.state.item().name.text.empty() &&
                    app.state.item().name.revision == before_cut.revision + 1 && app.state.item().name.anchor == 0 &&
                    app.state.item().name.caret == 0 && app.executions == cut_executions + 1,
                "Native Control+X must cut Unicode once and suppress character/repeat messages");
        control_key('V');
        require(app.state.item().name.text == utf8, "Native paste must restore the cut Unicode text");
        clipboard = "bad\ntext";
        const auto before_rejection = app.state.item().name;
        control_key('V');
        require(app.state.item().name == before_rejection && !app.clipboard_diagnostics.empty() &&
                    caption().find(L"Use a single line") != std::wstring::npos,
                "Rejected native paste must leave the application usable");
        control_key('A');
        const auto before_atlas_rejection = app.state.item().name;
        const auto atlas_generation = app.view.commands.generation();
        clipboard = std::string(4096, 'n');
        control_key('V');
        require(app.state.item().name == before_atlas_rejection && app.view.commands.generation() == atlas_generation &&
                    app.clipboard_diagnostics.front().code == "glyph_limit" &&
                    caption().find(L"Paste could not finish.") != std::wstring::npos,
                "Native atlas rejection must retain the accepted presentation before publication");
        // A full UTF-8 buffer whose scalar count fits the native frame's atlas budget.
        std::string full_unicode;
        for (int i = 0; i < 1024; ++i) full_unicode += "\xf0\x9f\x98\x80";
        require(
            app.try_edit({before_atlas_rejection.owner, before_atlas_rejection.revision, TextEditInsert{full_unicode}})
                    .status == CommandOutcomeStatus::success,
            "Native rejection fixture must fill the name buffer");
        const auto full_name = app.state.item().name;
        const auto full_generation = app.view.commands.generation();
        host.message(WM_CHAR, '!');
        require(app.state.item().name == full_name && app.view.commands.generation() == full_generation &&
                    caption().find(L"4096 UTF-8 bytes") != std::wstring::npos && window.editing(),
                "Typing beyond the name limit must show feedback and retain accepted text");
        host.message(WM_KEYDOWN, VK_BACK, 1);
        require(app.state.item().name.text.size() == 4092 && app.edit_diagnostics.empty() && caption() == window.title,
                "Backspace after a rejected native edit must recover and clear feedback");
        control_key('A');
        clipboard = "Cube!";
        control_key('V');
        // Injected IME session: the window reads strings from these services instead of an IME.
        std::wstring composition_string, result_string;
        LONG cursor = 0;
        std::vector<DWORD> notifications;
        host.ime_string = [&](DWORD kind) -> std::optional<std::wstring> {
            return kind == GCS_RESULTSTR ? result_string : composition_string;
        };
        host.ime_cursor = [&] { return cursor; };
        host.ime_notify = [&](DWORD action) { notifications.push_back(action); };
        control_key('A');
        host.message(WM_CHAR, 'N');
        require(host.ime_attached() && !host.composing(), "The focused name editor must attach the IME");
        host.message(WM_IME_STARTCOMPOSITION);
        composition_string = L"\x306b\x307b\x3093\x3054"; // nihongo preedit
        cursor = 4;
        host.message(WM_IME_COMPOSITION, 0, GCS_COMPSTR | GCS_CURSORPOS);
        require(host.composing() && app.state.item().name.text == "N" &&
                    app.state.item().name.composition->preedit == "\xe3\x81\xab\xe3\x81\xbb\xe3\x82\x93\xe3\x81\x94" &&
                    app.state.item().name.composition->caret == 12,
                "Native composition must deliver UTF-8 preedit and caret without committing");
        const auto preedit_caret = *app.caret_rect();
        const auto placed = host.physical(preedit_caret.origin);
        require(host.ime_area && host.ime_area->left == placed.x && host.ime_area->top == placed.y,
                "Native candidate placement must use the logical preedit caret");
        const auto during = app.state.item().name;
        host.message(WM_KEYDOWN, VK_TAB, 1);
        host.message(WM_CHAR, 'z');
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.item().name == during && window.editing(),
                "Keys and characters during composition must stay with the IME");
        result_string = L"\x65e5\x672c\x8a9e"; // Japanese kanji
        host.message(WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
        host.message(WM_IME_ENDCOMPOSITION);
        require(!host.composing() && !app.composing() && app.state.item().name.text == "N" + std::string(japanese),
                "Native result must commit once and end the session");
        host.message(WM_IME_STARTCOMPOSITION);
        composition_string = L"\xd83d\xde00\x3042"; // Emoji and hiragana, with a cursor inside the surrogate pair.
        cursor = 1;
        host.message(WM_IME_COMPOSITION, 0, GCS_COMPSTR | GCS_CURSORPOS);
        require(app.state.item().name.composition->caret == 0, "Native caret must not split a surrogate pair");
        host.message(WM_IME_ENDCOMPOSITION);
        require(!app.composing() && app.state.item().name.text == "N" + std::string(japanese),
                "Ending without a result must cancel preedit");
        host.message(WM_IME_STARTCOMPOSITION);
        result_string = std::wstring{L'\xd800'};
        host.message(WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
        host.message(WM_IME_ENDCOMPOSITION);
        require(!app.composing() && app.state.item().name.text == "N" + std::string(japanese),
                "Malformed native result must cancel instead of committing");
        host.message(WM_IME_STARTCOMPOSITION);
        composition_string = L"\x3042";
        cursor = 1;
        host.message(WM_IME_COMPOSITION, 0, GCS_COMPSTR | GCS_CURSORPOS);
        host.message(WM_KILLFOCUS);
        require(!host.composing() && !app.composing() && notifications.back() == CPS_CANCEL &&
                    app.state.item().name.text == "N" + std::string(japanese),
                "Window focus loss must cancel the platform session and preedit");
        // A pointer press first completes the platform session, as a real IME would synchronously.
        host.ime_notify = [&](DWORD action) {
            notifications.push_back(action);
            if (action != CPS_COMPLETE) return;
            result_string = L"\x3042";
            host.message(WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
            host.message(WM_IME_ENDCOMPOSITION);
        };
        host.message(WM_IME_STARTCOMPOSITION);
        host.message(WM_IME_COMPOSITION, 0, GCS_COMPSTR | GCS_CURSORPOS);
        // The taller Inspector card may be scrolled; the wheel neither moves focus nor ends the session.
        wheel_top("inspector");
        require(host.composing(), "Native wheel must keep the composition session");
        // The visible selected-item button takes press focus without changing selection.
        click("selected");
        require(notifications.back() == CPS_COMPLETE && app.state.selected == "cube" &&
                    app.state.item().name.text == "N" + std::string(japanese) + "\xe3\x81\x82" && !app.composing() &&
                    !host.composing() && !window.editing() && !host.ime_attached(),
                "A press must commit the completed result before moving focus and detaching the IME");
        seek("name");
        require(host.ime_attached(), "Returning to the name editor must reattach the IME");
        host.ime_string = {};
        host.ime_cursor = {};
        host.ime_notify = {};
        control_key('A');
        clipboard = "Cube!";
        control_key('V');
        seek("enabled");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        seek("amount");
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        seek("scale");
        require(window.editing() && !host.ime_attached(), "Scale must take text without attaching the IME");
        control_key('A');
        for (const auto unit : {L'2', L'.', L'5'}) host.message(WM_CHAR, unit);
        require(app.state.item().scale == 1 && app.state.item().scale_draft.text == "2.5",
                "Native characters must edit only the Scale draft");
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        host.message(WM_KEYDOWN, VK_UP, 1);
        require(app.state.item().scale == 2.6, "Native Return must apply and Up must step Scale");
        host.message(WM_CHAR, L'x');
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.item().scale == 2.6 && caption().find(L"Scale could not finish.") != std::wstring::npos,
                "A native invalid Scale draft must be rejected with caption feedback");
        host.message(WM_KEYDOWN, VK_ESCAPE, 1);
        require(app.state.item().scale_draft.text == "2.6" && caption() == window.title,
                "Native Escape must revert the Scale draft and clear feedback");
        click("scale-decrease");
        require(app.state.item().scale == 2.5, "A native step-part press must step Scale");
        // Shift+Tab back from Scale reaches the tabs in four stops instead of wrapping the Hierarchy.
        seek("sections-tab-general", true);
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        require(app.state.section == "display" && app.view.tree->find("visibility"),
                "Native Right must select the Display tab");
        seek("visibility-option-inherited");
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        require(app.state.item().visibility == "visible", "Native arrows must select the next Visibility option");
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        seek("shadows");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        const auto cube = app.state.item();
        require(cube.name.text == "Cube!" && !cube.enabled && cube.amount == 55 && cube.visibility == "hidden" &&
                    !cube.shadows,
                "Native fields must accept edits");
        // Tab traversal revealed the last Hierarchy row and the Visibility choices; wheel both back.
        wheel_top("hierarchy");
        wheel_top("inspector");
        require(app.state.item() == cube,
                "Native wheel must reveal the first Hierarchy rows without changing selection");
        click("hierarchy-item-sphere");
        require(app.state.selected == "sphere" && app.state.item().name.text == "Sphere",
                "Native pointer press/release must select another hierarchy item");
        click("hierarchy-item-cube");
        require(app.state.item() == cube, "Native select/edit/return must preserve per-item state");
        const auto selected = host.physical(app.view.center("selected"));
        host.message(WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(selected.x, selected.y));
        host.message(WM_RBUTTONUP, 0, MAKELPARAM(selected.x, selected.y));
        require(app.state.menu == OpenMenu::context, "Native right-click must open selected-item actions");
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(window.editing() && app.state.menu == OpenMenu::none && app.state.section == "general",
                "Native context Rename must show General and focus the name editor");
        const auto handle = host.physical(app.view.center("panes-handle"));
        host.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(handle.x, handle.y));
        require(GetCapture() == host.handle && app.resizing(), "Native splitter must capture");
        host.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(handle.x + 32, handle.y));
        host.message(WM_LBUTTONUP, 0, MAKELPARAM(handle.x + 32, handle.y));
        require(SetWindowPos(host.handle, nullptr, 0, 0, 720, 340, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Native Inspector resize failed");
        require(app.state.item() == cube && app.observed_name("hierarchy-item-cube") == "Cube!" &&
                    host.presentations > 5,
                "Native resize and presentation must preserve accepted edits");
        seek("actions");
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        require(app.focused_on("tool-reset") && app.state.item() == cube,
                "Native arrows must move within the toolbar without running commands");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        const auto reset = app.state.item();
        require(reset.name == cube.name && reset.enabled && reset.amount == 50 && reset.scale == 1 &&
                    reset.visibility == "inherited" && reset.shadows,
                "Native toolbar Reset must restore the properties and keep the name");
        std::cout << "Inspector native injected-message smoke passed: " << host.presentations << " presentations.\n";
    };
    if (smoke) return window.run(verify);
    return window.run();
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            smoke();
            return 0;
        }
        require(argc == 2 && (std::string_view(argv[1]) == "--native" || std::string_view(argv[1]) == "--native-smoke"),
                "Usage: tessera_inspector_hierarchy [--native | --native-smoke]");
#ifdef _WIN32
        return native(std::string_view(argv[1]) == "--native-smoke");
#else
        throw std::runtime_error("The Inspector native host currently requires Windows");
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
