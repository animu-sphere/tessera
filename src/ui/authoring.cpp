#include <tessera/ui/authoring.hpp>
#include <tessera/ui/property_metadata.hpp>
#include <set>
#include <algorithm>
#include <cmath>

namespace tessera::authoring {
namespace detail {
struct ElementData {
    NodeKind kind = NodeKind::box;
    Props props;
    std::optional<Value<std::string>> text;
    std::optional<Value<bool>> checked_marker;
    std::optional<Value<bool>> disabled;
    std::optional<Value<TextEditState>> editing;
    bool selection = false;
    std::optional<std::string> action;
    std::map<std::string, Property, std::less<>> properties;
    std::vector<Element> children;
    std::vector<Diagnostic> diagnostics;
};

struct Lowerer {
    static Element split_pane(Element, Element, float, std::string, SplitPaneProps, FlexDirection);
    Presentation result;
    std::vector<const ElementData*> elements;
    std::vector<Diagnostic> errors;

    bool editing_value(const ElementData& data, const TextEditState& state, UiNode& node, const std::string& path) {
        // Reuse the logical editor's complete state validation without accepting
        // its returned next revision. Lowering never mutates editing state.
        auto checked = edit_text(state, {state.owner, state.revision, TextEditSelection{state.anchor, state.caret}});
        if (!checked) {
            for (auto& error : checked.diagnostics) {
                error.path = path + "/properties/text";
                errors.push_back(std::move(error));
            }
            return false;
        }
        node.properties["text"] = data.selection ? "Selection " + std::to_string(state.anchor) + ":" +
            std::to_string(state.caret) : state.text;
        return true;
    }

    bool describe(const Element& element, UiNode& node, const std::string& path, std::size_t depth) {
        if (!element.data_) {
            errors.push_back({"authoring_empty_element", Severity::error, path,
                              "Supply a constructed element, not a moved-from description."});
            return false;
        }
        if (depth > max_document_depth || elements.size() >= max_document_nodes) {
            errors.push_back({"authoring_limit", Severity::error, path,
                              "Keep the form within document depth and node bounds."});
            return false;
        }
        const auto& data = *element.data_;
        for (auto error : data.diagnostics) {
            error.path = path;
            if (data.props.source) error.source = DiagnosticSource{*data.props.source, DiagnosticOrigin::argument};
            errors.push_back(std::move(error));
        }
        elements.push_back(&data);
        node.kind = data.kind;
        node.id = data.props.id;
        node.classes = data.props.classes;
        node.properties = data.properties;
        if (data.text) {
            const auto* literal = std::get_if<std::string>(&data.text->value_);
            node.properties["text"] = literal ? *literal : std::string{};
        }
        if (data.checked_marker) {
            const auto* literal = std::get_if<bool>(&data.checked_marker->value_);
            node.properties["text"] = std::string(literal && *literal ? "[x]" : "[ ]");
        }
        if (data.editing) {
            node.properties["text"] = std::string{};
            if (const auto* literal = std::get_if<TextEditState>(&data.editing->value_))
                (void)editing_value(data, *literal, node, path);
        }
        if (data.action) {
            node.events["activate"] = *data.action;
            node.properties["focusable"] = true;
            node.properties["disabled"] = false;
        }
        NodeSource source;
        source.pointer = path;
        source.mapped = false;
        source.invocation = data.props.source;
        source.style_argument = data.props.source;
        for (const auto& [name, value] : node.properties) {
            (void)value;
            source.unmapped_properties.insert(name);
            if (data.props.source) source.property_arguments[name] = *data.props.source;
        }
        if (data.action && data.props.source) source.event_arguments["activate"] = *data.props.source;
        result.sources.nodes.push_back(std::move(source));
        result.styles.push_back(data.props.style);
        auto style_errors = validate(data.props.style, path + "/style");
        for (auto& error : style_errors) {
            if (data.props.source) error.source = DiagnosticSource{*data.props.source, DiagnosticOrigin::argument};
            errors.push_back(std::move(error));
        }
        // Do not reserve from an unbounded child count before the traversal guard.
        for (std::size_t i = 0; i < data.children.size(); ++i) {
            node.children.emplace_back();
            if (!describe(data.children[i], node.children.back(), path + "/children/" + std::to_string(i), depth + 1))
                return false;
        }
        return true;
    }

    bool values(const ReactiveRuntime& runtime, UiNode& node, std::size_t& index) {
        const auto current = index++;
        const auto& data = *elements[current];
        std::string property;
        try {
            if (data.text) {
                property = "text";
                node.properties["text"] = data.text->snapshot(runtime);
            }
            if (data.checked_marker) {
                property = "text";
                node.properties["text"] = std::string(data.checked_marker->snapshot(runtime) ? "[x]" : "[ ]");
            }
            if (data.disabled) {
                property = "disabled";
                node.properties["disabled"] = data.disabled->snapshot(runtime);
            }
            if (data.editing) {
                property = "text";
                if (!editing_value(data, data.editing->snapshot(runtime), node,
                    result.sources.nodes[current].pointer)) return false;
            }
        } catch (const ReactiveError& failure) {
            auto error = failure.diagnostic();
            error.message += " Binding graph location: " + error.path;
            error.path = result.sources.nodes[current].pointer + "/properties/" + property;
            errors.push_back(std::move(error));
            return false;
        }
        for (auto& child : node.children) if (!values(runtime, child, index)) return false;
        return true;
    }
};
} // namespace detail

Element Text(Value<std::string> text, Props props) {
    auto data = std::make_shared<detail::ElementData>();
    data->kind = NodeKind::text;
    data->props = std::move(props);
    data->text = std::move(text);
    return Element(std::move(data));
}
Element Popup(std::string anchor, std::vector<Element> children, PopupProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::column;
    if (!data->props.style.overflow) data->props.style.overflow = Overflow::clip;
    if (!props.open) data->props.style.display = Display::none;
    data->properties["popup_anchor"] = std::move(anchor);
    data->properties["popup_gap"] = static_cast<double>(props.gap);
    std::string side;
    switch (props.side) {
    case PopupSide::bottom: side = "bottom"; break;
    case PopupSide::top: side = "top"; break;
    case PopupSide::left: side = "left"; break;
    case PopupSide::right: side = "right"; break;
    }
    data->properties["popup_side"] = std::move(side);
    data->children = std::move(children);
    return Element(std::move(data));
}
Element Tooltip(std::string anchor, Value<std::string> text, PopupProps props) {
    const auto source = props.node.source;
    auto popup = Popup(std::move(anchor), {Text(std::move(text), {.source = source})}, std::move(props));
    auto data = std::make_shared<detail::ElementData>(*popup.data_);
    data->properties["pointer_events"] = false;
    return Element(std::move(data));
}
Element Button(Value<std::string> label, std::string action, ButtonProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props.node);
    data->disabled = std::move(props.disabled);
    data->action = std::move(action);
    data->children.push_back(Text(std::move(label), {.source = data->props.source}));
    return Element(std::move(data));
}
Element Checkbox(Value<std::string> label, Value<bool> checked, std::string action, CheckboxProps props) {
    auto marker = std::make_shared<detail::ElementData>();
    marker->kind = NodeKind::text;
    marker->props.source = props.node.source;
    marker->checked_marker = std::move(checked);
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::row;
    if (!data->props.style.gap) data->props.style.gap = 8.f;
    data->disabled = std::move(props.disabled);
    data->action = std::move(action);
    data->children.push_back(Element(std::move(marker)));
    data->children.push_back(Text(std::move(label), {.source = data->props.source}));
    return Element(std::move(data));
}
Element TextInput(Value<std::string> label, Value<TextEditState> value, std::string action, TextInputProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::column;
    data->disabled = std::move(props.disabled);
    data->action = std::move(action);
    data->children.push_back(Text(std::move(label), {.source = data->props.source}));
    for (bool selection : {false, true}) {
        auto child = std::make_shared<detail::ElementData>();
        child->kind = NodeKind::text;
        child->props.source = data->props.source;
        child->editing = value;
        child->selection = selection;
        data->children.push_back(Element(std::move(child)));
    }
    return Element(std::move(data));
}
Element Row(std::vector<Element> children, Props props) {
    props.style.direction = FlexDirection::row;
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props);
    data->children = std::move(children);
    return Element(std::move(data));
}
Element Column(std::vector<Element> children, Props props) {
    props.style.direction = FlexDirection::column;
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props);
    data->children = std::move(children);
    return Element(std::move(data));
}

Element Stack(std::vector<Element> children, Props props) {
    if (props.style.display != Display::none) props.style.display = Display::stack;
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props);
    data->children = std::move(children);
    return Element(std::move(data));
}

Element ScrollView(std::vector<Element> children, Props props) {
    props.style.overflow = Overflow::scroll;
    return Column(std::move(children), std::move(props));
}

Element Grid(std::vector<Element> children, Props props) {
    if (props.style.display != Display::none) props.style.display = Display::grid;
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props);
    data->children = std::move(children);
    return Element(std::move(data));
}

Element Dropdown(std::string label, std::string selected, std::vector<DropdownOption> options,
    std::string toggle_action, DropdownProps props) {
    std::vector<Diagnostic> errors;
    std::set<std::string> keys;
    const DropdownOption* chosen = nullptr;
    for (const auto& option : options) {
        if (option.key.empty() || !keys.insert(option.key).second)
            errors.push_back({"dropdown_options", Severity::error, {}, "Supply unique nonempty Dropdown option keys."});
        if (option.key == selected) chosen = &option;
    }
    if (!chosen)
        errors.push_back({"dropdown_selection", Severity::error, {}, "Select an existing Dropdown option key."});
    auto control = [&](const std::string& suffix) {
        Props node;
        if (props.node.id) node.id = *props.node.id + suffix;
        node.source = props.node.source;
        return node;
    };
    std::vector<Element> children;
    children.push_back(Button(label + ": " + (chosen ? chosen->label : selected) +
        (props.expanded ? " [-]" : " [+]"), std::move(toggle_action),
        {.node = control("-trigger"), .disabled = props.disabled}));
    if (props.expanded) {
        for (auto& option : options)
            children.push_back(Button((option.key == selected ? "* " : "") + option.label,
                std::move(option.action), {.node = control("-option-" + option.key),
                    .disabled = props.disabled || option.disabled}));
    }
    auto data = std::make_shared<detail::ElementData>();
    props.node.style.direction = FlexDirection::column;
    if (!props.node.style.gap) props.node.style.gap = 8.f;
    data->props = std::move(props.node);
    data->children = std::move(children);
    data->diagnostics = std::move(errors);
    return Element(std::move(data));
}

namespace {
std::vector<Diagnostic> slider_range_errors(SliderRange range) {
    const auto span = std::int64_t{range.maximum} - range.minimum;
    if (span <= 0 || range.step <= 0 || span % range.step != 0)
        return {{"slider_range", Severity::error, {},
            "Set minimum < maximum and a positive step that divides maximum - minimum exactly."}};
    return {};
}
}

Result<int> slider_value_at(SliderRange range, double position) {
    auto errors = slider_range_errors(range);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (!std::isfinite(position))
        return {std::nullopt, {{"slider_position", Severity::error, {}, "Supply a finite normalized Slider position."}}};
    const auto stops = (std::int64_t{range.maximum} - range.minimum) / range.step;
    const auto stop = static_cast<std::int64_t>(std::floor(std::clamp(position, 0., 1.) * stops + 0.5));
    return {static_cast<int>(std::int64_t{range.minimum} + stop * range.step), {}};
}

Element Slider(std::string label, int value, SliderRange range, std::string action, SliderProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->diagnostics = slider_range_errors(range);
    if (data->diagnostics.empty() && (value < range.minimum || value > range.maximum ||
        (std::int64_t{value} - range.minimum) % range.step != 0))
        data->diagnostics.push_back({"slider_value", Severity::error, {},
            "Supply a Slider value within [minimum, maximum] at a minimum + n * step stop."});
    const auto fraction = data->diagnostics.empty() ?
        (static_cast<double>(value) - range.minimum) / (static_cast<double>(range.maximum) - range.minimum) : 0.;
    auto part = [&](const char* suffix, StyleDeclarations style) {
        Props node;
        if (props.node.id) node.id = *props.node.id + suffix;
        node.source = props.node.source;
        node.style = std::move(style);
        return node;
    };
    data->children.push_back(Text(std::move(label) + ": " + std::to_string(value), {.source = props.node.source}));
    data->children.push_back(Row({
        Column({}, part("-fill", {.width = Dimension::points(static_cast<float>(192 * fraction)),
            .height = Dimension::points(12), .background = Color{0.25f, 0.5f, 0.85f, 1}})),
        Column({}, part("-thumb", {.width = Dimension::points(8), .height = Dimension::points(12),
            .background = Color{0.1f, 0.2f, 0.4f, 1}}))},
        part("-track", {.width = Dimension::points(200), .height = Dimension::points(12),
            .gap = 0.f, .background = Color{0.75f, 0.78f, 0.82f, 1}})));
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::column;
    if (!data->props.style.gap) data->props.style.gap = 4.f;
    data->disabled = props.disabled;
    data->action = std::move(action);
    return Element(std::move(data));
}

namespace {
std::vector<Diagnostic> split_limits_errors(SplitPaneLimits limits) {
    if (!std::isfinite(limits.minimum_first) || limits.minimum_first < 0 ||
        !std::isfinite(limits.minimum_second) || limits.minimum_second < 0 ||
        !std::isfinite(limits.handle_size) || limits.handle_size <= 0)
        return {{"split_pane_limits", Severity::error, {},
            "Supply finite nonnegative pane minima and a finite positive logical handle size."}};
    return {};
}
}

Result<float> split_pane_size_at(SplitPaneLimits limits, float available, float requested) {
    auto errors = split_limits_errors(limits);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (!std::isfinite(available) || available < 0 || !std::isfinite(requested))
        return {std::nullopt, {{"split_pane_size", Severity::error, {},
            "Supply a finite nonnegative parent content size and a finite requested pane size."}}};
    const auto maximum = std::max(limits.minimum_first, available - limits.handle_size - limits.minimum_second);
    return {std::clamp(requested, limits.minimum_first, maximum), {}};
}

Element detail::Lowerer::split_pane(Element first, Element second, float size, std::string action,
    SplitPaneProps props, FlexDirection direction) {
    auto data = std::make_shared<detail::ElementData>();
    data->diagnostics = split_limits_errors(props.limits);
    if (!std::isfinite(size) || size < 0)
        data->diagnostics.push_back({"split_pane_size", Severity::error, {},
            "Supply a finite nonnegative first-pane logical size."});
    if (!props.node.id || props.node.id->empty())
        data->diagnostics.push_back({"split_pane_id", Severity::error, {},
            "Give SplitPane a stable author ID for its handle and retained host state."});
    const std::string id = props.node.id.value_or("");
    const bool horizontal = direction == FlexDirection::row;
    auto pane = [&](Element child, const char* suffix, bool leading) {
        auto content = child.data_ ? std::make_shared<detail::ElementData>(*child.data_) :
            std::shared_ptr<detail::ElementData>{};
        if (content) {
            content->props.style.grow = 1.f;
            content->props.style.shrink = 1.f;
            child = Element(std::move(content));
        }
        Props node{.id = id + suffix, .source = props.node.source};
        auto& style = node.style;
        style.direction = direction;
        style.overflow = Overflow::clip;
        style.grow = leading ? 0.f : 1.f;
        style.shrink = leading ? 1.f : 0.f;
        if (horizontal) {
            style.width = Dimension::points(leading ? size : 0);
            style.min_width = leading ? props.limits.minimum_first : props.limits.minimum_second;
        } else {
            style.height = Dimension::points(leading ? size : 0);
            style.min_height = leading ? props.limits.minimum_first : props.limits.minimum_second;
        }
        return horizontal ? Row({std::move(child)}, std::move(node)) : Column({std::move(child)}, std::move(node));
    };
    auto handle = std::make_shared<detail::ElementData>();
    handle->props.id = id + "-handle";
    handle->props.source = props.node.source;
    handle->props.style.background = Color{0.45f, 0.5f, 0.6f, 1};
    if (horizontal) handle->props.style.width = Dimension::points(props.limits.handle_size);
    else handle->props.style.height = Dimension::points(props.limits.handle_size);
    handle->properties[std::string(property_names::separator)] = true;
    handle->properties["labelled_by"] = NodeReference{id + "-handle-label"};
    handle->disabled = props.disabled;
    handle->action = std::move(action);
    handle->children.push_back(Text(props.label + ": " + std::to_string(size) + " logical units",
        {.id = id + "-handle-label", .style = {.display = Display::none}, .source = props.node.source}));
    data->children = {pane(std::move(first), "-first", true), Element(std::move(handle)),
        pane(std::move(second), "-second", false)};
    data->props = std::move(props.node);
    data->props.style.direction = direction;
    data->props.style.justify = Justify::start;
    data->props.style.align = Align::stretch;
    data->props.style.gap = 0.f;
    data->props.style.overflow = Overflow::clip;
    return Element(std::move(data));
}
Element HorizontalSplitPane(Element first, Element second, float size, std::string action, SplitPaneProps props) {
    return detail::Lowerer::split_pane(std::move(first), std::move(second), size, std::move(action), std::move(props), FlexDirection::row);
}
Element VerticalSplitPane(Element first, Element second, float size, std::string action, SplitPaneProps props) {
    return detail::Lowerer::split_pane(std::move(first), std::move(second), size, std::move(action), std::move(props), FlexDirection::column);
}

Result<Presentation> Session::lower(const Element& element, const ValidationContext& context) {
    detail::Lowerer lowering;
    if (!lowering.describe(element, lowering.result.document.root, "/root", 1))
        return {std::nullopt, std::move(lowering.errors)};
    auto& result = lowering.result;
    auto sources = validate_sources(result.document, result.sources);
    if (!sources.empty()) return {std::nullopt, std::move(sources)};
    auto finish_errors = [&]() -> Result<Presentation> {
        const auto location_errors = locate_diagnostics(result.document, result.sources, lowering.errors);
        if (!location_errors.empty()) return {std::nullopt, location_errors};
        return {std::nullopt, std::move(lowering.errors)};
    };
    auto document_errors = validate(result.document, context);
    lowering.errors.insert(lowering.errors.end(), document_errors.begin(), document_errors.end());
    if (!lowering.errors.empty()) return finish_errors();
    try {
        lowering.errors = runtime_.flush();
        if (!lowering.errors.empty()) return finish_errors();
        runtime_.check_settled();
        std::size_t index = 0;
        if (!lowering.values(runtime_, result.document.root, index)) return finish_errors();
    } catch (const ReactiveError& error) {
        return {std::nullopt, {error.diagnostic()}};
    }
    lowering.errors = validate(result.document, context);
    if (!lowering.errors.empty()) return finish_errors();
    return {std::move(result), {}};
}
} // namespace tessera::authoring
