#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/number_drag.hpp>
#include <tessera/input/popup.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/input/split_pane.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/text/text_caret.hpp>
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
constexpr ui::NumberRange position_range{-1000, 1000, 0.1};
constexpr std::uint64_t position_owner_base = 2000; // + 3 * item + axis
constexpr std::uint64_t notes_owner_base = 3000;
constexpr ui::SplitPaneLimits pane_limits{140, 320, 8};
const std::vector<std::pair<std::string, std::string>> visibility_choices{
    {"inherited", "Inherited"}, {"visible", "Visible"}, {"hidden", "Hidden"}};
// USD imageable purpose tokens, chosen through the Display tab's ComboBox.
const std::vector<std::pair<std::string, std::string>> purpose_choices{
    {"default", "Default"}, {"render", "Render"}, {"proxy", "Proxy"}, {"guide", "Guide"}};
const std::vector<std::pair<std::string, std::string>> sections{{"general", "General"}, {"display", "Display"}};
// USD's fallback display color, 50% gray, chosen through the Display tab's ColorPicker.
constexpr HsvColor default_color{0, 0, 0.5};
struct Item {
    std::string id;
    TextEditState name;
    bool enabled = true;
    int amount = 50;
    double scale = 1;                            // Accepted number.
    TextEditState scale_draft;                   // Pending NumberInput text; Enter applies, Escape reverts.
    std::array<double, 3> position{};            // Accepted X/Y/Z.
    std::array<TextEditState, 3> position_draft; // Pending VectorInput component drafts.
    std::string visibility = "inherited";
    bool shadows = true;
    std::string purpose = "default";
    HsvColor color = default_color; // Accepted HSV, keeping its hue while gray.
    TextEditState notes;            // Multiple-line documentation; like the name, Reset keeps it.
    bool operator==(const Item&) const = default;
};
// Host-edited strings: the single-line display name and the multiple-line notes. Each has
// its own editor ID, command and feedback subject; both share copy/paste and IME routing.
enum class TextField : std::uint8_t { name, notes };
struct TextFieldInfo {
    const char* id;
    const char* command;
    const char* subject; // Feedback label.
};
constexpr std::array<TextFieldInfo, 2> text_fields{{{"name", "item.name", "Name"}, {"notes", "item.notes", "Notes"}}};
template<class I> auto& text_field(I& item, TextField field) {
    return field == TextField::name ? item.name : item.notes;
}
const TextFieldInfo& info(TextField field) { return text_fields[static_cast<std::size_t>(field)]; }
std::optional<TextField> text_command(std::string_view command) {
    for (const auto field : {TextField::name, TextField::notes})
        if (command == info(field).command) return field;
    return std::nullopt;
}
// Host-edited numeric drafts: Scale's NumberInput, then the Position VectorInput components. Each
// field has its own accepted value, draft, command, range and reset value.
struct NumberField {
    const char* id;
    const char* command;
    const char* name; // Feedback label.
    ui::NumberRange range;
    double reset;
};
constexpr std::array<NumberField, 4> number_fields{{{"scale", "item.scale", "Scale", scale_range, 1},
                                                    {"position-x", "item.position.x", "Position", position_range, 0},
                                                    {"position-y", "item.position.y", "Position", position_range, 0},
                                                    {"position-z", "item.position.z", "Position", position_range, 0}}};
template<class I> auto& number_value(I& item, std::size_t field) {
    return field == 0 ? item.scale : item.position[field - 1];
}
template<class I> auto& number_draft(I& item, std::size_t field) {
    return field == 0 ? item.scale_draft : item.position_draft[field - 1];
}
std::optional<std::size_t> number_command(std::string_view command) {
    for (std::size_t i = 0; i < number_fields.size(); ++i)
        if (command == number_fields[i].command) return i;
    return std::nullopt;
}
// A draft that differs from its accepted value's canonical text.
bool pending(const Item& item, std::size_t field) {
    return number_draft(item, field).text != ui::format_number(number_value(item, field));
}
// True when Reset would change a property; the display name is not reset.
bool modified(const Item& item) {
    for (std::size_t i = 0; i < number_fields.size(); ++i)
        if (number_value(item, i) != number_fields[i].reset ||
            number_draft(item, i).text != ui::format_number(number_fields[i].reset))
            return true;
    return !item.enabled || item.amount != 50 || item.visibility != "inherited" || !item.shadows ||
           item.purpose != "default" || item.color != default_color;
}
// At most one popup is open: an item menu, the Purpose list or the Display color panel.
enum class OpenMenu { none, actions, context, purpose, color };
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
    for (std::size_t i = 0; i < state.items.size(); ++i) {
        state.items[i].scale_draft = take(make_text_edit_state(scale_owner_base + i, ui::format_number(1)));
        for (std::size_t axis = 0; axis < 3; ++axis)
            state.items[i].position_draft[axis] =
                take(make_text_edit_state(position_owner_base + 3 * i + axis, ui::format_number(0)));
        const auto notes = state.items[i].id == "cube"    ? "Unit cube for layout checks.\nKeep it at the origin."
                           : state.items[i].id == "scene" ? "Root of the example stage."
                                                          : "";
        state.items[i].notes = take(make_text_edit_state(notes_owner_base + i, notes, TextLines::multiple));
    }
    return state;
}
ValidationContext action_context(const State& state) {
    ValidationContext context{{"scene.toggle", "item.name", "item.enabled", "item.amount", "item.scale", "item.shadows",
                               "item.rename", "item.reset", "menu.open", "panes.resize", "purpose.toggle",
                               "item.position.x", "item.position.y", "item.position.z", "color.toggle", "color.area",
                               "color.hue", "item.notes"}};
    for (const auto& item : state.items) context.actions.insert("select." + item.id);
    for (const auto& [key, label] : visibility_choices) context.actions.insert("visibility." + key);
    for (const auto& [key, label] : purpose_choices) context.actions.insert("purpose." + key);
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
        // A focused row stacks above a raised selected neighbor, so its shadow never covers the focus ring.
        sheet.rules.push_back({StyleSelector::of_id(std::move(id), {.focus = true}),
                               {.border_color = Color{.2f, .4f, .85f, 1},
                                .z_index = 2,
                                .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}, 4.f, 2.f}}});
    };
    for (const auto& item : state.items) row("hierarchy-item-" + item.id);
    // Position components share the control decoration; the Grid label column already names the vector.
    sheet.rules.push_back({StyleSelector::of_id("position-label"), {.display = Display::none}});
    for (const auto* axis : {"x", "y", "z"}) {
        const auto id = std::string("position-") + axis;
        sheet.rules.push_back({StyleSelector::of_id(id),
                               {.min_height = 32.f,
                                .border = Edges{1, 1, 1, 1},
                                .padding = Edges{6, 6, 6, 6},
                                .background = Color{1, 1, 1, 1},
                                .border_color = Color{.65f, .72f, .83f, 1},
                                .corner_radius = 6.f}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.hover = true}), {.background = Color{.9f, .94f, 1, 1}}});
        sheet.rules.push_back({StyleSelector::of_id(id, {.active = true}), {.background = Color{.8f, .87f, .98f, 1}}});
        sheet.rules.push_back(
            {StyleSelector::of_id(id, {.focus = true}),
             {.border_color = Color{.2f, .4f, .85f, 1}, .outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}});
        sheet.rules.push_back(
            {StyleSelector::of_id(id + "-label"), {.text = {.size = 12.f}, .color = Color{.32f, .4f, .54f, 1}}});
    }
    const auto selected = "hierarchy-item-" + state.selected;
    // The selected row is raised: its elevation shadow falls on the next row and stays above that row's hover fill.
    sheet.rules.push_back({StyleSelector::of_id(selected),
                           {.background = Color{.2f, .34f, .67f, 1},
                            .color = Color{1, 1, 1, 1},
                            .z_index = 1,
                            .box_shadow = BoxShadow{{0, 2}, 6, {.12f, .22f, .46f, .35f}}}});
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
    sheet.rules.push_back({StyleSelector::of_id("purpose-arrow"), {.color = Color{.32f, .4f, .54f, 1}}});
    // The Display color panel controls carry the shared solid focus outline; their fills stay unchanged.
    for (const auto* id : {"display-color-area", "display-color-hue"})
        sheet.rules.push_back(
            {StyleSelector::of_id(id, {.focus = true}), {.outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}}}});
    for (const auto& [key, label] : purpose_choices) row("purpose-option-" + key);
    sheet.rules.push_back(
        {StyleSelector::of_id("purpose-label-" + state.item().purpose), {.color = Color{.12f, .26f, .6f, 1}}});
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
    // Composed under the root, after the item menus.
    std::optional<ui::Element> purpose_list, color_panel;
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
            ui::Text(pending(item, 1) || pending(item, 2) || pending(item, 3) ? "Position *" : "Position",
                     {.id = "position-caption", .classes = {"muted"}}),
            ui::VectorInput("Position",
                            {{"x", "X", item.position_draft[0], "item.position.x"},
                             {"y", "Y", item.position_draft[1], "item.position.y"},
                             {"z", "Z", item.position_draft[2], "item.position.z"}},
                            {.node = {.id = "position"}}),
            // Contextual help spans the label and control columns below Scale and Position.
            ui::Text("Enter applies, Esc reverts, Up/Down or drag step.",
                     {.id = "scale-hint", .classes = {"muted"}, .style = {.grid_column_span = std::uint16_t{2}}}),
            // Multiple-line documentation; Enter starts a line and the field grows with its wrapped rows.
            ui::Text("Notes", {.id = "notes-caption", .classes = {"muted"}}),
            ui::TextEditor("Notes", item.notes, "item.notes", {.node = control("notes")})};
        rows.resize(7);
    } else {
        properties = {
            ui::Text("Visibility", {.classes = {"muted"}}),
            // Choices share a line when the control column allows and wrap in narrow panes.
            ui::RadioGroup(
                "Visibility", item.visibility, std::move(visibility),
                {.node = {.id = "visibility",
                          .style = {.direction = FlexDirection::row, .flex_wrap = FlexWrap::wrap, .gap = 4.f}}}),
            ui::Text("Shadows", {.classes = {"muted"}}),
            ui::Switch("Cast shadows", item.shadows, "item.shadows", {.node = control("shadows")})};
        std::vector<ui::ComboBoxOption> purposes;
        for (const auto& [key, label] : purpose_choices) purposes.push_back({key, label, "purpose." + key});
        auto purpose = ui::ComboBox(
            "Purpose", item.purpose, std::move(purposes), "purpose.toggle",
            {.node = control("purpose"), .list = {.classes = {"popup"}}, .open = state.menu == OpenMenu::purpose});
        properties.push_back(ui::Text("Purpose", {.classes = {"muted"}}));
        properties.push_back(std::move(purpose.field));
        purpose_list = std::move(purpose.list);
        auto color = ui::ColorPicker(
            "Display color", item.color, {.toggle = "color.toggle", .area = "color.area", .hue = "color.hue"},
            {.node = control("display-color"), .panel = {.classes = {"popup"}}, .open = state.menu == OpenMenu::color});
        properties.push_back(ui::Text("Color", {.classes = {"muted"}}));
        properties.push_back(std::move(color.field));
        color_panel = std::move(color.panel);
        rows.resize(4);
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
    std::vector<ui::Element> layers{
        ui::Menu("Item actions", "actions", commands, popup("actions-menu", OpenMenu::actions)),
        ui::ContextMenu("Item actions", "selected", state.position, commands, popup("item-menu", OpenMenu::context))};
    if (purpose_list) layers.push_back(std::move(*purpose_list));
    if (color_panel) layers.push_back(std::move(*color_panel));
    std::vector<ui::Element> content{
        ui::Row({ui::Text("Hierarchy / Inspector", {.style = {.text = {.size = 20.f, .line_height = 24.f}}}),
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
            ui::ScrollView({ui::Text("Inspector", {.id = "inspector-title", .classes = {"section-title"}}),
                            ui::Column(
                                std::move(card),
                                {.id = "item-card",
                                 .style = {.border = Edges{1, 1, 1, 1},
                                           .padding = Edges{12, 12, 12, 12},
                                           .gap = 12.f,
                                           .border_color = Color{.72f, .8f, .92f, 1},
                                           .corner_radius = 12.f,
                                           .background_gradient = RadialGradient{.center = {.25f, .2f},
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
             .label = "Resize hierarchy"})};
    for (auto& layer : layers) content.push_back(std::move(layer));
    return ui::Column(std::move(content), {.id = "application",
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
std::optional<TextEditRequest> text_edit_request(const InputEvent& event, const TextEditState& state) {
    const auto request = [&state](decltype(TextEditRequest::operation) operation) {
        return TextEditRequest{state.owner, state.revision, std::move(operation)};
    };
    if (const auto* committed = std::get_if<tessera::TextInput>(&event.data))
        return request(TextEditInsert{committed->text});
    const auto* key = std::get_if<KeyDown>(&event.data);
    if (!key) return std::nullopt;
    if (key->key == Key::a && key->modifiers == Modifiers{.control = true} && !key->repeat)
        return request(TextEditSelection{0, state.text.size()});
    if (key->modifiers.control || key->modifiers.alt || key->modifiers.super) return std::nullopt;

    switch (key->key) {
        case Key::left:
            return request(TextEditMove{TextMove::left, key->modifiers.shift});
        case Key::right:
            return request(TextEditMove{TextMove::right, key->modifiers.shift});
        case Key::home:
            return request(TextEditMove{TextMove::home, key->modifiers.shift});
        case Key::end:
            return request(TextEditMove{TextMove::end, key->modifiers.shift});
        case Key::backspace:
            return request(TextEditErase{TextErase::backward});
        case Key::delete_key:
            return request(TextEditErase{TextErase::forward});
        default:
            return std::nullopt;
    }
}
// One host-normalized IME step for the focused name editor; UTF-8 bytes only.
struct ImeStep {
    enum class Kind : std::uint8_t { start, update, commit, cancel } kind = Kind::start;
    std::string text;      // Update preedit or commit result.
    std::size_t caret = 0; // Byte offset within an update's preedit.
};
// Host requests for one numeric field, each executed through that field's own command.
struct NumberApply {};  // Enter: parse the draft and accept it.
struct NumberRevert {}; // Escape: restore the canonical accepted text.
struct NumberStep {     // Up/Down or a step part: move from the parsed draft and accept.
    int steps = 0;
};
struct NumberScrub { // Pointer scrubbing: move from the value the scrub started at and accept.
    double origin = 0;
    int steps = 0;
};
using NumberRequest = std::variant<TextEditRequest, NumberApply, NumberRevert, NumberStep, NumberScrub>;
// Edits change only the draft. Apply/step accept a valid draft and replace it with canonical
// text (caret at the end, next revision); an invalid draft rejects them and stays as typed.
std::vector<Diagnostic> apply_number(Item& item, std::size_t field, const NumberRequest& request) {
    auto& draft = number_draft(item, field);
    auto& value = number_value(item, field);
    const auto range = number_fields[field].range;
    if (const auto* edit = std::get_if<TextEditRequest>(&request)) {
        auto next = edit_text(draft, *edit);
        if (!next) return std::move(next.diagnostics);
        draft = std::move(*next.value);
        return {};
    }
    if (const auto* scrub = std::get_if<NumberScrub>(&request)) {
        auto next = ui::step_number(range, scrub->origin, scrub->steps);
        if (!next) return std::move(next.diagnostics);
        value = *next.value;
    } else if (!std::holds_alternative<NumberRevert>(request)) {
        auto parsed = ui::parse_number(range, draft.text);
        if (!parsed) return std::move(parsed.diagnostics);
        value = *parsed.value;
        if (const auto* step = std::get_if<NumberStep>(&request))
            value = take(ui::step_number(range, value, step->steps));
    }
    draft.text = ui::format_number(value);
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
        else if (const auto edited = text_command(command)) {
            require(edit.has_value(), "Text editing needs an owned request");
            auto& field = text_field(candidate.item(), *edited);
            auto next = edit_text(field, *edit);
            if (!next) return {CommandOutcomeStatus::error, {}, std::move(next.diagnostics)};
            field = std::move(*next.value);
        } else if (command == "item.enabled")
            candidate.item().enabled = !candidate.item().enabled;
        else if (command.starts_with("visibility."))
            candidate.item().visibility = command.substr(11);
        else if (command.starts_with("section."))
            candidate.section = command.substr(8);
        else if (command == "item.reset") {
            // Restore every property except the display name; pending numeric drafts are discarded.
            auto& item = candidate.item();
            item.enabled = true;
            item.amount = 50;
            for (std::size_t i = 0; i < number_fields.size(); ++i) {
                auto& draft = number_draft(item, i);
                number_value(item, i) = number_fields[i].reset;
                draft.text = ui::format_number(number_fields[i].reset);
                draft.anchor = draft.caret = draft.text.size();
                ++draft.revision;
            }
            item.visibility = "inherited";
            item.shadows = true;
            item.purpose = "default";
            item.color = default_color;
        } else if (command == "item.shadows")
            candidate.item().shadows = !candidate.item().shadows;
        else if (const auto field = number_command(command)) {
            require(number_request && number_request->first == *field, "Numeric input needs an owned request");
            auto rejected = apply_number(candidate.item(), *field, number_request->second);
            if (!rejected.empty()) return {CommandOutcomeStatus::error, {}, std::move(rejected)};
        } else if (command == "item.amount") {
            candidate.item().amount = amount.value_or(std::min(100, candidate.item().amount + amount_range.step));
        } else if (command == "panes.resize") {
            require(split.has_value(), "Pane resize needs a size");
            candidate.split = *split;
        } else if (command == "menu.open")
            candidate.menu = OpenMenu::actions;
        else if (command == "purpose.toggle")
            candidate.menu = candidate.menu == OpenMenu::purpose ? OpenMenu::none : OpenMenu::purpose;
        else if (command == "color.toggle")
            candidate.menu = candidate.menu == OpenMenu::color ? OpenMenu::none : OpenMenu::color;
        else if (command == "color.area" || command == "color.hue") {
            // Each accepted adjustment applies at once; the panel stays open.
            require(picked_color.has_value(), "Color adjustment needs a requested color");
            candidate.item().color = *picked_color;
        } else if (command.starts_with("purpose.")) {
            // An accepted choice closes the list; refresh then returns focus to the trigger.
            candidate.item().purpose = command.substr(8);
            candidate.menu = OpenMenu::none;
        } else if (command != "item.rename")
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
        if (text_command(action.action) && !edit) return {};
        if (number_command(action.action) && !number_request) return {};
        if (action.action == "panes.resize" && !split) return {};
        // Plain activation of a panel control carries no color; only the dispatcher requests one.
        if ((action.action == "color.area" || action.action == "color.hue") && !picked_color) return {};
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
        const auto choice = take(purpose.dispatch(view.hit(), event, take(focus.refresh(view.hit())).focused));
        if (choice.focus) (void)take(focus.focus(view.hit(), *choice.focus));
        if (choice.dismiss) {
            state.menu = OpenMenu::none;
            pointer.reset();
            slider_pointer.reset();
            refresh();
        }
        if (choice.open)
            checked(operate(take(request_semantic_action(view.semantic(), *view.tree->find("purpose"), "activate")),
                            source));
        if (choice.consumed) {
            observe();
            return;
        }
        const auto picked = take(
            display_color.dispatch(view.hit(), event, take(focus.refresh(view.hit())).focused, state.item().color));
        if (picked.focus) (void)take(focus.focus(view.hit(), *picked.focus));
        if (picked.dismiss) {
            state.menu = OpenMenu::none;
            pointer.reset();
            slider_pointer.reset();
            refresh();
        }
        if (picked.open)
            checked(operate(
                take(request_semantic_action(view.semantic(), *view.tree->find("display-color"), "activate")), source));
        if (picked.action) {
            picked_color = picked.color;
            const auto outcome = operate(*picked.action, source);
            picked_color.reset();
            checked(outcome);
        }
        if (picked.consumed) {
            observe();
            return;
        }
        if (scrub(take(number_drag.dispatch(view.hit(), event)), source)) return;
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
                if (try_number(0, NumberStep{steps}, source).status != CommandOutcomeStatus::success) refresh(true);
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
        if (const auto field = focused_text(focused)) {
            std::optional<float> goal;
            auto request =
                *field == TextField::notes ? notes_request(event, goal) : text_edit_request(event, state.item().name);
            if (request) {
                const auto outcome = try_text(*field, std::move(*request), source);
                edit_diagnostics = outcome.diagnostics;
                if (outcome.status != CommandOutcomeStatus::success)
                    require(!edit_diagnostics.empty(), "Rejected text edit needs diagnostics");
                // Successive Up/Down keep the column they started from until another edit.
                const auto& notes = state.item().notes;
                vertical_goal.reset();
                if (goal && outcome.status == CommandOutcomeStatus::success)
                    vertical_goal = VerticalGoal{notes.owner, notes.revision, *goal};
                report(std::string(info(*field).subject) + " edit", state.selected, edit_diagnostics);
                return;
            }
        }
        if (const auto field = focused_number(focused)) {
            const auto& draft = number_draft(state.item(), *field);
            if (auto request = text_edit_request(event, draft)) {
                (void)try_number(*field, std::move(*request), source);
                return;
            }
            if (const auto* key = std::get_if<KeyDown>(&event.data); key && key->modifiers == Modifiers{}) {
                std::optional<NumberRequest> request;
                if (key->key == Key::up) request = NumberStep{1};
                if (key->key == Key::down) request = NumberStep{-1};
                if (key->key == Key::enter && !key->repeat) request = NumberApply{};
                if (key->key == Key::escape && !key->repeat) request = NumberRevert{};
                if (request) {
                    (void)try_number(*field, std::move(*request), source);
                    return;
                }
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
        return try_text(TextField::name, std::move(request), source);
    }
    // Executes one logical edit through the text field's own command.
    CommandOutcome try_text(TextField field, TextEditRequest request, CommandSource source = CommandSource::keyboard) {
        edit = std::move(request);
        const auto outcome = operate(
            take(request_semantic_action(view.semantic(), *view.tree->find(info(field).id), "activate")), source);
        edit.reset();
        return outcome;
    }
    // The text field whose editor holds focus, if any; absent on the Display tab.
    std::optional<TextField> focused_text(std::optional<NodeHandle> focused) const {
        if (!focused) return std::nullopt;
        for (const auto field : {TextField::name, TextField::notes})
            if (focused == view.tree->find(info(field).id)) return field;
        return std::nullopt;
    }
    // Caret geometry of the presented text in an editor's <id>-value Text, with the Text's content origin.
    std::optional<std::pair<TextCaretLayout, Point>> caret_layout(TextField field, std::string_view shown) {
        const auto value = view.tree->find(std::string(info(field).id) + "-value");
        if (!value) return std::nullopt;
        const auto found = std::find_if(view.layout.boxes.begin(), view.layout.boxes.end(),
                                        [&](const LayoutBox& box) { return box.node == *value; });
        if (found == view.layout.boxes.end() || !found->visible) return std::nullopt;
        const auto content = found->content_box();
        auto carets = layout_text_carets(text, shown, view.styles[value->index].text, {content.size.width});
        if (!carets) return std::nullopt;
        return std::pair{std::move(*carets.value), content.origin};
    }
    // Notes keys that need the presented rows: Enter inserts LF, Home/End address the visual
    // line and Up/Down move by row toward `goal` x, the column where vertical movement began.
    // Control+Home/End address the buffer ends. Other keys share the name editor's mapping.
    std::optional<TextEditRequest> notes_request(const InputEvent& event, std::optional<float>& goal) {
        const auto& notes = state.item().notes;
        const auto* key = std::get_if<KeyDown>(&event.data);
        if (!key) return text_edit_request(event, notes);
        const auto request = [&](decltype(TextEditRequest::operation) operation) {
            return TextEditRequest{notes.owner, notes.revision, std::move(operation)};
        };
        const auto mods = key->modifiers;
        if (mods.alt || mods.super) return std::nullopt;
        if (key->key == Key::enter && !mods.control) return request(TextEditInsert{"\n"});
        const bool home = key->key == Key::home, end = key->key == Key::end;
        if ((home || end) && mods.control)
            return request(TextEditMove{home ? TextMove::home : TextMove::end, mods.shift});
        if (mods.control || (!home && !end && key->key != Key::up && key->key != Key::down))
            return text_edit_request(event, notes);
        const auto layout = caret_layout(TextField::notes, notes.text);
        if (!layout) return std::nullopt;
        const auto& carets = layout->first;
        const auto line = carets.line_of(notes.caret);
        std::size_t target = 0;
        if (home)
            target = carets.lines[line].begin;
        else if (end)
            target = carets.line_end(line);
        else {
            goal = vertical_goal && vertical_goal->owner == notes.owner && vertical_goal->revision == notes.revision
                       ? vertical_goal->x
                       : carets.caret(notes.caret).origin.x;
            target = carets.offset_at(key->key == Key::up ? (line ? line - 1 : 0) : line + 1, *goal);
        }
        return request(TextEditSelection{mods.shift ? notes.anchor : target, target});
    }
    // Addresses the selected item's draft for one numeric field; rejection keeps the draft and reports feedback.
    CommandOutcome try_number(std::size_t field, NumberRequest request,
                              CommandSource source = CommandSource::keyboard) {
        number_request = {field, std::move(request)};
        const auto outcome = operate(
            take(request_semantic_action(view.semantic(), *view.tree->find(number_fields[field].id), "activate")),
            source);
        number_request.reset();
        edit_diagnostics = outcome.diagnostics;
        if (outcome.status != CommandOutcomeStatus::success)
            require(!edit_diagnostics.empty(), "Rejected numeric input needs diagnostics");
        report(number_fields[field].name, state.selected, edit_diagnostics, number_fields[field].range);
        return outcome;
    }
    // The numeric field whose editor holds focus, if any.
    std::optional<std::size_t> focused_number(std::optional<NodeHandle> focused) const {
        if (!focused) return std::nullopt;
        for (std::size_t i = 0; i < number_fields.size(); ++i)
            if (focused == view.tree->find(number_fields[i].id)) return i;
        return std::nullopt;
    }
    bool resizing() const { return splitter.dragging(); }
    // A horizontal resize cursor over the splitter and over scrubbable numeric fields.
    bool horizontal_cursor(Point point) const {
        if (splitter.dragging() || number_drag.pressed()) return true;
        const auto geometry = take(split_pane_geometry(view.hit(), "panes"));
        return (geometry && geometry->enabled && take(hit_test(view.hit(), point)).target == geometry->handle) ||
               take(number_drag_target(view.hit(), point)).has_value();
    }
    bool composing() const {
        const auto& item = state.item();
        return item.name.composition.has_value() || item.notes.composition.has_value();
    }
    // Platform sessions address only the focused, eligible text editor that started them.
    void ime(ImeStep step, CommandSource source = CommandSource::keyboard) {
        using Kind = ImeStep::Kind;
        if (step.kind == Kind::start) {
            cancel_composition(source); // A restarted platform session replaces any open preedit.
            const auto field = focused_text(take(focus.refresh(view.hit())).focused);
            ime_ignored = !field || !eligible_text(*field);
            if (ime_ignored) {
                edit_diagnostics = {{"ime_target", Severity::error, "/composition",
                                     "Composition needs a focused, eligible text editor."}};
                report("Composition", state.selected, edit_diagnostics);
                return;
            }
            ime_item = state.selected;
            ime_field = *field;
        } else if (ime_ignored) {
            return;
        }
        if (step.kind == Kind::cancel) return cancel_composition(source);
        const auto edited = text_field(state.item(), ime_field);
        const auto id = edited.composition ? edited.composition->id : 0;
        std::optional<decltype(TextEditRequest::operation)> operation;
        if (state.selected != ime_item || !eligible_text(ime_field)) {
            edit_diagnostics = {{"stale_composition", Severity::error, "/composition",
                                 "Composition target changed during the platform session."}};
        } else {
            if (step.kind == Kind::start)
                operation = TextCompositionStart{};
            else if (step.kind == Kind::update)
                operation = TextCompositionUpdate{id, std::move(step.text), step.caret};
            else
                operation = TextCompositionCommit{id, std::move(step.text)};
            edit_diagnostics =
                try_text(ime_field, {edited.owner, edited.revision, std::move(*operation)}, source).diagnostics;
        }
        // A rejected commit still ends the platform session: drop preedit, keep committed bytes.
        if (step.kind == Kind::commit && !edit_diagnostics.empty()) {
            auto rejected = std::move(edit_diagnostics);
            cancel_composition(source);
            edit_diagnostics = std::move(rejected);
        }
        report("Composition", state.selected, edit_diagnostics);
    }
    // Logical caret rectangle in the composing, else the focused, text editor's presented text,
    // for platform candidate placement. Multiple-line notes place it on the caret's visual row.
    std::optional<Rect> caret_rect() {
        const auto& item = state.item();
        std::optional<TextField> field;
        if (item.name.composition || item.notes.composition)
            field = item.name.composition ? TextField::name : TextField::notes;
        else
            field = focused_text(take(focus.refresh(view.hit())).focused);
        if (!field) return {};
        const auto shown = take(display_text_edit(text_field(item, *field)));
        const auto layout = caret_layout(*field, shown.text);
        if (!layout) return {};
        auto caret = layout->first.caret(shown.caret);
        caret.origin = {layout->second.x + caret.origin.x, layout->second.y + caret.origin.y};
        return caret;
    }

private:
    bool eligible_text(TextField field) {
        return state.menu == OpenMenu::none && focused_on(info(field).id) &&
               static_cast<bool>(
                   request_semantic_action(view.semantic(), *view.tree->find(info(field).id), "activate"));
    }
    void cancel_composition(CommandSource source) {
        for (const auto field : {TextField::name, TextField::notes}) {
            const auto edited = text_field(state.item(), field);
            if (!edited.composition) continue;
            const auto outcome =
                try_text(field, {edited.owner, edited.revision, TextCompositionCancel{edited.composition->id}}, source);
            if (outcome.status != CommandOutcomeStatus::success) {
                // Presentation failed; drop preedit anyway so no stale session survives,
                // and replace the view once a presentation succeeds.
                auto& retained = text_field(state.item(), field);
                retained.composition.reset();
                ++retained.revision;
                auto next = state;
                auto candidate_focus = focus;
                if (auto candidate = stage(next, viewport, offsets, candidate_focus, false)) {
                    state = std::move(next);
                    focus = std::move(candidate_focus);
                    accept(std::move(*candidate.value));
                }
            }
        }
    }
    // Pointer scrubbing over Scale and the Position components. Each changed step count runs the
    // field's own command from the value the scrub started at; Escape requests the start value
    // again. Returns true when the event was consumed.
    bool scrub(const NumberDragDispatchResult& result, CommandSource source) {
        std::optional<std::size_t> field;
        if (result.input) field = number_command(view.tree->get(*result.input)->events.at("activate"));
        if (result.started && field) {
            // The scrub starts from the parsed draft, like a step; an invalid draft cannot scrub.
            auto origin = ui::parse_number(number_fields[*field].range, number_draft(state.item(), *field).text);
            if (!origin) {
                number_drag.reset();
                edit_diagnostics = std::move(origin.diagnostics);
                report(number_fields[*field].name, state.selected, edit_diagnostics, number_fields[*field].range);
                observe();
                return true;
            }
            scrub_origin = {*field, *origin.value};
        }
        if (result.steps && field && scrub_origin && scrub_origin->first == *field) {
            const auto& item = state.item();
            const auto origin = scrub_origin->second;
            // Clamped steps can repeat the accepted value; only a change runs the command.
            if (take(ui::step_number(number_fields[*field].range, origin, *result.steps)) !=
                    number_value(item, *field) ||
                pending(item, *field)) {
                if (try_number(*field, NumberScrub{origin, *result.steps}, source).status !=
                    CommandOutcomeStatus::success)
                    number_drag.reset();
            }
        }
        if (result.ended || !number_drag.pressed()) scrub_origin.reset();
        if (result.focus) {
            (void)take(focus.focus(view.hit(), *result.focus));
            refresh(true);
            return true;
        }
        if (result.consumed) observe();
        return result.consumed;
    }
    bool clipboard_key(const KeyDown& key, CommandSource source) {
        if ((key.key != Key::c && key.key != Key::v && key.key != Key::x) ||
            key.modifiers != Modifiers{.control = true} || key.repeat || state.menu != OpenMenu::none)
            return false;
        const auto field = focused_text(take(focus.refresh(view.hit())).focused);
        if (!field) return false;
        clipboard_diagnostics.clear();
        const auto eligible = [&] { return eligible_text(*field); };
        if (!eligible()) return true;
        const auto selected = state.selected;
        const auto edited = text_field(state.item(), *field);
        const auto target_unchanged = [&] {
            return state.selected == selected && text_field(state.item(), *field) == edited && eligible();
        };
        if (key.key == Key::c || key.key == Key::x) {
            const auto begin = std::min(edited.anchor, edited.caret), end = std::max(edited.anchor, edited.caret);
            if (begin == end) return true;
            if (clipboard_write)
                clipboard_diagnostics = clipboard_write(std::string_view(edited.text).substr(begin, end - begin));
            else
                clipboard_error("clipboard_unavailable", "No clipboard write service is connected.");
            if (key.key == Key::x && clipboard_diagnostics.empty()) {
                if (!target_unchanged()) {
                    clipboard_error("stale_clipboard_target", "Clipboard target changed during the host request.");
                } else {
                    const auto outcome =
                        try_text(*field, {edited.owner, edited.revision, TextEditErase{TextErase::forward}}, source);
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
                    // Notes accept LF only: platform CRLF and lone CR line breaks become LF.
                    if (edited.lines == TextLines::multiple) *text.value = line_feeds(std::move(*text.value));
                    const auto outcome = try_text(
                        *field, {edited.owner, edited.revision, TextEditInsert{std::move(*text.value)}}, source);
                    clipboard_diagnostics = outcome.diagnostics;
                }
            }
        }
        report(key.key == Key::c ? "Copy" : key.key == Key::x ? "Cut" : "Paste", selected, clipboard_diagnostics);
        return true;
    }
    static std::string line_feeds(std::string text) {
        std::string result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\r') {
                result += text[i];
                continue;
            }
            result += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        }
        return result;
    }
    // `range` names the bounds of a rejected numeric field.
    void report(std::string_view operation, std::string_view item, const std::vector<Diagnostic>& diagnostics,
                std::optional<ui::NumberRange> range = {}) {
        feedback.clear();
        if (diagnostics.empty()) return;
        feedback = std::string(operation) + " could not finish. ";
        const auto& code = diagnostics.front().code;
        if (code == "text_too_long")
            feedback += "Text must fit within 4096 UTF-8 bytes.";
        else if (code == "multiline_text")
            feedback += "Use a single line for the name.";
        else if ((code == "number_syntax" || code == "number_out_of_range") && range)
            feedback += "Enter a number from " + ui::format_number(range->minimum) + " to " +
                        ui::format_number(range->maximum) + ".";
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
    ComboBoxDispatcher purpose{"purpose"};
    ColorPickerDispatcher display_color{"display-color"};
    NumberDragDispatcher number_drag;
    std::optional<std::pair<std::size_t, double>> scrub_origin; // Field and value the active scrub started at.
    Offsets offsets;
    std::optional<TextEditRequest> edit;
    std::optional<std::pair<std::size_t, NumberRequest>> number_request;
    std::string ime_item;
    TextField ime_field = TextField::name;
    bool ime_ignored = false;
    // The notes column where Up/Down began, valid while the notes keep the revision it produced.
    struct VerticalGoal {
        std::uint64_t owner = 0, revision = 0;
        float x = 0;
    };
    std::optional<VerticalGoal> vertical_goal;
    std::optional<int> amount;
    std::optional<float> split;
    std::optional<HsvColor> picked_color; // The dispatcher's request for one color.area/color.hue command.
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
        if (take(number_drag.refresh(view.hit())).ended) scrub_origin.reset();
        (void)take(focus.refresh(view.hit()));
        bool dismissed = false;
        for (const auto& transition : {take(menu.refresh(view.hit())), take(context_menu.refresh(view.hit()))}) {
            if (transition.focus) (void)take(focus.focus(view.hit(), *transition.focus));
            dismissed = dismissed || transition.dismiss;
        }
        const auto choice = take(purpose.refresh(view.hit()));
        if (choice.focus) (void)take(focus.focus(view.hit(), *choice.focus));
        const auto picker = take(display_color.refresh(view.hit()));
        if (picker.focus) (void)take(focus.focus(view.hit(), *picker.focus));
        dismissed = dismissed || choice.dismiss || picker.dismiss;
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
        if (const auto scrubbed = number_drag.captured())
            if (const auto node = candidate.tree->find(*scrubbed)) state.active.push_back(*node);
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
    // Leave room below the card, which now includes the Scale input, Visibility choices and Notes.
    app.resize({960, 640});
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

    // The group is a wrapping row: one line in the wide pane, a second line in the narrow control column.
    const auto option = [&](std::string_view key) {
        return app.view.box("visibility-option-" + std::string(key)).border_box;
    };
    const auto right = [](const Rect& rect) { return rect.origin.x + rect.size.width; };
    const auto bottom = [](const Rect& rect) { return rect.origin.y + rect.size.height; };
    require(option("visible").origin.y == option("inherited").origin.y &&
                option("hidden").origin.y == option("inherited").origin.y &&
                option("visible").origin.x == right(option("inherited")) + 4 &&
                option("hidden").origin.x == right(option("visible")) + 4,
            "Visibility choices must share one line in the wide Inspector");
    app.action("hierarchy-item-cube");
    app.resize({492, 360});
    const auto lines = app.view.box("visibility").content_box();
    const auto shadows = app.view.box("shadows").border_box;
    require(option("inherited").origin.x == lines.origin.x && option("hidden").origin.x == lines.origin.x &&
                option("hidden").origin.y >= bottom(option("inherited")) + 4 &&
                right(option("inherited")) <= right(lines) && right(option("visible")) <= right(lines) &&
                right(option("hidden")) <= right(lines) && bottom(option("hidden")) == bottom(lines) &&
                shadows.origin.y >= bottom(lines) + 12,
            "Narrow Visibility choices must wrap inside the control column and push Shadows below their lines");
    const auto wrapped = app.view.reachable("visibility-option-hidden");
    input(PointerDown{{1}, wrapped}, CommandSource::pointer);
    input(PointerUp{{1}, wrapped}, CommandSource::pointer);
    require(app.state.item().visibility == "hidden" &&
                take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find("visibility-option-hidden"),
            "A pointer press on a wrapped choice must focus and select it");
    app.focus_id("visibility-option-inherited");
    input(KeyDown{Key::down});
    require(app.state.item().visibility == "visible", "Arrow order must stay in declared order across lines");
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
void purpose_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    app.action("sections-tab-display");
    const auto trigger = app.observed("purpose");
    require(trigger.role == SemanticRole::combobox && trigger.name == "Purpose" && trigger.value == "Default" &&
                trigger.expanded == false &&
                trigger.actions == std::vector<SemanticAction>{{"activate", "purpose.toggle"}},
            "Purpose must expose a named combobox with the accepted value");
    const auto list_node = app.view.tree->find("purpose-list");
    require(list_node && std::none_of(app.view.layout.boxes.begin(), app.view.layout.boxes.end(),
                                      [&](const LayoutBox& box) { return box.node == *list_node; }),
            "A closed Purpose list must not be shown");
    app.focus_id("shadows");
    input(KeyDown{Key::tab});
    require(app.focused_on("purpose"), "Tab must reach Purpose after Cast shadows");
    auto executions = app.executions;
    input(KeyDown{Key::down});
    require(app.state.menu == OpenMenu::none && app.executions == executions && app.state.item().purpose == "default",
            "Plain Down must not open or change Purpose");
    app.focus_id("purpose");
    input(KeyDown{Key::down, {.alt = true}});
    require(app.state.menu == OpenMenu::purpose && app.executions == executions + 1 &&
                app.focused_on("purpose-option-default") && app.observed("purpose").expanded == true &&
                app.observed("purpose-list").role == SemanticRole::listbox &&
                app.observed("purpose-option-default").selected == true,
            "Alt+Down must open the list at the selected option");
    const auto field = app.view.box("purpose").border_box;
    const auto list = app.view.box("purpose-list").border_box;
    require(
        list.size.width >= field.size.width && list.origin.x == field.origin.x &&
            (list.origin.y >= field.origin.y + field.size.height || list.origin.y + list.size.height <= field.origin.y),
        "The Purpose list must sit beside the trigger and be at least as wide");
    input(KeyDown{Key::down});
    input(KeyDown{Key::down});
    require(app.focused_on("purpose-option-proxy") && app.state.item().purpose == "default" &&
                app.executions == executions + 1,
            "Arrows must move within the list without selecting");
    input(Activate{});
    require(app.state.item().purpose == "proxy" && app.state.menu == OpenMenu::none && app.focused_on("purpose") &&
                app.observed_value("purpose") == "Proxy" && app.view.tree->find("modified-badge") &&
                app.executions == executions + 2,
            "Activate must choose the focused option, close the list and return focus");
    input(Activate{});
    require(app.state.menu == OpenMenu::purpose && app.focused_on("purpose-option-proxy"),
            "Activating the trigger must open the list at the new selection");
    input(KeyDown{Key::up});
    input(KeyDown{Key::escape});
    require(app.state.item().purpose == "proxy" && app.state.menu == OpenMenu::none && app.focused_on("purpose"),
            "Escape must dismiss without choosing");
    input(Activate{});
    input(KeyDown{Key::tab});
    require(app.state.menu == OpenMenu::none && app.state.item().purpose == "proxy" && !app.focused_on("purpose") &&
                !app.focused_on("purpose-option-proxy"),
            "Tab must close the list and continue from the trigger");
    const auto press = [&](std::string_view id) {
        const auto point = app.view.reachable(id);
        input(PointerDown{{1}, point}, CommandSource::pointer);
        input(PointerUp{{1}, point}, CommandSource::pointer);
    };
    press("purpose");
    require(app.state.menu == OpenMenu::purpose, "A press on the trigger must open the list");
    input(PointerMove{{1}, app.view.reachable("purpose-option-guide")}, CommandSource::pointer);
    require(app.focused_on("purpose-option-guide"), "Hover must focus an option");
    press("purpose-option-guide");
    require(app.state.item().purpose == "guide" && app.state.menu == OpenMenu::none && app.focused_on("purpose"),
            "A pointer press must choose an option and close the list");
    press("purpose");
    executions = app.executions;
    press("hierarchy-item-sphere");
    require(app.state.menu == OpenMenu::none && app.state.item().purpose == "guide" && app.executions == executions &&
                app.state.selected == "cube" && app.focused_on("purpose"),
            "An outside press must close the list without clicking through");
    const auto label = [&](std::string_view key) {
        return app.view.styles[app.view.tree->find("purpose-label-" + std::string(key))->index].color;
    };
    press("purpose");
    require(label("guide") != label("default"), "The selected option label must be emphasized");
    input(KeyDown{Key::escape});
    app.action("hierarchy-item-sphere");
    require(app.state.item().purpose == "default" && app.observed_value("purpose") == "Default",
            "Another item must show its own Purpose");
    app.action("hierarchy-item-cube");
    require(app.state.item().purpose == "guide", "Returning to an item must retain its Purpose");
    app.action("tool-reset");
    require(app.state.item().purpose == "default", "Reset must restore Purpose");
}
void color_smoke() {
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    app.action("sections-tab-display");
    const auto trigger = app.observed("display-color");
    require(trigger.role == SemanticRole::color_picker && trigger.name == "Display color" &&
                trigger.value == "#808080" && trigger.expanded == false &&
                trigger.actions == std::vector<SemanticAction>{{"activate", "color.toggle"}},
            "Display color must expose a named color picker with the accepted hex value");
    const auto panel_node = app.view.tree->find("display-color-panel");
    require(panel_node && std::none_of(app.view.layout.boxes.begin(), app.view.layout.boxes.end(),
                                       [&](const LayoutBox& box) { return box.node == *panel_node; }),
            "A closed Display color panel must not be shown");
    app.focus_id("purpose");
    input(KeyDown{Key::tab});
    require(app.focused_on("display-color"), "Tab must reach Display color after Purpose");
    auto executions = app.executions;
    input(KeyDown{Key::down, {.alt = true}});
    require(app.state.menu == OpenMenu::color && app.executions == executions + 1 &&
                app.focused_on("display-color-area") && app.observed("display-color").expanded == true &&
                app.observed("display-color-panel").role == SemanticRole::group &&
                app.observed_name("display-color-area") == "Saturation 0%, value 50%" &&
                app.observed_name("display-color-hue") == "Hue 0 degrees",
            "Alt+Down must open the panel at the area");
    const auto field = app.view.box("display-color").border_box;
    const auto panel = app.view.box("display-color-panel").border_box;
    require(panel.origin.x == field.origin.x && (panel.origin.y >= field.origin.y + field.size.height ||
                                                 panel.origin.y + panel.size.height <= field.origin.y),
            "The panel must sit beside the trigger");
    input(KeyDown{Key::right});
    input(KeyDown{Key::up});
    require(app.state.item().color == HsvColor{0, 0.05, 0.55} && app.executions == executions + 3 &&
                app.state.menu == OpenMenu::color && app.focused_on("display-color-area") &&
                app.observed_value("display-color") == format_hex_color(to_color({0, 0.05, 0.55})) &&
                app.view.tree->find("modified-badge"),
            "Area arrows must adjust saturation and value, keep the panel open and mark the item");
    require(app.view.styles[app.view.tree->find("display-color-swatch")->index].background ==
                to_color(app.state.item().color),
            "The swatch must show the accepted color");
    input(KeyDown{Key::tab});
    input(KeyDown{Key::right});
    require(app.focused_on("display-color-hue") && app.state.item().color == HsvColor{5, 0.05, 0.55},
            "Tab must reach the hue strip, whose arrows adjust hue");
    input(KeyDown{Key::tab});
    require(app.state.menu == OpenMenu::none && !app.focused_on("display-color") &&
                app.state.item().color == HsvColor{5, 0.05, 0.55},
            "Tab past the hue strip must close the panel and continue from the trigger");
    const auto press = [&](std::string_view id) {
        const auto point = app.view.reachable(id);
        input(PointerDown{{1}, point}, CommandSource::pointer);
        input(PointerUp{{1}, point}, CommandSource::pointer);
    };
    const auto at = [&](std::string_view id, float x, float y) {
        const auto content = app.view.box(id).content_box();
        return Point{content.origin.x + x * content.size.width, content.origin.y + y * content.size.height};
    };
    press("display-color");
    require(app.state.menu == OpenMenu::color && app.focused_on("display-color-area"),
            "A press on the trigger must open the panel");
    executions = app.executions;
    input(PointerDown{{1}, at("display-color-area", 0.25f, 0.25f)}, CommandSource::pointer);
    input(PointerMove{{1}, at("display-color-area", 0.75f, 0.5f)}, CommandSource::pointer);
    require(app.state.item().color == HsvColor{5, 0.75, 0.5} && app.executions == executions + 2,
            "A press and drag on the area must request the colors under the pointer");
    input(PointerUp{{1}, at("display-color-area", 0.75f, 0.5f)}, CommandSource::pointer);
    require(app.state.item().color == HsvColor{5, 0.75, 0.5} && app.executions == executions + 2 &&
                app.state.menu == OpenMenu::color,
            "An unchanged release must not repeat the command or close the panel");
    const auto thumb = app.view.box("display-color-area-thumb").border_box;
    const auto area = app.view.box("display-color-area").content_box();
    require(std::abs(thumb.origin.x + 6 - (area.origin.x + 120)) < 1e-3f &&
                std::abs(thumb.origin.y + 6 - (area.origin.y + 60)) < 1e-3f,
            "The area thumb must follow the accepted saturation and value");
    press("display-color-hue");
    require(app.focused_on("display-color-hue") && app.state.item().color == HsvColor{180, 0.75, 0.5},
            "A pointer press on the hue strip must select its hue");
    executions = app.executions;
    press("hierarchy-item-sphere");
    require(app.state.menu == OpenMenu::none && app.state.selected == "cube" && app.executions == executions &&
                app.focused_on("display-color"),
            "An outside press must close the panel without clicking through");
    input(Activate{});
    input(KeyDown{Key::left});
    input(KeyDown{Key::escape});
    require(app.state.menu == OpenMenu::none && app.focused_on("display-color") &&
                app.state.item().color == HsvColor{180, 0.7, 0.5},
            "Escape must close the panel and keep the applied color");
    input(Activate{});
    input(KeyDown{Key::enter});
    require(app.state.menu == OpenMenu::none && app.focused_on("display-color"),
            "Enter must close the panel and return focus to the trigger");
    const auto cube = app.state.item().color;
    app.action("hierarchy-item-sphere");
    require(app.state.item().color == default_color && app.observed_value("display-color") == "#808080",
            "Another item must show its own Display color");
    app.action("hierarchy-item-cube");
    require(app.state.item().color == cube, "Returning to an item must retain its Display color");
    app.action("tool-reset");
    require(app.state.item().color == default_color && !app.view.tree->find("modified-badge"),
            "Reset must restore the Display color");
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
void position_smoke() {
    App app;
    // Leave room below the card for every General row.
    app.resize({960, 540});
    std::chrono::microseconds time{};
    auto input = [&](auto event, CommandSource source = CommandSource::keyboard) {
        app.input({++time, event}, source);
    };
    const auto caption = [&] {
        return std::get<std::string>(
            app.view.tree->get(*app.view.tree->find("position-caption"))->properties.at("text"));
    };
    const auto focused = [&](std::string_view id) {
        return take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id);
    };
    const auto nodes = app.view.inspection.semantics.nodes;
    const auto group = std::find_if(nodes.begin(), nodes.end(), [](const auto& node) { return node.id == "position"; });
    require(group != nodes.end() && group->role == SemanticRole::group && group->name == "Position" &&
                group->actions.empty() && caption() == "Position",
            "Position must expose a named group");
    for (const auto& [id, name, action] :
         std::vector<std::tuple<const char*, const char*, const char*>>{{"position-x", "X", "item.position.x"},
                                                                        {"position-y", "Y", "item.position.y"},
                                                                        {"position-z", "Z", "item.position.z"}}) {
        const auto& component = app.observed(id);
        require(component.role == SemanticRole::number_input && component.name == name && component.value == "0" &&
                    component.actions == std::vector<SemanticAction>{{"activate", action}} &&
                    &nodes[component.parent] == &*group,
                "Each Position component must be a named number input inside the group");
    }
    app.focus_id("scale");
    for (const auto* id : {"position-x", "position-y", "position-z"}) {
        input(KeyDown{Key::tab});
        require(focused(id), "Tab must visit each Position component after Scale");
    }
    input(KeyDown{Key::tab, {.shift = true}});
    require(focused("position-y"), "Shift+Tab must return through the components");

    auto executions = app.executions;
    input(KeyDown{Key::up});
    require(app.state.item().position == std::array<double, 3>{0, .1, 0} &&
                app.state.item().position_draft[1].text == "0.1" && app.observed_value("position-y") == "0.1" &&
                app.executions == executions + 1,
            "Up must step only the focused component through its own command");
    input(KeyDown{Key::tab});
    input(KeyDown{Key::down});
    input(KeyDown{Key::down});
    require(app.state.item().position[2] == -.2 && app.observed_value("position-z") == "-0.2",
            "Down must step below zero without binary noise");

    app.focus_id("position-x");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"12.5"});
    require(app.state.item().position[0] == 0 && app.observed_value("position-x") == "12.5" &&
                caption() == "Position *",
            "Typing must change only that component's draft and mark Position pending");
    executions = app.executions;
    input(KeyDown{Key::enter});
    require(app.state.item().position == std::array<double, 3>{12.5, .1, -.2} && caption() == "Position" &&
                app.executions == executions + 1 && app.feedback.empty(),
            "Enter must apply the focused component");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"x"});
    const auto invalid = app.state.item();
    input(KeyDown{Key::enter});
    require(app.state.item() == invalid && app.edit_diagnostics.front().code == "number_syntax" &&
                app.feedback == "Position could not finish. Enter a number from -1000 to 1000.",
            "An invalid component draft must be rejected with Position feedback");
    input(KeyDown{Key::escape});
    require(app.state.item().position_draft[0].text == "12.5" && app.feedback.empty() && caption() == "Position",
            "Escape must restore the component's accepted text");

    const auto point = app.view.reachable("position-z-field");
    executions = app.executions;
    input(PointerDown{{1}, point}, CommandSource::pointer);
    input(PointerUp{{1}, point}, CommandSource::pointer);
    require(focused("position-z") && app.executions == executions, "Clicking a component must only focus it");
    const auto x = app.view.box("position-x").border_box;
    const auto z = app.view.box("position-z").border_box;
    require(std::abs(x.size.width - z.size.width) < 1e-3f && z.origin.x > x.origin.x &&
                std::abs(x.origin.x - app.view.box("scale").border_box.origin.x) < 1e-3f,
            "Components must share the control column equally from its leading edge");
    require(app.view.tree->find("modified-badge").has_value(), "A moved item must show the Modified badge");

    app.action("hierarchy-item-sphere");
    require(app.state.item().position == std::array<double, 3>{} && app.observed_value("position-x") == "0",
            "Another item must show its own Position");
    app.action("hierarchy-item-cube");
    require(app.state.item().position == std::array<double, 3>{12.5, .1, -.2} &&
                app.observed_value("position-x") == "12.5",
            "Returning to an item must retain its Position");
    app.resize({492, 540});
    const auto pane = app.view.box("inspector").padding_box();
    const auto narrow = app.view.box("position-z").border_box;
    require(narrow.origin.x + narrow.size.width <= pane.origin.x + pane.size.width &&
                std::abs(app.view.box("position-x").border_box.size.width - narrow.size.width) < 1e-3f,
            "Position components must fit the minimum Inspector pane");
    const auto revision = app.state.item().position_draft[0].revision;
    app.action("tool-reset");
    require(app.state.item().position == std::array<double, 3>{} && app.state.item().position_draft[0].text == "0" &&
                app.state.item().position_draft[0].revision == revision + 1 && !app.view.tree->find("modified-badge"),
            "Reset must restore Position and clear the badge");
}
void scrub_smoke() {
    App app;
    // Leave room below the card for every General row.
    app.resize({960, 540});
    std::chrono::microseconds time{};
    auto input = [&](auto event) { app.input({++time, event}, CommandSource::pointer); };
    const auto focused = [&](std::string_view id) {
        return take(app.focus.refresh(app.view.hit())).focused == app.view.tree->find(id);
    };
    const Color pressed{.8f, .87f, .98f, 1}; // The active control background.
    const auto active = [&](std::string_view id) {
        return app.view.styles[app.view.tree->find(id)->index].background == pressed;
    };
    app.focus_id("name");
    const auto start = app.view.reachable("scale-label");
    const auto at = [&](float dx, float dy = 0) { return Point{start.x + dx, start.y + dy}; };
    require(app.horizontal_cursor(start) && app.horizontal_cursor(app.view.reachable("position-y-field")) &&
                !app.horizontal_cursor(app.view.reachable("scale-increase")) &&
                !app.horizontal_cursor(app.view.reachable("name")),
            "Scrubbable fields, not step parts or text inputs, must show the horizontal cursor");
    auto executions = app.executions;
    input(PointerDown{{1}, start});
    require(focused("scale") && active("scale") && app.executions == executions,
            "A press on the Scale label must focus and press Scale without a command");
    input(PointerMove{{1}, at(2, 20)});
    require(app.executions == executions, "Travel below the threshold must not scrub");
    input(PointerMove{{1}, at(12, 30)});
    require(app.state.item().scale == 1.3 && app.state.item().scale_draft.text == "1.3" &&
                app.observed_value("scale") == "1.3" && app.executions == executions + 1 && active("scale"),
            "Dragging right must step Scale by 0.1 per 4 units through one command");
    input(PointerMove{{1}, at(15.5f)});
    require(app.executions == executions + 1, "An unchanged step count must not run a command");
    input(PointerMove{{1}, at(4000)});
    input(PointerMove{{1}, at(5000)});
    require(app.state.item().scale == 10 && app.executions == executions + 2,
            "Scrubbing must clamp to the maximum and skip unchanged clamped values");
    input(PointerMove{{1}, at(-8)});
    require(app.state.item().scale == .8 && app.observed_value("scale") == "0.8",
            "Scrubbing back must step from the start value without binary noise");
    input(KeyDown{Key::escape});
    require(app.state.item().scale == 1 && app.state.item().scale_draft.text == "1" && !active("scale"),
            "Escape must restore the value the scrub started at");
    executions = app.executions;
    input(PointerUp{{1}, at(30)});
    require(app.state.item().scale == 1 && app.executions == executions,
            "The release after cancellation must not run a command");

    input(PointerDown{{1}, start});
    input(PointerUp{{1}, at(20)});
    require(app.state.item().scale == 1.5 && app.feedback.empty(), "A release must apply its own steps");
    executions = app.executions;
    input(PointerDown{{1}, start});
    input(PointerUp{{1}, at(1)});
    require(app.executions == executions && focused("scale") && app.state.item().scale == 1.5,
            "A click on the label must only focus Scale");

    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"2.5"});
    input(PointerDown{{1}, start});
    input(PointerMove{{1}, at(-8)});
    require(app.state.item().scale == 2.3 && app.state.item().scale_draft.text == "2.3",
            "A valid pending draft must be the start value");
    input(PointerCancel{{1}});
    require(app.state.item().scale == 2.3 && !active("scale"), "PointerCancel must keep the last scrubbed value");
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"2,5"});
    const auto invalid = app.state.item();
    input(PointerDown{{1}, start});
    input(PointerMove{{1}, at(12)});
    require(app.state.item() == invalid && app.feedback == "Scale could not finish. Enter a number from 0 to 10.",
            "An invalid draft must reject the scrub and keep its text");
    input(PointerMove{{1}, at(30)});
    input(PointerUp{{1}, at(30)});
    require(app.state.item() == invalid, "A rejected scrub must not resume");
    input(KeyDown{Key::escape});
    require(app.state.item().scale_draft.text == "2.3", "Escape must still revert the draft afterwards");

    const auto y = app.view.reachable("position-y-label");
    input(PointerDown{{1}, y});
    input(PointerMove{{1}, Point{y.x - 20, y.y}});
    require(focused("position-y") && app.state.item().position == std::array<double, 3>{0, -.5, 0} &&
                app.observed_value("position-y") == "-0.5" && active("position-y"),
            "A Position component must scrub through its own command");
    input(KeyDown{Key::tab});
    require(focused("position-z") && !active("position-y") && app.state.item().position[1] == -.5,
            "Tab must end the scrub, keep its value and move focus");
    executions = app.executions;
    input(PointerMove{{1}, Point{y.x + 40, y.y}});
    input(PointerUp{{1}, Point{y.x + 40, y.y}});
    require(app.executions == executions, "Moves after an ended scrub must not run commands");
    const auto point = app.view.reachable("scale-increase");
    input(PointerDown{{1}, point});
    input(PointerUp{{1}, point});
    require(app.state.item().scale == 2.4, "Step parts must keep stepping once per press");

    const auto edited = app.state.item();
    app.action("hierarchy-item-sphere");
    require(app.state.item().scale == 1 && app.state.item().position[1] == 0, "Scrubs must edit only their item");
    app.action("hierarchy-item-cube");
    require(app.state.item() == edited, "The scrubbed item must retain its values");
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
    require(app.try_number(0, NumberStep{5}).status == CommandOutcomeStatus::success, "Scale must step before reset");
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
// The selected Hierarchy row is raised above its siblings and the focused row above it; compares actual pixels
// with the same view painted with every z_index cleared.
void stacking_smoke() {
    App app;
    const auto style = [&](std::string_view id) -> const ResolvedStyle& {
        return app.view.styles[app.view.tree->find(id)->index];
    };
    const auto cube = app.view.box("hierarchy-item-cube").border_box;
    const auto sphere = app.view.box("hierarchy-item-sphere").border_box;
    require(app.state.selected == "cube" && style("hierarchy-item-cube").z_index == 1 &&
                style("hierarchy-item-sphere").z_index == 0 && style("hierarchy-item-cube").box_shadow &&
                sphere.origin.y - (cube.origin.y + cube.size.height) == 4,
            "The selected row must be raised with an elevation shadow above a 4-unit gap");
    CpuFrameCapture frames;
    const auto capture = [&](const std::vector<ResolvedStyle>& styles) {
        auto paint = take(build_paint_list({app.view.tree.get(), styles, &app.view.layout, &app.text}));
        std::erase_if(paint.commands,
                      [](const auto& command) { return std::holds_alternative<DrawGlyphRun>(command); });
        return take(frames.capture({0, app.viewport, 1, take(capture_extent(app.viewport, 1)), &paint}));
    };
    const auto flat = [&] {
        auto styles = app.view.styles;
        for (auto& entry : styles) entry.z_index = 0;
        return styles;
    };
    // Counts differing pixels between two captures inside a logical band.
    const auto changes = [](const CapturedImage& a, const CapturedImage& b, const Rect& band) {
        std::size_t result = 0;
        for (unsigned y = unsigned(band.origin.y); y < unsigned(band.origin.y + band.size.height); ++y)
            for (unsigned x = unsigned(band.origin.x); x < unsigned(band.origin.x + band.size.width); ++x) {
                const auto at = (std::size_t(y) * a.extent.width + x) * 4;
                if (a.pixels[at] != b.pixels[at] || a.pixels[at + 1] != b.pixels[at + 1] ||
                    a.pixels[at + 2] != b.pixels[at + 2])
                    ++result;
            }
        return result;
    };
    const Rect overlap{{sphere.origin.x + 8, sphere.origin.y}, {sphere.size.width - 16, 4}};
    const Rect body{{sphere.origin.x + 8, sphere.origin.y + 6}, {sphere.size.width - 16, sphere.size.height - 12}};
    const Point covered{sphere.origin.x + sphere.size.width / 2, sphere.origin.y + 1};
    const auto hovered_layout = app.view.layout;
    app.input({0us, PointerMove{{1}, covered}}, CommandSource::pointer);
    require(app.view.layout == hovered_layout && style("hierarchy-item-sphere").background == Color{.9f, .94f, 1, 1} &&
                take(hit_test(app.view.hit(), covered)).target == app.view.tree->find("hierarchy-item-sphere"),
            "Raising a row must not move rows or steal hits from the hovered row beneath its shadow");
    auto raised = capture(app.view.styles);
    auto lowered = capture(flat());
    require(changes(raised, lowered, overlap) >= 32 && changes(raised, lowered, body) == 0,
            "The raised selection shadow must stay above the next row's hover fill, and only there");
    app.input({0us, PointerMove{{1}, app.view.center("actions")}}, CommandSource::pointer);
    app.focus_id("hierarchy-item-sphere");
    require(style("hierarchy-item-sphere").z_index == 2 && style("hierarchy-item-cube").z_index == 1,
            "The focused row must stack above the raised selection");
    raised = capture(app.view.styles);
    auto partial = app.view.styles;
    partial[app.view.tree->find("hierarchy-item-sphere")->index].z_index = 0;
    lowered = capture(partial);
    const Rect ring{{sphere.origin.x + 8, sphere.origin.y - 3}, {sphere.size.width - 16, 3}};
    require(changes(raised, lowered, ring) >= 16,
            "The focus ring must paint above the selected row's shadow in the gap between them");
    app.action("hierarchy-item-sphere");
    require(style("hierarchy-item-sphere").z_index == 2 && style("hierarchy-item-cube").z_index == 0 &&
                !style("hierarchy-item-cube").box_shadow && style("hierarchy-item-sphere").box_shadow,
            "Elevation must follow the selection");
}
void notes_smoke() {
    using Kind = ImeStep::Kind;
    App app;
    std::chrono::microseconds time{};
    auto input = [&](auto event) { app.input({++time, event}); };
    const auto notes = [&]() -> const TextEditState& { return app.state.item().notes; };
    const auto replace = [&](std::string text) {
        const auto& current = notes();
        require(
            app.try_text(TextField::notes, {current.owner, current.revision, TextEditSelection{0, current.text.size()}})
                        .status == CommandOutcomeStatus::success &&
                app.try_text(TextField::notes, {notes().owner, notes().revision, TextEditInsert{std::move(text)}})
                        .status == CommandOutcomeStatus::success,
            "Notes fixture edit failed");
    };
    const auto cube_notes = notes();
    const auto& initial = app.observed("notes");
    require(initial.role == SemanticRole::text_editor && initial.name == "Notes" &&
                initial.value == "Unit cube for layout checks.\nKeep it at the origin." &&
                initial.actions == std::vector<SemanticAction>{{"activate", "item.notes"}} &&
                cube_notes.lines == TextLines::multiple,
            "Notes must expose a named text editor with the item's multiple-line text");
    app.focus_id("position-z");
    input(KeyDown{Key::tab});
    require(app.focused_on("notes"), "Tab must reach Notes after Position");

    // Enter and Shift+Enter insert LF through the ordinary notes command.
    auto executions = app.executions;
    input(KeyDown{Key::a, {.control = true}});
    input(tessera::TextInput{"abcdef"});
    input(KeyDown{Key::enter});
    input(tessera::TextInput{"x"});
    input(KeyDown{Key::enter, {.shift = true}});
    input(tessera::TextInput{"abcdef"});
    require(notes().text == "abcdef\nx\nabcdef" && notes().caret == 15 &&
                app.observed_value("notes") == "abcdef\nx\nabcdef" && app.executions == executions + 6 &&
                app.state.item().name.text == "Cube" && app.feedback.empty(),
            "Typing and Enter must edit only the notes, one command each");
    const auto [rows, origin] = *app.caret_layout(TextField::notes, notes().text);
    require(rows.lines.size() == 3 && app.view.box("notes-value").content_box().size.height == 3 * rows.line_height,
            "Each LF must present a row and grow the editor");

    // Up/Down keep the column where vertical movement began; Shift extends.
    input(KeyDown{Key::up});
    require(notes().anchor == 8 && notes().caret == 8, "Up from a long row must stop at the end of a short row");
    input(KeyDown{Key::up});
    require(notes().anchor == 6 && notes().caret == 6, "A second Up must return to the starting column");
    input(KeyDown{Key::down, {.shift = true}});
    input(KeyDown{Key::down, {.shift = true}});
    require(notes().anchor == 6 && notes().caret == 15, "Shift+Down must extend while keeping the goal column");
    input(KeyDown{Key::up});
    input(KeyDown{Key::up});
    input(KeyDown{Key::up});
    require(notes().caret == 6 && notes().anchor == 6, "Up on the first row must stay on that row");
    input(KeyDown{Key::left});
    input(KeyDown{Key::down});
    input(KeyDown{Key::down});
    require(notes().caret == 14, "Another edit must restart the goal column from the caret");

    // Home/End address the caret's row; Control+Home/End address the buffer.
    input(KeyDown{Key::home});
    require(notes().anchor == 9 && notes().caret == 9, "Home must move to the start of the caret's row");
    input(KeyDown{Key::end, {.shift = true}});
    require(notes().anchor == 9 && notes().caret == 15, "Shift+End must extend to the end of the row");
    input(KeyDown{Key::home, {.control = true}});
    require(notes().anchor == 0 && notes().caret == 0, "Control+Home must move to the buffer start");
    input(KeyDown{Key::end, {.shift = true, .control = true}});
    require(notes().anchor == 0 && notes().caret == 15, "Control+Shift+End must select to the buffer end");
    input(KeyDown{Key::backspace});
    require(notes().text.empty() && app.observed_value("notes").empty(), "Backspace must delete across lines");

    // Long text wraps in the narrow control column; Up/Down and End follow the wrapped rows.
    app.resize({492, 360});
    replace(std::string(60, 'w'));
    const auto [wrapped, wrapped_origin] = *app.caret_layout(TextField::notes, notes().text);
    const auto last = wrapped.line_of(60);
    require(wrapped.lines.size() >= 3 && !wrapped.lines[0].hard && last == wrapped.lines.size() - 1 &&
                app.view.box("notes-value").content_box().size.height ==
                    static_cast<float>(wrapped.lines.size()) * wrapped.line_height,
            "A long notes line must wrap into the control column");
    const auto x = wrapped.caret(60).origin.x;
    input(KeyDown{Key::up});
    require(notes().caret == wrapped.offset_at(last - 1, x) && wrapped.line_of(notes().caret) == last - 1,
            "Up must move by wrapped row within one line");
    input(KeyDown{Key::end});
    require(notes().caret == wrapped.line_end(last - 1) && wrapped.line_of(notes().caret) == last - 1,
            "End must stay on its wrapped row");
    input(KeyDown{Key::home});
    require(notes().caret == wrapped.lines[last - 1].begin, "Home must move to the wrapped row's start");
    app.resize({960, 360});

    // Paste normalizes platform line breaks; copy keeps LF for the host adapter.
    std::string clipboard;
    app.clipboard_read = [&]() -> Result<std::string> { return {clipboard, {}}; };
    app.clipboard_write = [&](std::string_view text) {
        clipboard = text;
        return std::vector<Diagnostic>{};
    };
    replace("abcdef\nx\nabcdef");
    require(app.try_text(TextField::notes, {notes().owner, notes().revision, TextEditSelection{3, 9}}).status ==
                CommandOutcomeStatus::success,
            "Notes selection failed");
    input(KeyDown{Key::c, {.control = true}});
    require(clipboard == "def\nx\n" && notes().text == "abcdef\nx\nabcdef", "Copy must keep the selected LF bytes");
    clipboard = "one\r\ntwo\rthree";
    input(KeyDown{Key::v, {.control = true}});
    require(notes().text == "abcone\ntwo\nthreeabcdef" && notes().caret == 16 && app.feedback.empty(),
            "Paste must replace the selection with CRLF and CR normalized to LF");
    input(KeyDown{Key::x, {.control = true}});
    require(notes().text == "abcone\ntwo\nthreeabcdef", "Cut with an empty selection must do nothing");

    // Japanese composition presents in the notes and places candidates on the caret's row.
    replace("ab\ncd");
    executions = app.executions;
    app.ime({Kind::start});
    app.ime({Kind::update, "\xe3\x81\xab\xe3\x81\xbb", 6}); // niho preedit
    require(app.composing() && notes().composition && notes().text == "ab\ncd" &&
                app.observed_value("notes") == "ab\ncd\xe3\x81\xab\xe3\x81\xbb" && app.executions == executions + 2,
            "Preedit must appear in the notes through ordinary edit commands");
    const auto [shown, shown_origin] = *app.caret_layout(TextField::notes, "ab\ncd\xe3\x81\xab\xe3\x81\xbb");
    const float advance = app.view.styles[app.view.tree->find("notes-value")->index].text.size * .5f;
    const auto caret = app.caret_rect();
    require(caret && caret->origin.y == shown_origin.y + shown.line_height &&
                caret->origin.x == shown_origin.x + 6 * advance,
            "Candidate placement must follow the preedit caret on its row");
    app.ime({Kind::commit, "\xe6\x97\xa5\xe6\x9c\xac"}); // Japanese kanji
    require(!app.composing() && notes().text == "ab\ncd\xe6\x97\xa5\xe6\x9c\xac" && notes().caret == 11,
            "Commit must replace the captured notes range once");
    app.ime({Kind::start});
    app.ime({Kind::update, "\xe3\x81\x82", 3});
    input(KeyDown{Key::tab});
    require(!app.composing() && notes().text == "ab\ncd\xe6\x97\xa5\xe6\x9c\xac",
            "Focus movement must cancel notes preedit without committing it");

    // The name editor stays single-line: Enter changes nothing.
    app.focus_id("name");
    const auto name = app.state.item().name;
    executions = app.executions;
    input(KeyDown{Key::enter});
    require(app.state.item().name == name && app.executions == executions, "Enter must not edit the name");

    // Oversized notes reject with recoverable feedback.
    app.focus_id("notes");
    const auto before = notes();
    input(tessera::TextInput{std::string(max_text_edit_bytes, 'n')});
    require(notes() == before && app.feedback == "Notes edit could not finish. Text must fit within 4096 UTF-8 bytes.",
            "Oversized notes must reject and keep the accepted text");

    // Notes stay per item and survive Reset like the display name.
    const auto edited = notes();
    app.action("hierarchy-item-sphere");
    require(notes().text.empty() && app.observed_value("notes").empty() && notes().owner != edited.owner,
            "Each item must own its notes");
    app.action("hierarchy-item-cube");
    require(notes() == edited && app.observed_value("notes") == edited.text, "Selection must retain edited notes");
    app.action("tool-reset");
    require(notes() == edited && !app.view.tree->find("modified-badge"),
            "Reset must keep notes, and notes must not mark the item modified");
}
void smoke() {
    sections_smoke();
    notes_smoke();
    placement_smoke();
    toolbar_smoke();
    scale_smoke();
    position_smoke();
    scrub_smoke();
    visibility_smoke();
    shadows_smoke();
    purpose_smoke();
    color_smoke();
    appearance_smoke();
    gradient_card_smoke();
    analytic_fill_smoke();
    shadow_card_smoke();
    stacking_smoke();
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
    // Text and numeric inputs take committed WM_CHAR text and Return; only the name and notes editors attach the IME.
    window.editing = [&] {
        const auto focused = take(app.focus.refresh(app.view.hit())).focused;
        return app.focused_text(focused) || app.focused_number(focused);
    };
    window.ime_editing = [&] { return app.focused_text(take(app.focus.refresh(app.view.hit())).focused).has_value(); };
    window.resized = [&](Size viewport) { app.resize(viewport); };
    window.resize_cursor = [&](Point point) { return app.horizontal_cursor(point); };
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
        seek("position-y");
        control_key('A');
        for (const auto unit : {L'-', L'3'}) host.message(WM_CHAR, unit);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        require(app.state.item().position == std::array<double, 3>{0, -3.1, 0} && app.state.item().scale == 2.5,
                "Native characters, Return and Down must edit only the focused Position component");
        // Dragging the X label scrubs only that component under OS capture, with the resize cursor.
        const auto x_label = app.view.reachable("position-x-label");
        const auto grab = host.physical(x_label);
        const auto scrubbed = host.physical({x_label.x + 12, x_label.y});
        require(window.resize_cursor(x_label) && !window.resize_cursor(app.view.reachable("name")),
                "Native scrubbable fields must request the horizontal cursor");
        host.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(grab.x, grab.y));
        require(GetCapture() == host.handle && app.focused_on("position-x"), "A native scrub press must capture");
        host.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(scrubbed.x, scrubbed.y));
        host.message(WM_LBUTTONUP, 0, MAKELPARAM(scrubbed.x, scrubbed.y));
        require(app.state.item().position == std::array<double, 3>{.3, -3.1, 0} && app.state.item().scale == 2.5 &&
                    app.observed_value("position-x") == "0.3",
                "A native drag must scrub only the Position component under the pointer");
        // Notes take committed characters, Return as a line break and Up by row, with the IME attached.
        seek("notes");
        require(window.editing() && host.ime_attached(), "Native Notes must accept text and attach the IME");
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        for (const auto unit : {L'O', L'K'}) host.message(WM_CHAR, unit);
        host.message(WM_KEYDOWN, VK_UP, 1);
        require(app.state.item().notes.text == "Unit cube for layout checks.\nKeep it at the origin.\nOK" &&
                    app.state.item().notes.caret == 31 && app.state.item().position[1] == -3.1,
                "Native Return, characters and Up must edit only the notes, by row");
        // Shift+Tab back from Notes and Position reaches the tabs instead of wrapping the Hierarchy.
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
        seek("purpose");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        require(app.state.menu == OpenMenu::purpose && app.focused_on("purpose-option-default"),
                "Native Space must open the Purpose list at its selection");
        host.message(WM_KEYDOWN, VK_DOWN, 1);
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.item().purpose == "render" && app.state.menu == OpenMenu::none && app.focused_on("purpose"),
                "Native arrows and Return must choose a Purpose");
        seek("display-color");
        host.message(WM_KEYDOWN, VK_SPACE, 1);
        require(app.state.menu == OpenMenu::color && app.focused_on("display-color-area"),
                "Native Space must open the Display color panel at its area");
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        host.message(WM_KEYDOWN, VK_UP, 1);
        host.message(WM_KEYDOWN, VK_TAB, 1);
        host.message(WM_KEYDOWN, VK_RIGHT, 1);
        require(app.focused_on("display-color-hue") && app.state.menu == OpenMenu::color,
                "Native Tab must move within the open Display color panel");
        host.message(WM_KEYDOWN, VK_RETURN, 1);
        require(app.state.item().color == HsvColor{5, 0.05, 0.55} && app.state.menu == OpenMenu::none &&
                    app.focused_on("display-color"),
                "Native arrows and Return must adjust the Display color and close its panel");
        const auto cube = app.state.item();
        require(cube.name.text == "Cube!" && !cube.enabled && cube.amount == 55 && cube.visibility == "hidden" &&
                    !cube.shadows && cube.purpose == "render" && cube.color == HsvColor{5, 0.05, 0.55},
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
                    reset.visibility == "inherited" && reset.shadows && reset.purpose == "default" &&
                    reset.color == default_color,
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
