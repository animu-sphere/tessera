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
