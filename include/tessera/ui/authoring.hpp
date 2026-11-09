#pragma once

#include <tessera/reactive/runtime.hpp>
#include <tessera/input/text_edit.hpp>
#include <tessera/style/style_sheet.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/layout/layout_box.hpp>

namespace tessera::authoring {
namespace detail {
struct ElementData;
struct Lowerer;
}

// Owned literals or weak references to the ordinary reactive graph.
template<class T> class Value final {
public:
    Value(T value) : value_(std::move(value)) {}
    Value(Signal<T> value) : value_(std::move(value)) {}
    Value(Computed<T> value) : value_(std::move(value)) {}
    Value(const char* value)
        requires std::is_same_v<T, std::string>
        : value_(std::string(value)) {}

private:
    T snapshot(const ReactiveRuntime& runtime) const {
        return std::visit(
            [&](const auto& value) -> T {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, T>)
                    return value;
                else
                    return runtime.snapshot(value);
            },
            value_);
    }
    std::variant<T, Signal<T>, Computed<T>> value_;
    friend struct detail::Lowerer;
};

struct Props {
    std::optional<std::string> id;
    std::vector<std::string> classes;
    StyleDeclarations style;
    // Optional caller-declared UTF-8 expression range, never a C++ line number.
    std::optional<SourceLocation> source;
};
struct ButtonProps {
    Props node;
    Value<bool> disabled = false;
};
struct CheckboxProps {
    Props node;
    Value<bool> disabled = false;
};
using SwitchProps = CheckboxProps;
struct TextInputProps {
    Props node;
    Value<bool> disabled = false;
};
using NumberInputProps = TextInputProps;
// Inclusive decimal bounds and a positive adjustment step for NumberInput drafts.
// Direct entry need not land on a step.
struct NumberRange {
    double minimum = 0;
    double maximum = 100;
    double step = 1;
};
// Whole-string, locale-independent ASCII -?(0|[1-9][0-9]*)(\.[0-9]+)? within the bounds;
// rejects whitespace, '+', exponents, NaN/infinity and empty text. Negative zero is zero.
Result<double> parse_number(NumberRange, std::string_view text);
// Shortest fixed decimal that round-trips the value; zero is "0", nonfinite values are "".
std::string format_number(double);
// Moves `steps` steps from a finite value, clamps to the bounds and keeps the decimal places of
// the value and step, so 0.2 + 0.1 yields 0.3.
Result<double> step_number(NumberRange, double value, int steps);
struct DropdownOption {
    std::string key;
    std::string label;
    std::string action;
    bool disabled = false;
};
struct DropdownProps {
    Props node;
    bool expanded = false;
    bool disabled = false;
};
// Integer stops include both endpoints; step must divide the range exactly.
struct SliderRange {
    int minimum = 0;
    int maximum = 100;
    int step = 1;
};
struct SliderProps {
    Props node;
    bool disabled = false;
};
// Clamp a finite normalized position to [0,1], then round to the nearest stop
// (ties toward maximum). Invalid ranges/positions return actionable diagnostics.
Result<int> slider_value_at(SliderRange, double position);

struct SplitPaneLimits {
    float minimum_first = 80;
    float minimum_second = 80;
    float handle_size = 8;
};
struct SplitPaneProps {
    Props node; // A stable ID is required; generated parts use this ID as a prefix.
    SplitPaneLimits limits;
    std::string label = "Resize panes";
    bool disabled = false;
};
struct ScrollbarProps {
    Props node; // Required stable ID; reserve <id>-before, <id>-thumb and <id>-label.
    float minimum_thumb = 18;
    bool disabled = false;
    bool hovered = false;
    bool focused = false;
};
enum class PopupSide { bottom, top, left, right };
struct PopupProps {
    Props node;
    bool open = false;
    PopupSide side = PopupSide::bottom;
    float gap = 4;
};
struct ModalProps {
    Props node; // Required stable ID; compose one Modal directly under the root, last.
    bool open = false;
};
struct DialogProps {
    Props node;
    Props panel; // Defaults to centered, width-bounded, scrollable content.
    bool open = false;
};
struct ListItem {
    std::string key;
    std::string label;
    std::string action;
    bool disabled = false;
};
using MenuItem = ListItem;
using RadioOption = ListItem;
using TabOption = ListItem;
struct TreeItem {
    std::string key;
    std::string label;
    std::string action;
    std::string toggle_action; // Required for branches; both expand/collapse request this command.
    bool expanded = false;
    bool disabled = false;
    std::vector<TreeItem> children;
};
struct CollectionProps {
    Props node; // Required stable ID; item keys are unique across the collection.
    bool disabled = false;
};
using ComboBoxOption = ListItem;
struct ComboBoxProps {
    Props node; // Required stable trigger ID; generated parts use it as a prefix.
    Props list; // Classes, styles and source for the <id>-list popup; its ID is generated.
    bool open = false;
    bool disabled = false; // Disables the trigger and every option.
    PopupSide side = PopupSide::bottom;
    float gap = 4;
};
class Element;
struct ComboBoxParts;
// If the parent cannot hold both minima, keep them and clip the overflow.
Result<float> split_pane_size_at(SplitPaneLimits, float available, float requested);

// Immutable composition values. Copying shares descriptions, never state/owners.
class Element final {
public:
    Element(const Element&) = default;
    Element(Element&&) noexcept = default;
    Element& operator=(const Element&) = default;
    Element& operator=(Element&&) noexcept = default;

private:
    explicit Element(std::shared_ptr<const detail::ElementData> data) : data_(std::move(data)) {}
    std::shared_ptr<const detail::ElementData> data_;
    friend struct detail::Lowerer;
    friend Element Text(Value<std::string>, Props);
    friend Element Button(Value<std::string>, std::string, ButtonProps);
    friend Element Checkbox(Value<std::string>, Value<bool>, std::string, CheckboxProps);
    friend Element Switch(Value<std::string>, Value<bool>, std::string, SwitchProps);
    friend Element TextInput(Value<std::string>, Value<TextEditState>, std::string, TextInputProps);
    friend Element NumberInput(Value<std::string>, Value<TextEditState>, std::string, NumberInputProps);
    friend Element Row(std::vector<Element>, Props);
    friend Element Column(std::vector<Element>, Props);
    friend Element Stack(std::vector<Element>, Props);
    friend Element Grid(std::vector<Element>, Props);
    friend Element Popup(std::string, std::vector<Element>, PopupProps);
    friend Element Tooltip(std::string, Value<std::string>, PopupProps);
    friend Element Menu(std::string, std::string, std::vector<MenuItem>, PopupProps);
    friend Element ContextMenu(std::string, std::string, Point, std::vector<MenuItem>, PopupProps);
    friend Element Modal(std::vector<Element>, ModalProps);
    friend Element Dialog(Value<std::string>, std::vector<Element>, DialogProps);
    friend Element ListView(std::string, std::optional<std::string>, std::vector<ListItem>, CollectionProps);
    friend Element TreeView(std::string, std::optional<std::string>, std::vector<TreeItem>, CollectionProps);
    friend Element RadioGroup(std::string, std::optional<std::string>, std::vector<RadioOption>, CollectionProps);
    friend Element Tabs(std::string, std::string, std::vector<TabOption>, std::vector<Element>, CollectionProps);
    friend Element Toolbar(std::string, std::vector<Element>, Props);
    friend Element Dropdown(std::string, std::string, std::vector<DropdownOption>, std::string, DropdownProps);
    friend ComboBoxParts ComboBox(std::string, std::string, std::vector<ComboBoxOption>, std::string, ComboBoxProps);
    friend Element Slider(std::string, int, SliderRange, std::string, SliderProps);
    friend Element HorizontalSplitPane(Element, Element, float, std::string, SplitPaneProps);
    friend Element VerticalSplitPane(Element, Element, float, std::string, SplitPaneProps);
};

Element Text(Value<std::string>, Props = {});
// An ordinary activate action; the host maps it to a CommandRegistry descriptor.
Element Button(Value<std::string> label, std::string action, ButtonProps = {});
// Controlled value: activation only requests a host command. A row with a
// presentational "[x]" / "[ ]" marker Text and its <id>-label Text; it projects a
// checkbox named by the label with `selected` as its checked state. Requires a stable ID.
Element Checkbox(Value<std::string> label, Value<bool> checked, std::string action, CheckboxProps = {});
// The same controlled contract as an on/off switch: a 32x18 rounded <id>-track whose
// <id>-thumb sits at the start when off and the end when on, then the <id>-label Text.
// Projects a switch role; the track turns blue when on. Requires a stable ID.
Element Switch(Value<std::string> label, Value<bool> on, std::string action, SwitchProps = {});
// Controlled single-line observation: <id>-label, <id>-value (committed text) and byte
// selection Text children; an open composition presents its preedit instead. Projects a
// text_input named by its label whose value is the presented text. Requires a stable ID.
// The host routes edits through this activate owner.
Element TextInput(Value<std::string> label, Value<TextEditState> value, std::string action, TextInputProps = {});
// Controlled numeric draft: a row with <id>-label, an <id>-field holding the <id>-value
// draft Text, then presentational <id>-decrease / <id>-increase step parts. Projects a
// number_input named by its label whose value is the presented draft text. The host owns
// editing, parsing (parse_number), Enter/Escape and Up/Down or step-part adjustment
// (step_number), and the accepted number. Requires a stable ID.
Element NumberInput(Value<std::string> label, Value<TextEditState> draft, std::string action, NumberInputProps = {});
Element Row(std::vector<Element> children, Props = {});
Element Column(std::vector<Element> children, Props = {});
// Overlapping children in one content box, later children on top. Each child's
// stack_horizontal/stack_vertical selects start/center/end/stretch independently.
// Ordinary clipping, visibility, focus and action routing still apply; no portal.
Element Stack(std::vector<Element> children, Props = {});
// Row-major children, one per explicit cell; no implicit tracks or spanning.
Element Grid(std::vector<Element> children, Props = {});
// Root children, after ordinary content. Missing, hidden or fully clipped anchors
// suppress the subtree. No reparenting, modal scope or focus trap.
Element Popup(std::string anchor, std::vector<Element> children, PopupProps = {});
// Noninteractive text popup; the host controls visibility from hover/focus.
Element Tooltip(std::string anchor, Value<std::string> text, PopupProps = {});
// Single-level command menus. Require a stable node ID; item IDs are <id>-item-<key>.
// ContextMenu uses a root-logical position, clamped to the viewport, while target
// remains its visibility/focus-recovery anchor. The host owns open/close state.
Element Menu(std::string label, std::string anchor, std::vector<MenuItem>, PopupProps = {});
Element ContextMenu(std::string label, std::string target, Point position, std::vector<MenuItem>, PopupProps = {});
// Controlled root-content layer. Background input/semantic actions are ineligible
// while shown; ordinary focus traversal stays inside. No Portal or nested modals.
Element Modal(std::vector<Element> children, ModalProps = {});
// Title-labelled Modal; generated IDs are <id>-panel and <id>-title.
Element Dialog(Value<std::string> title, std::vector<Element> children, DialogProps = {});
// A Column with scroll overflow. Constrain its size through Props::style;
// the host owns offsets and routes wheel input/focus reveal through input/scroll.
Element ScrollView(std::vector<Element> children, Props = {});
// Compose beside a ScrollView using its accepted padding-box height and scroll
// geometry. Recompose after wheel/reveal/resize; no second scroll state is kept.
Element VerticalScrollbar(std::string label, float viewport, ScrollGeometry, std::string action, ScrollbarProps = {});
// The same control below a ScrollView, using its accepted padding-box width and horizontal offset.
Element HorizontalScrollbar(std::string label, float viewport, ScrollGeometry, std::string action, ScrollbarProps = {});
// Controlled single selection; nullopt means no selection. Branch expansion never
// changes selection. IDs: <id>-item-<key>, -label-<key>, -toggle-<key>, and <id>-title.
// Both are scrollable Columns; constrain their size and retain host scroll offsets.
// Arrow navigation moves focus, Activate selects; tree Left/Right collapse/expand.
Element ListView(std::string label, std::optional<std::string> selected, std::vector<ListItem>, CollectionProps = {});
Element TreeView(std::string label, std::optional<std::string> selected, std::vector<TreeItem>, CollectionProps = {});
// Controlled single choice; nullopt means no option is checked. Each option is a radio
// button with a "(o)"/"( )" marker; only its label names it. IDs: <id>-title,
// <id>-option-<key> and <id>-label-<key>. A Column unless Props::style sets a direction.
// Tab enters at the checked option; arrows move focus with wrapping and request the
// newly focused option's action. Activate requests the focused option's action.
Element RadioGroup(std::string label, std::optional<std::string> selected, std::vector<RadioOption> options,
                   CollectionProps = {});
// Controlled tabs: the host owns the selected key and supplies only that tab's panel content.
// A Column with an <id>-list Row (tab_list named by the display-none <id>-title) of
// <id>-tab-<key> tabs, each named by its <id>-label-<key> Text, then the <id>-panel Column.
// Tab enters at the selected tab; Left/Right (wrapping) and Home/End move focus and request the
// newly focused tab's action; Up/Down keep geometric movement. Selected tabs show a bottom border.
Element Tabs(std::string label, std::string selected, std::vector<TabOption> tabs, std::vector<Element> panel,
             CollectionProps = {});
// A Row of ordinary controls projected as a toolbar named by the display-none <id>-title.
// Requires a stable ID. Tab enters once at the first eligible control; Left/Right (wrapping)
// and Home/End move focus without requesting actions. No collections or nested toolbars.
Element Toolbar(std::string label, std::vector<Element> controls, Props = {});
// Controlled inline dropdown: host commands own expansion and selected key.
// With a node ID, generated controls use <id>-trigger / <id>-option-<key>.
// Options are ordinary buttons in layout, reached through normal focus/scroll.
Element Dropdown(std::string label, std::string selected, std::vector<DropdownOption> options,
                 std::string toggle_action, DropdownProps = {});
// Controlled select-only combobox. `field` is the inline <id> trigger, projected as a combobox
// named by the display-none <id>-title whose value is the selected label; it holds <id>-value and
// an <id>-arrow marker and requests toggle_action. `list` is the root-sibling <id>-list listbox
// popup, at least as wide as the trigger, holding <id>-option-<key> options named by their
// <id>-label-<key> Text; the selected option is marked. Compose `field` in place and `list` directly
// under the root after ordinary content. The host owns the selected key and open state; route input
// through ComboBoxDispatcher.
struct ComboBoxParts {
    Element field;
    Element list;
};
ComboBoxParts ComboBox(std::string label, std::string selected, std::vector<ComboBoxOption> options,
                       std::string toggle_action, ComboBoxProps = {});
// Controlled button semantics with a value caption and a 200-unit track.
// Generated IDs: <id>-track, <id>-fill, <id>-thumb. The host owns input,
// commands and values; map thumb centers over track width minus 8 units.
Element Slider(std::string label, int value, SliderRange, std::string action, SliderProps = {});
// Controlled first-pane border-box size in logical units. Flex clamps it to the
// parent; the host retains the resolved size and routes adjustment commands.
// Horizontal places panes left/right; Vertical places them top/bottom. Nest to
// compose more panes. Parts: <id>-first, -handle, -handle-label, -second.
Element HorizontalSplitPane(Element first, Element second, float size, std::string action, SplitPaneProps = {});
Element VerticalSplitPane(Element first, Element second, float size, std::string action, SplitPaneProps = {});

struct Presentation {
    UiDocument document;
    std::vector<StyleDeclarations> styles; // Ordinary preorder instance overrides.
    DocumentSourceMap sources;
};

// One host-owned form lifetime on its creating UI thread. Compose once using
// ordinary typed C++ functions/props/children; no call-order state or remounting.
// All returned state/derived references expire with this session.
class Session final {
public:
    template<class T> Signal<T> state(std::string name, T initial) {
        return runtime_.signal(runtime_.root(), std::move(name), std::move(initial));
    }
    template<class Calculate> auto computed(std::string name, Calculate calculate) {
        using T = std::decay_t<std::invoke_result_t<Calculate&>>;
        return runtime_.computed<T>(runtime_.root(), std::move(name), std::move(calculate));
    }
    template<class Update> void update(Update update) { runtime_.batch(std::move(update)); }

    // Preflights the whole description, settles the graph, then copies accepted
    // values into a validated candidate. Does not replace/publish host snapshots.
    Result<Presentation> lower(const Element&, const ValidationContext& = {});
    // Call only after the host accepts style/layout/semantic/paint stages.
    void publish() { (void)runtime_.publish(); }

private:
    ReactiveRuntime runtime_;
};
} // namespace tessera::authoring
