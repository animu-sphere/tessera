#include <tessera/ui/authoring.hpp>

namespace tessera::authoring {
namespace detail {
struct ElementData {
    NodeKind kind = NodeKind::box;
    Props props;
    std::optional<Value<std::string>> text;
    std::optional<Value<bool>> checked_marker;
    std::optional<Value<bool>> disabled;
    std::optional<std::string> action;
    std::vector<Element> children;
};

struct Lowerer {
    Presentation result;
    std::vector<const ElementData*> elements;
    std::vector<Diagnostic> errors;

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
        elements.push_back(&data);
        node.kind = data.kind;
        node.id = data.props.id;
        node.classes = data.props.classes;
        if (data.text) {
            const auto* literal = std::get_if<std::string>(&data.text->value_);
            node.properties["text"] = literal ? *literal : std::string{};
        }
        if (data.checked_marker) {
            const auto* literal = std::get_if<bool>(&data.checked_marker->value_);
            node.properties["text"] = std::string(literal && *literal ? "[x]" : "[ ]");
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
