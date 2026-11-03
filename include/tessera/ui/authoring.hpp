#pragma once

#include <tessera/reactive/runtime.hpp>
#include <tessera/input/text_edit.hpp>
#include <tessera/style/style_sheet.hpp>
#include <tessera/ui/serialization.hpp>

namespace tessera::authoring {
namespace detail { struct ElementData; struct Lowerer; }

// Owned literals or weak references to the ordinary reactive graph.
template<class T>
class Value final {
public:
    Value(T value) : value_(std::move(value)) {}
    Value(Signal<T> value) : value_(std::move(value)) {}
    Value(Computed<T> value) : value_(std::move(value)) {}
    Value(const char* value) requires std::is_same_v<T, std::string> : value_(std::string(value)) {}
private:
    T snapshot(const ReactiveRuntime& runtime) const {
        return std::visit([&](const auto& value) -> T {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, T>) return value;
            else return runtime.snapshot(value);
        }, value_);
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
struct TextInputProps {
    Props node;
    Value<bool> disabled = false;
};
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
    friend Element TextInput(Value<std::string>, Value<TextEditState>, std::string, TextInputProps);
    friend Element Row(std::vector<Element>, Props);
    friend Element Column(std::vector<Element>, Props);
    friend Element Stack(std::vector<Element>, Props);
    friend Element Dropdown(std::string, std::string, std::vector<DropdownOption>, std::string, DropdownProps);
    friend Element Slider(std::string, int, SliderRange, std::string, SliderProps);
    friend Element HorizontalSplitPane(Element, Element, float, std::string, SplitPaneProps);
    friend Element VerticalSplitPane(Element, Element, float, std::string, SplitPaneProps);
};

Element Text(Value<std::string>, Props = {});
// An ordinary activate action; the host maps it to a CommandRegistry descriptor.
Element Button(Value<std::string> label, std::string action, ButtonProps = {});
// Controlled value: activation only requests a host command. The Box/Text
// composition exposes "[x]" / "[ ]" in its ordinary button semantic name.
Element Checkbox(Value<std::string> label, Value<bool> checked, std::string action, CheckboxProps = {});
// Controlled single-line observation: label, committed text, then byte selection
// as ordinary Text children. The host routes edits through this activate owner.
Element TextInput(Value<std::string> label, Value<TextEditState> value, std::string action, TextInputProps = {});
Element Row(std::vector<Element> children, Props = {});
Element Column(std::vector<Element> children, Props = {});
// Overlapping children in one content box, later children on top. Each child's
// stack_horizontal/stack_vertical selects start/center/end/stretch independently.
// Ordinary clipping, visibility, focus and action routing still apply; no portal.
Element Stack(std::vector<Element> children, Props = {});
// A Column with scroll overflow. Constrain its size through Props::style;
// the host owns offsets and routes wheel input/focus reveal through input/scroll.
Element ScrollView(std::vector<Element> children, Props = {});
// Controlled inline dropdown: host commands own expansion and selected key.
// With a node ID, generated controls use <id>-trigger / <id>-option-<key>.
// Options are ordinary buttons in layout, reached through normal focus/scroll.
Element Dropdown(std::string label, std::string selected, std::vector<DropdownOption> options,
    std::string toggle_action, DropdownProps = {});
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
    template<class T>
    Signal<T> state(std::string name, T initial) {
        return runtime_.signal(runtime_.root(), std::move(name), std::move(initial));
    }
    template<class Calculate>
    auto computed(std::string name, Calculate calculate) {
        using T = std::decay_t<std::invoke_result_t<Calculate&>>;
        return runtime_.computed<T>(runtime_.root(), std::move(name), std::move(calculate));
    }
    template<class Update>
    void update(Update update) { runtime_.batch(std::move(update)); }

    // Preflights the whole description, settles the graph, then copies accepted
    // values into a validated candidate. Does not replace/publish host snapshots.
    Result<Presentation> lower(const Element&, const ValidationContext& = {});
    // Call only after the host accepts style/layout/semantic/paint stages.
    void publish() { (void)runtime_.publish(); }
private:
    ReactiveRuntime runtime_;
};
} // namespace tessera::authoring
