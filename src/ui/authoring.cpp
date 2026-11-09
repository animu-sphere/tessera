#include <tessera/ui/authoring.hpp>
#include <tessera/ui/property_metadata.hpp>
#include <array>
#include <charconv>
#include <set>
#include <algorithm>
#include <cmath>
#include <functional>

namespace tessera::authoring {
namespace detail {
struct ElementData {
    NodeKind kind = NodeKind::box;
    Props props;
    std::optional<Value<std::string>> text;
    // A toggle owner's controlled checked value, projected as its `selected` property. Its
    // generated parts choose text or styles from the same snapshot: [unchecked, checked].
    std::optional<Value<bool>> checked;
    std::optional<std::array<std::string, 2>> checked_text;
    std::optional<std::array<StyleDeclarations, 2>> checked_styles;
    std::optional<Value<bool>> disabled;
    // Controlled editing observation, presented as the value Text, the selection Text, or
    // the input owner's semantic edit_value.
    std::optional<Value<TextEditState>> editing;
    enum class EditPart : std::uint8_t { value, selection, owner } edit_part = EditPart::value;
    std::optional<std::string> action;
    std::map<std::string, std::string, std::less<>> events;
    std::map<std::string, Property, std::less<>> properties;
    std::vector<Element> children;
    std::vector<Diagnostic> diagnostics;
};

struct Lowerer {
    static Element split_pane(Element, Element, float, std::string, SplitPaneProps, FlexDirection);
    static Element scrollbar(bool horizontal, std::string, float, ScrollGeometry, std::string, ScrollbarProps);
    static Element collection(std::string, std::optional<std::string>, std::vector<TreeItem>, CollectionProps,
                              std::string_view);
    static Element flat_collection(std::string, std::optional<std::string>, std::vector<ListItem>, CollectionProps,
                                   std::string_view);
    Presentation result;
    std::vector<const ElementData*> elements;
    std::vector<Diagnostic> errors;

    bool editing_value(const ElementData& data, const TextEditState& state, UiNode& node, const std::string& path) {
        // Reuse the logical editor's complete observation validation. Lowering
        // never mutates editing state; an open composition presents its preedit.
        using Part = ElementData::EditPart;
        auto display = display_text_edit(state);
        if (!display) {
            // The generated value/selection Text report the failure once each; the owner is silent.
            if (data.edit_part != Part::owner)
                for (auto& error : display.diagnostics) {
                    error.path = path + "/properties/text";
                    errors.push_back(std::move(error));
                }
            return data.edit_part == Part::owner;
        }
        const auto& shown = *display.value;
        if (data.edit_part == Part::owner)
            node.properties["edit_value"] = shown.text;
        else if (data.edit_part == Part::value)
            node.properties["text"] = shown.text;
        else if (shown.composing)
            node.properties["text"] = "Composition " + std::to_string(shown.preedit_begin) + ":" +
                                      std::to_string(shown.preedit_end) + " caret " + std::to_string(shown.caret);
        else
            node.properties["text"] = "Selection " + std::to_string(state.anchor) + ":" + std::to_string(state.caret);
        return true;
    }

    bool describe(const Element& element, UiNode& node, const std::string& path, std::size_t depth,
                  std::optional<bool> checked = {}) {
        if (!element.data_) {
            errors.push_back({"authoring_empty_element", Severity::error, path,
                              "Supply a constructed element, not a moved-from description."});
            return false;
        }
        if (depth > max_document_depth || elements.size() >= max_document_nodes) {
            errors.push_back(
                {"authoring_limit", Severity::error, path, "Keep the form within document depth and node bounds."});
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
        node.events = data.events;
        if (data.text) {
            const auto* literal = std::get_if<std::string>(&data.text->value_);
            node.properties["text"] = literal ? *literal : std::string{};
        }
        if (data.checked) {
            const auto* literal = std::get_if<bool>(&data.checked->value_);
            checked = literal && *literal;
            node.properties["selected"] = *checked;
        }
        if (data.checked_text) node.properties["text"] = (*data.checked_text)[checked.value_or(false)];
        if (data.editing) {
            node.properties[data.edit_part == ElementData::EditPart::owner ? "edit_value" : "text"] = std::string{};
            if (const auto* literal = std::get_if<TextEditState>(&data.editing->value_))
                (void)editing_value(data, *literal, node, path);
        }
        if (data.action) {
            node.events["activate"] = *data.action;
            node.properties.try_emplace("focusable", true);
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
        if (data.props.source)
            for (const auto& [event, action] : node.events) {
                (void)action;
                source.event_arguments[event] = *data.props.source;
            }
        result.sources.nodes.push_back(std::move(source));
        const auto& style = data.checked_styles ? (*data.checked_styles)[checked.value_or(false)] : data.props.style;
        result.styles.push_back(style);
        auto style_errors = validate(style, path + "/style");
        for (auto& error : style_errors) {
            if (data.props.source) error.source = DiagnosticSource{*data.props.source, DiagnosticOrigin::argument};
            errors.push_back(std::move(error));
        }
        // Do not reserve from an unbounded child count before the traversal guard.
        for (std::size_t i = 0; i < data.children.size(); ++i) {
            node.children.emplace_back();
            if (!describe(data.children[i], node.children.back(), path + "/children/" + std::to_string(i), depth + 1,
                          checked))
                return false;
        }
        return true;
    }

    bool values(const ReactiveRuntime& runtime, UiNode& node, std::size_t& index, std::optional<bool> checked = {}) {
        const auto current = index++;
        const auto& data = *elements[current];
        std::string property;
        try {
            if (data.text) {
                property = "text";
                node.properties["text"] = data.text->snapshot(runtime);
            }
            if (data.checked) {
                property = "selected";
                checked = data.checked->snapshot(runtime);
                node.properties["selected"] = *checked;
            }
            if (data.checked_text && checked) node.properties["text"] = (*data.checked_text)[*checked];
            if (data.checked_styles && checked) result.styles[current] = (*data.checked_styles)[*checked];
            if (data.disabled) {
                property = "disabled";
                node.properties["disabled"] = data.disabled->snapshot(runtime);
            }
            if (data.editing && data.edit_part != ElementData::EditPart::owner) {
                property = "text";
                if (!editing_value(data, data.editing->snapshot(runtime), node, result.sources.nodes[current].pointer))
                    return false;
            }
        } catch (const ReactiveError& failure) {
            auto error = failure.diagnostic();
            error.message += " Binding graph location: " + error.path;
            error.path = result.sources.nodes[current].pointer + "/properties/" + property;
            errors.push_back(std::move(error));
            return false;
        }
        for (auto& child : node.children)
            if (!values(runtime, child, index, checked)) return false;
        // The owner's value is read after its generated Text, which locates any binding failure.
        if (data.editing && data.edit_part == ElementData::EditPart::owner)
            (void)editing_value(data, data.editing->snapshot(runtime), node, result.sources.nodes[current].pointer);
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
        case PopupSide::bottom:
            side = "bottom";
            break;
        case PopupSide::top:
            side = "top";
            break;
        case PopupSide::left:
            side = "left";
            break;
        case PopupSide::right:
            side = "right";
            break;
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
Element Menu(std::string label, std::string anchor, std::vector<MenuItem> items, PopupProps props) {
    auto content =
        detail::Lowerer::flat_collection(std::move(label), {}, std::move(items), {.node = props.node}, "menu");
    if (!props.node.style.overflow) props.node.style.overflow = Overflow::scroll;
    auto popup = Popup(std::move(anchor), content.data_->children, std::move(props));
    auto data = std::make_shared<detail::ElementData>(*popup.data_);
    data->properties.insert(content.data_->properties.begin(), content.data_->properties.end());
    data->diagnostics = content.data_->diagnostics;
    return Element(std::move(data));
}
Element ContextMenu(std::string label, std::string target, Point position, std::vector<MenuItem> items,
                    PopupProps props) {
    auto menu = Menu(std::move(label), std::move(target), std::move(items), std::move(props));
    auto data = std::make_shared<detail::ElementData>(*menu.data_);
    data->properties["popup_x"] = static_cast<double>(position.x);
    data->properties["popup_y"] = static_cast<double>(position.y);
    return Element(std::move(data));
}
Element Modal(std::vector<Element> children, ModalProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->props = std::move(props.node);
    data->props.style.display = props.open ? Display::stack : Display::none;
    data->props.style.overflow = Overflow::clip;
    if (!data->props.style.background) data->props.style.background = Color{0, 0, 0, 0.4f};
    data->properties["modal"] = true;
    data->children = std::move(children);
    return Element(std::move(data));
}
Element Dialog(Value<std::string> title, std::vector<Element> children, DialogProps props) {
    const auto prefix = props.node.id.value_or("");
    if (!props.panel.id) props.panel.id = prefix + "-panel";
    if (!props.panel.source) props.panel.source = props.node.source;
    auto& style = props.panel.style;
    if (!style.max_width) style.max_width = 360.f;
    if (!style.stack_horizontal) style.stack_horizontal = Align::center;
    if (!style.stack_vertical) style.stack_vertical = Align::center;
    if (!style.overflow) style.overflow = Overflow::scroll;
    if (!style.background) style.background = Color{1, 1, 1, 1};
    children.insert(children.begin(), Text(std::move(title), {.id = prefix + "-title", .source = props.node.source}));
    auto layer = Modal({Column(std::move(children), std::move(props.panel))},
                       {.node = std::move(props.node), .open = props.open});
    auto data = std::make_shared<detail::ElementData>(*layer.data_);
    data->properties["labelled_by"] = NodeReference{prefix + "-title"};
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
namespace {
// Shared Checkbox/Switch owner: a focusable row named by its generated <id>-label Text.
std::shared_ptr<detail::ElementData> toggle_owner(const char* role, const char* name, Value<bool> checked,
                                                  std::string action, CheckboxProps& props) {
    auto data = std::make_shared<detail::ElementData>();
    const auto prefix = props.node.id.value_or("");
    if (prefix.empty())
        data->diagnostics.push_back({std::string(role) + "_id",
                                     Severity::error,
                                     {},
                                     std::string("Give the ") + name + " a stable nonempty author ID."});
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::row;
    if (!data->props.style.gap) data->props.style.gap = 8.f;
    data->disabled = std::move(props.disabled);
    data->action = std::move(action);
    data->checked = std::move(checked);
    data->properties["toggle_role"] = std::string(role);
    data->properties["labelled_by"] = NodeReference{prefix + "-label"};
    return data;
}
}

Element Checkbox(Value<std::string> label, Value<bool> checked, std::string action, CheckboxProps props) {
    auto data = toggle_owner("checkbox", "Checkbox", std::move(checked), std::move(action), props);
    auto marker = std::make_shared<detail::ElementData>();
    marker->kind = NodeKind::text;
    marker->props.source = data->props.source;
    marker->checked_text = std::array<std::string, 2>{"[ ]", "[x]"};
    data->children.push_back(Element(std::move(marker)));
    data->children.push_back(
        Text(std::move(label), {.id = data->props.id.value_or("") + "-label", .source = data->props.source}));
    return Element(std::move(data));
}

Element Switch(Value<std::string> label, Value<bool> on, std::string action, SwitchProps props) {
    auto data = toggle_owner("switch", "Switch", std::move(on), std::move(action), props);
    if (!data->props.style.align) data->props.style.align = Align::center;
    const auto prefix = data->props.id.value_or("");
    StyleDeclarations track{.direction = FlexDirection::row,
                            .justify = Justify::start,
                            .align = Align::center,
                            .width = Dimension::points(32),
                            .height = Dimension::points(18),
                            .padding = Edges{3, 3, 3, 3},
                            .shrink = 0.f,
                            .background = Color{0.62f, 0.66f, 0.72f, 1},
                            .corner_radius = 9.f};
    auto checked_track = track;
    checked_track.justify = Justify::end;
    checked_track.background = Color{0.25f, 0.5f, 0.85f, 1};
    auto thumb = Column({}, {.id = prefix + "-thumb",
                             .style = {.width = Dimension::points(12),
                                       .height = Dimension::points(12),
                                       .shrink = 0.f,
                                       .background = Color{1, 1, 1, 1},
                                       .background_shape = FillShape::ellipse,
                                       .background_antialias = true},
                             .source = data->props.source});
    auto part = std::make_shared<detail::ElementData>();
    part->props = {.id = prefix + "-track", .source = data->props.source};
    part->checked_styles = std::array<StyleDeclarations, 2>{std::move(track), std::move(checked_track)};
    part->children.push_back(std::move(thumb));
    data->children.push_back(Element(std::move(part)));
    data->children.push_back(Text(std::move(label), {.id = prefix + "-label", .source = data->props.source}));
    return Element(std::move(data));
}
namespace {
// Shared TextInput/NumberInput owner: a focusable Box named by its generated <id>-label
// Text whose semantic edit_value is the presented text.
std::shared_ptr<detail::ElementData> edit_owner(const char* role, const char* name, const Value<TextEditState>& value,
                                                std::string action, Props node, Value<bool> disabled) {
    auto data = std::make_shared<detail::ElementData>();
    if (node.id.value_or("").empty())
        data->diagnostics.push_back({std::string(role) + "_id",
                                     Severity::error,
                                     {},
                                     std::string("Give the ") + name + " a stable nonempty author ID."});
    data->props = std::move(node);
    data->disabled = std::move(disabled);
    data->action = std::move(action);
    data->editing = value;
    data->edit_part = detail::ElementData::EditPart::owner;
    data->properties["edit_role"] = std::string(role);
    data->properties["labelled_by"] = NodeReference{data->props.id.value_or("") + "-label"};
    return data;
}
std::shared_ptr<detail::ElementData> edit_part_text(const Value<TextEditState>& value,
                                                    detail::ElementData::EditPart part, std::optional<std::string> id,
                                                    const std::optional<SourceLocation>& source) {
    auto child = std::make_shared<detail::ElementData>();
    child->kind = NodeKind::text;
    child->props.id = std::move(id);
    child->props.source = source;
    child->editing = value;
    child->edit_part = part;
    return child;
}
} // namespace

Element TextInput(Value<std::string> label, Value<TextEditState> value, std::string action, TextInputProps props) {
    using Part = detail::ElementData::EditPart;
    auto data = edit_owner("text_input", "TextInput", value, std::move(action), std::move(props.node),
                           std::move(props.disabled));
    data->props.style.direction = FlexDirection::column;
    const auto prefix = data->props.id.value_or("");
    const auto source = data->props.source;
    data->children.push_back(Text(std::move(label), {.id = prefix + "-label", .source = source}));
    data->children.push_back(Element(edit_part_text(value, Part::value, prefix + "-value", source)));
    data->children.push_back(Element(edit_part_text(value, Part::selection, std::nullopt, source)));
    return Element(std::move(data));
}

Element NumberInput(Value<std::string> label, Value<TextEditState> draft, std::string action, NumberInputProps props) {
    using Part = detail::ElementData::EditPart;
    auto data = edit_owner("number_input", "NumberInput", draft, std::move(action), std::move(props.node),
                           std::move(props.disabled));
    auto& style = data->props.style;
    style.direction = FlexDirection::row;
    if (!style.align) style.align = Align::center;
    if (!style.gap) style.gap = 6.f;
    const auto prefix = data->props.id.value_or("");
    const auto source = data->props.source;
    data->children.push_back(Text(std::move(label), {.id = prefix + "-label", .source = source}));
    auto field = std::make_shared<detail::ElementData>();
    field->props = {.id = prefix + "-field",
                    .style = {.direction = FlexDirection::row, .align = Align::center, .grow = 1.f, .shrink = 1.f},
                    .source = source};
    field->children.push_back(Element(edit_part_text(draft, Part::value, prefix + "-value", source)));
    data->children.push_back(Element(std::move(field)));
    for (const auto& [part, glyph] : {std::pair{"-decrease", "-"}, std::pair{"-increase", "+"}})
        data->children.push_back(Column({Text(glyph, {.source = source})}, {.id = prefix + part,
                                                                            .style = {.justify = Justify::center,
                                                                                      .align = Align::center,
                                                                                      .width = Dimension::points(20),
                                                                                      .shrink = 0.f},
                                                                            .source = source}));
    return Element(std::move(data));
}

namespace {
Result<double> number_failure(std::string code, std::string message) {
    return {std::nullopt, {{std::move(code), Severity::error, "/text", std::move(message), {}}}};
}
std::vector<Diagnostic> check_range(NumberRange range) {
    if (!std::isfinite(range.minimum) || !std::isfinite(range.maximum) || range.minimum > range.maximum ||
        !std::isfinite(range.step) || range.step <= 0)
        return {{"number_range",
                 Severity::error,
                 "/range",
                 "Use finite bounds with minimum <= maximum and a finite positive step.",
                 {}}};
    return {};
}
std::size_t fraction_digits(double value) {
    const auto text = format_number(value);
    const auto point = text.find('.');
    return point == std::string::npos ? 0 : text.size() - point - 1;
}
} // namespace

std::string format_number(double value) {
    if (value == 0) return "0";
    if (!std::isfinite(value)) return {};
    std::array<char, 400> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed);
    return written.ec == std::errc{} ? std::string(buffer.data(), written.ptr) : std::string{};
}

Result<double> parse_number(NumberRange range, std::string_view text) {
    if (auto errors = check_range(range); !errors.empty()) return {std::nullopt, std::move(errors)};
    const auto bounds = " from " + format_number(range.minimum) + " to " + format_number(range.maximum) + ".";
    std::size_t at = text.starts_with('-') ? 1 : 0;
    const auto digits = [&] {
        const auto begin = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        return at - begin;
    };
    const auto first = at;
    const auto whole = digits();
    bool valid = whole > 0 && !(whole > 1 && text[first] == '0');
    if (valid && at < text.size() && text[at] == '.') {
        ++at;
        valid = digits() > 0;
    }
    if (!valid || at != text.size())
        return number_failure("number_syntax", "Enter a plain decimal such as -1.25" + bounds);
    double parsed = 0;
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (converted.ec != std::errc{} || !std::isfinite(parsed) || parsed < range.minimum || parsed > range.maximum)
        return number_failure("number_out_of_range", "Enter a number" + bounds);
    return {parsed == 0 ? 0.0 : parsed, {}};
}

Result<double> step_number(NumberRange range, double value, int steps) {
    if (auto errors = check_range(range); !errors.empty()) return {std::nullopt, std::move(errors)};
    if (!std::isfinite(value)) return number_failure("number_value", "Step from a finite value.");
    double next = std::clamp(value + static_cast<double>(steps) * range.step, range.minimum, range.maximum);
    // Drop binary noise such as 0.1 + 0.2 by keeping the decimal places of the value and step.
    const auto digits = std::max(fraction_digits(value), fraction_digits(range.step));
    if (next != range.minimum && next != range.maximum && digits <= 17) {
        std::array<char, 400> buffer{};
        const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), next, std::chars_format::fixed,
                                           static_cast<int>(digits));
        if (written.ec == std::errc{}) (void)std::from_chars(buffer.data(), written.ptr, next);
    }
    return {next == 0 ? 0.0 : next, {}};
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

Element detail::Lowerer::collection(std::string label, std::optional<std::string> selected, std::vector<TreeItem> items,
                                    CollectionProps props, std::string_view role) {
    const bool tree = role == "tree";
    const bool menu = role == "menu";
    auto data = std::make_shared<ElementData>();
    const auto prefix = props.node.id.value_or("");
    const auto source = props.node.source;
    auto error = [&](const char* code, const char* message) {
        data->diagnostics.push_back({code, Severity::error, {}, message});
    };
    if (prefix.empty()) error("collection_id", "Give the collection a stable nonempty author ID.");
    if (label.empty()) error("collection_label", "Give the collection a nonempty label.");
    auto part = [&](std::string id) { return Props{.id = std::move(id), .source = source}; };
    std::set<std::string> keys;
    bool found_selection = !selected;
    std::size_t count = 0;
    std::function<void(const TreeItem&, std::size_t, bool, bool, std::optional<std::string>)> item;
    item = [&](const TreeItem& value, std::size_t depth, bool shown, bool disabled, std::optional<std::string> parent) {
        auto entry = std::make_shared<ElementData>();
        entry->props = part(prefix + "-item-" + value.key);
        entry->props.style.direction = FlexDirection::row;
        entry->props.style.padding = Edges{2, 4, 2, static_cast<float>(depth - 1) * 16 + 4};
        entry->props.style.gap = 4.f;
        if (!shown) entry->props.style.display = Display::none;
        entry->action = value.action;
        disabled = disabled || value.disabled;
        entry->disabled = disabled;
        entry->properties["collection_role"] = std::string(menu ? "menu_item" : tree ? "tree_item" : "list_item");
        if (!menu) entry->properties["selected"] = selected == value.key;
        entry->properties["labelled_by"] = NodeReference{prefix + "-label-" + value.key};
        if (parent) entry->properties["tree_parent"] = NodeReference{*parent};
        if (value.key.empty() || !keys.insert(value.key).second)
            error("collection_keys", "Supply unique nonempty item keys across the collection.");
        if (menu && value.label.empty()) error("menu_label", "Give each menu command a nonempty label.");
        if (selected == value.key) found_selection = true;
        if (++count > max_document_nodes || depth > max_document_depth) {
            error("collection_limit", "Keep collection items within document depth and node limits.");
            return;
        }
        if (!value.children.empty()) {
            if (value.toggle_action.empty()) error("tree_toggle", "Give each tree branch a host toggle action.");
            entry->properties["expanded"] = value.expanded;
            entry->events["expand"] = value.toggle_action;
            entry->events["collapse"] = value.toggle_action;
            auto toggle = Button(value.expanded ? "-" : "+", value.toggle_action,
                                 {.node = part(prefix + "-toggle-" + value.key)});
            auto toggle_data = std::make_shared<ElementData>(*toggle.data_);
            toggle_data->properties["focusable"] = false;
            entry->children.push_back(Element(std::move(toggle_data)));
        } else if (value.expanded || !value.toggle_action.empty()) {
            error("tree_leaf", "Leaves cannot be expanded or declare a toggle action.");
        }
        if (!menu) entry->children.push_back(Text(selected == value.key ? "*" : " ", {.source = source}));
        entry->children.push_back(Text(value.label, part(prefix + "-label-" + value.key)));
        data->children.push_back(Element(std::move(entry)));
        for (const auto& child : value.children) {
            if (count >= max_document_nodes) {
                error("collection_limit", "Keep collection items within document depth and node limits.");
                break;
            }
            item(child, depth + 1, shown && value.expanded, disabled, prefix + "-item-" + value.key);
        }
    };
    data->children.push_back(Text(std::move(label), part(prefix + "-title")));
    for (const auto& value : items) {
        if (count >= max_document_nodes) {
            error("collection_limit", "Keep collection items within document depth and node limits.");
            break;
        }
        item(value, 1, true, false, std::nullopt);
    }
    if (!found_selection) error("collection_selection", "Select an existing item key or use nullopt.");
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::column;
    data->props.style.overflow = Overflow::scroll;
    data->disabled = props.disabled;
    data->properties["collection_role"] = std::string(role);
    data->properties["labelled_by"] = NodeReference{prefix + "-title"};
    return Element(std::move(data));
}

Element detail::Lowerer::flat_collection(std::string label, std::optional<std::string> selected,
                                         std::vector<ListItem> items, CollectionProps props, std::string_view role) {
    std::vector<TreeItem> entries;
    entries.reserve(std::min(items.size(), max_document_nodes + 1));
    for (auto& item : items) {
        if (entries.size() > max_document_nodes) break;
        entries.push_back(
            {std::move(item.key), std::move(item.label), std::move(item.action), {}, false, item.disabled, {}});
    }
    return collection(std::move(label), std::move(selected), std::move(entries), std::move(props), role);
}

Element ListView(std::string label, std::optional<std::string> selected, std::vector<ListItem> items,
                 CollectionProps props) {
    return detail::Lowerer::flat_collection(std::move(label), std::move(selected), std::move(items), std::move(props),
                                            "list");
}

Element TreeView(std::string label, std::optional<std::string> selected, std::vector<TreeItem> items,
                 CollectionProps props) {
    return detail::Lowerer::collection(std::move(label), std::move(selected), std::move(items), std::move(props),
                                       "tree");
}

Element RadioGroup(std::string label, std::optional<std::string> selected, std::vector<RadioOption> options,
                   CollectionProps props) {
    auto data = std::make_shared<detail::ElementData>();
    const auto prefix = props.node.id.value_or("");
    const auto source = props.node.source;
    auto error = [&](const char* code, const char* message) {
        data->diagnostics.push_back({code, Severity::error, {}, message});
    };
    if (prefix.empty()) error("radio_group_id", "Give the RadioGroup a stable nonempty author ID.");
    if (label.empty()) error("radio_group_label", "Give the RadioGroup a nonempty label.");
    if (options.size() > max_document_nodes)
        error("radio_group_limit", "Keep RadioGroup options within document node limits.");
    auto part = [&](std::string id) { return Props{.id = std::move(id), .source = source}; };
    std::set<std::string> keys;
    bool found_selection = !selected;
    data->children.push_back(Text(std::move(label), part(prefix + "-title")));
    for (std::size_t i = 0; i < options.size() && i < max_document_nodes; ++i) {
        auto& option = options[i];
        if (option.key.empty() || !keys.insert(option.key).second)
            error("radio_group_keys", "Supply unique nonempty RadioGroup option keys.");
        if (option.label.empty()) error("radio_group_option_label", "Give each RadioGroup option a nonempty label.");
        const bool checked = selected == option.key;
        found_selection = found_selection || checked;
        auto entry = std::make_shared<detail::ElementData>();
        entry->props = part(prefix + "-option-" + option.key);
        entry->props.style.direction = FlexDirection::row;
        entry->props.style.gap = 6.f;
        entry->action = std::move(option.action);
        entry->disabled = option.disabled;
        entry->properties["collection_role"] = std::string("radio");
        entry->properties["selected"] = checked;
        entry->properties["labelled_by"] = NodeReference{prefix + "-label-" + option.key};
        entry->children.push_back(Text(checked ? "(o)" : "( )", {.source = source}));
        entry->children.push_back(Text(std::move(option.label), part(prefix + "-label-" + option.key)));
        data->children.push_back(Element(std::move(entry)));
    }
    if (!found_selection) error("radio_group_selection", "Select an existing RadioGroup option key or use nullopt.");
    data->props = std::move(props.node);
    if (!data->props.style.direction) data->props.style.direction = FlexDirection::column;
    if (!data->props.style.gap) data->props.style.gap = 4.f;
    data->disabled = props.disabled;
    data->properties["collection_role"] = std::string("radio_group");
    data->properties["labelled_by"] = NodeReference{prefix + "-title"};
    return Element(std::move(data));
}

Element Tabs(std::string label, std::string selected, std::vector<TabOption> tabs, std::vector<Element> panel,
             CollectionProps props) {
    auto data = std::make_shared<detail::ElementData>();
    const auto prefix = props.node.id.value_or("");
    const auto source = props.node.source;
    auto error = [&](const char* code, const char* message) {
        data->diagnostics.push_back({code, Severity::error, {}, message});
    };
    if (prefix.empty()) error("tabs_id", "Give the Tabs a stable nonempty author ID.");
    if (label.empty()) error("tabs_label", "Give the Tabs a nonempty label.");
    if (tabs.empty()) error("tabs_empty", "Supply at least one tab.");
    if (tabs.size() > max_document_nodes) error("tabs_limit", "Keep tabs within document node limits.");
    auto part = [&](std::string id) { return Props{.id = std::move(id), .source = source}; };
    auto list = std::make_shared<detail::ElementData>();
    list->props = part(prefix + "-list");
    list->props.style.direction = FlexDirection::row;
    list->props.style.gap = 2.f;
    list->disabled = props.disabled;
    list->properties["collection_role"] = std::string("tab_list");
    list->properties["labelled_by"] = NodeReference{prefix + "-title"};
    auto title = part(prefix + "-title");
    title.style.display = Display::none;
    list->children.push_back(Text(std::move(label), std::move(title)));
    std::set<std::string> keys;
    bool found_selection = false;
    for (std::size_t i = 0; i < tabs.size() && i < max_document_nodes; ++i) {
        auto& tab = tabs[i];
        if (tab.key.empty() || !keys.insert(tab.key).second) error("tabs_keys", "Supply unique nonempty tab keys.");
        if (tab.label.empty()) error("tabs_tab_label", "Give each tab a nonempty label.");
        const bool current = selected == tab.key;
        found_selection = found_selection || current;
        auto entry = std::make_shared<detail::ElementData>();
        entry->props = part(prefix + "-tab-" + tab.key);
        entry->props.style.border = Edges{0, 0, 2, 0};
        entry->props.style.padding = Edges{4, 10, 4, 10};
        entry->props.style.border_color = current ? Color{0.25f, 0.5f, 0.85f, 1} : Color{0, 0, 0, 0};
        entry->action = std::move(tab.action);
        entry->disabled = tab.disabled;
        entry->properties["collection_role"] = std::string("tab");
        entry->properties["selected"] = current;
        entry->properties["labelled_by"] = NodeReference{prefix + "-label-" + tab.key};
        entry->children.push_back(Text(std::move(tab.label), part(prefix + "-label-" + tab.key)));
        list->children.push_back(Element(std::move(entry)));
    }
    if (!tabs.empty() && !found_selection) error("tabs_selection", "Select an existing tab key.");
    data->children.push_back(Element(std::move(list)));
    auto content = std::make_shared<detail::ElementData>();
    content->props = part(prefix + "-panel");
    content->props.style.direction = FlexDirection::column;
    content->children = std::move(panel);
    data->children.push_back(Element(std::move(content)));
    data->props = std::move(props.node);
    if (!data->props.style.direction) data->props.style.direction = FlexDirection::column;
    if (!data->props.style.gap) data->props.style.gap = 8.f;
    return Element(std::move(data));
}

Element Toolbar(std::string label, std::vector<Element> controls, Props props) {
    auto data = std::make_shared<detail::ElementData>();
    const auto prefix = props.id.value_or("");
    if (prefix.empty())
        data->diagnostics.push_back(
            {"toolbar_id", Severity::error, {}, "Give the Toolbar a stable nonempty author ID."});
    if (label.empty())
        data->diagnostics.push_back({"toolbar_label", Severity::error, {}, "Give the Toolbar a nonempty label."});
    Props title{.id = prefix + "-title", .source = props.source};
    title.style.display = Display::none;
    data->children.push_back(Text(std::move(label), std::move(title)));
    for (auto& control : controls) data->children.push_back(std::move(control));
    data->props = std::move(props);
    data->props.style.direction = FlexDirection::row;
    if (!data->props.style.align) data->props.style.align = Align::center;
    if (!data->props.style.gap) data->props.style.gap = 4.f;
    data->properties["collection_role"] = std::string("toolbar");
    data->properties["labelled_by"] = NodeReference{prefix + "-title"};
    return Element(std::move(data));
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
    children.push_back(Button(label + ": " + (chosen ? chosen->label : selected) + (props.expanded ? " [-]" : " [+]"),
                              std::move(toggle_action), {.node = control("-trigger"), .disabled = props.disabled}));
    if (props.expanded) {
        for (auto& option : options)
            children.push_back(
                Button((option.key == selected ? "* " : "") + option.label, std::move(option.action),
                       {.node = control("-option-" + option.key), .disabled = props.disabled || option.disabled}));
    }
    auto data = std::make_shared<detail::ElementData>();
    props.node.style.direction = FlexDirection::column;
    if (!props.node.style.gap) props.node.style.gap = 8.f;
    data->props = std::move(props.node);
    data->children = std::move(children);
    data->diagnostics = std::move(errors);
    return Element(std::move(data));
}

ComboBoxParts ComboBox(std::string label, std::string selected, std::vector<ComboBoxOption> options,
                       std::string toggle_action, ComboBoxProps props) {
    auto field = std::make_shared<detail::ElementData>();
    const auto prefix = props.node.id.value_or("");
    const auto source = props.node.source;
    auto error = [&](const char* code, const char* message) {
        field->diagnostics.push_back({code, Severity::error, {}, message});
    };
    if (prefix.empty()) error("combobox_id", "Give the ComboBox a stable nonempty author ID.");
    if (label.empty()) error("combobox_label", "Give the ComboBox a nonempty label.");
    if (options.empty()) error("combobox_empty", "Supply at least one ComboBox option.");
    if (options.size() > max_document_nodes) error("combobox_limit", "Keep ComboBox options within document limits.");
    auto part = [&](std::string id) { return Props{.id = std::move(id), .source = source}; };
    auto list = std::make_shared<detail::ElementData>();
    if (props.list.id)
        list->diagnostics.push_back(
            {"combobox_list_id", Severity::error, {}, "Leave the ComboBox list ID unset; it is always <id>-list."});
    std::set<std::string> keys;
    const ComboBoxOption* chosen = nullptr;
    auto list_title = part(prefix + "-list-title");
    list_title.style.display = Display::none;
    list->children.push_back(Text(label, std::move(list_title)));
    for (std::size_t i = 0; i < options.size() && i < max_document_nodes; ++i) {
        auto& option = options[i];
        if (option.key.empty() || !keys.insert(option.key).second)
            error("combobox_keys", "Supply unique nonempty ComboBox option keys.");
        if (option.label.empty()) error("combobox_option_label", "Give each ComboBox option a nonempty label.");
        const bool checked = option.key == selected;
        if (checked) chosen = &option;
        auto entry = std::make_shared<detail::ElementData>();
        entry->props = part(prefix + "-option-" + option.key);
        entry->props.style.direction = FlexDirection::row;
        entry->props.style.padding = Edges{2, 4, 2, 4};
        entry->props.style.gap = 4.f;
        entry->action = option.action;
        entry->disabled = props.disabled || option.disabled;
        entry->properties["collection_role"] = std::string("option");
        entry->properties["selected"] = checked;
        entry->properties["labelled_by"] = NodeReference{prefix + "-label-" + option.key};
        entry->children.push_back(Text(checked ? "*" : " ", {.source = source}));
        entry->children.push_back(Text(option.label, part(prefix + "-label-" + option.key)));
        list->children.push_back(Element(std::move(entry)));
    }
    if (!chosen && !options.empty()) error("combobox_selection", "Select an existing ComboBox option key.");
    const auto shown = chosen ? chosen->label : std::string{};

    auto title = part(prefix + "-title");
    title.style.display = Display::none;
    field->children.push_back(Text(std::move(label), std::move(title)));
    auto value = part(prefix + "-value");
    value.style.grow = 1.f;
    field->children.push_back(Text(shown, std::move(value)));
    field->children.push_back(Text(props.open ? "^" : "v", part(prefix + "-arrow")));
    field->props = std::move(props.node);
    field->props.style.direction = FlexDirection::row;
    if (!field->props.style.align) field->props.style.align = Align::center;
    if (!field->props.style.gap) field->props.style.gap = 8.f;
    field->action = std::move(toggle_action);
    field->disabled = props.disabled;
    field->properties["collection_role"] = std::string("combobox");
    field->properties["expanded"] = props.open;
    field->properties["edit_value"] = shown;
    field->properties["labelled_by"] = NodeReference{prefix + "-title"};

    auto list_props = std::move(props.list);
    list_props.id = prefix + "-list";
    if (!list_props.source) list_props.source = source;
    if (!list_props.style.overflow) list_props.style.overflow = Overflow::scroll;
    auto popup = Popup(prefix, list->children,
                       {.node = std::move(list_props), .open = props.open, .side = props.side, .gap = props.gap});
    auto data = std::make_shared<detail::ElementData>(*popup.data_);
    data->disabled = props.disabled;
    data->properties["collection_role"] = std::string("listbox");
    data->properties["labelled_by"] = NodeReference{prefix + "-list-title"};
    data->properties["popup_match_width"] = true;
    data->diagnostics = std::move(list->diagnostics);
    return {Element(std::move(field)), Element(std::move(data))};
}

namespace {
std::vector<Diagnostic> slider_range_errors(SliderRange range) {
    const auto span = std::int64_t{range.maximum} - range.minimum;
    if (span <= 0 || range.step <= 0 || span % range.step != 0)
        return {{"slider_range",
                 Severity::error,
                 {},
                 "Set minimum < maximum and a positive step that divides maximum - minimum exactly."}};
    return {};
}
}

Result<int> slider_value_at(SliderRange range, double position) {
    auto errors = slider_range_errors(range);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (!std::isfinite(position))
        return {std::nullopt,
                {{"slider_position", Severity::error, {}, "Supply a finite normalized Slider position."}}};
    const auto stops = (std::int64_t{range.maximum} - range.minimum) / range.step;
    const auto stop = static_cast<std::int64_t>(std::floor(std::clamp(position, 0., 1.) * stops + 0.5));
    return {static_cast<int>(std::int64_t{range.minimum} + stop * range.step), {}};
}

Element Slider(std::string label, int value, SliderRange range, std::string action, SliderProps props) {
    auto data = std::make_shared<detail::ElementData>();
    data->diagnostics = slider_range_errors(range);
    if (data->diagnostics.empty() &&
        (value < range.minimum || value > range.maximum || (std::int64_t{value} - range.minimum) % range.step != 0))
        data->diagnostics.push_back({"slider_value",
                                     Severity::error,
                                     {},
                                     "Supply a Slider value within [minimum, maximum] at a minimum + n * step stop."});
    const auto fraction = data->diagnostics.empty() ? (static_cast<double>(value) - range.minimum) /
                                                          (static_cast<double>(range.maximum) - range.minimum)
                                                    : 0.;
    auto part = [&](const char* suffix, StyleDeclarations style) {
        Props node;
        if (props.node.id) node.id = *props.node.id + suffix;
        node.source = props.node.source;
        node.style = std::move(style);
        return node;
    };
    data->children.push_back(Text(std::move(label) + ": " + std::to_string(value), {.source = props.node.source}));
    data->children.push_back(
        Row({Column({}, part("-fill", {.width = Dimension::points(static_cast<float>(192 * fraction)),
                                       .height = Dimension::points(12),
                                       .background = Color{0.25f, 0.5f, 0.85f, 1}})),
             Column({}, part("-thumb", {.width = Dimension::points(8),
                                        .height = Dimension::points(12),
                                        .background = Color{0.1f, 0.2f, 0.4f, 1}}))},
            part("-track", {.width = Dimension::points(200),
                            .height = Dimension::points(12),
                            .gap = 0.f,
                            .background = Color{0.75f, 0.78f, 0.82f, 1}})));
    data->props = std::move(props.node);
    data->props.style.direction = FlexDirection::column;
    if (!data->props.style.gap) data->props.style.gap = 4.f;
    data->disabled = props.disabled;
    data->action = std::move(action);
    return Element(std::move(data));
}

namespace {
std::vector<Diagnostic> split_limits_errors(SplitPaneLimits limits) {
    if (!std::isfinite(limits.minimum_first) || limits.minimum_first < 0 || !std::isfinite(limits.minimum_second) ||
        limits.minimum_second < 0 || !std::isfinite(limits.handle_size) || limits.handle_size <= 0)
        return {{"split_pane_limits",
                 Severity::error,
                 {},
                 "Supply finite nonnegative pane minima and a finite positive logical handle size."}};
    return {};
}
}

// One generated track for either axis; only the main-axis metrics and styles differ.
Element detail::Lowerer::scrollbar(bool horizontal, std::string label, float viewport, ScrollGeometry scroll,
                                   std::string action, ScrollbarProps props) {
    auto data = std::make_shared<detail::ElementData>();
    const char* name = horizontal ? "HorizontalScrollbar" : "VerticalScrollbar";
    const float extent = horizontal ? scroll.extent.width : scroll.extent.height;
    const float offset = horizontal ? scroll.offset.x : scroll.offset.y;
    if (!props.node.id || props.node.id->empty())
        data->diagnostics.push_back(
            {"scrollbar_id", Severity::error, {}, std::string("Give ") + name + " a stable author ID."});
    if (!std::isfinite(viewport) || viewport < 0 || !std::isfinite(extent) || extent < viewport ||
        !std::isfinite(offset) || offset < 0 || offset > extent - viewport)
        data->diagnostics.push_back({"scrollbar_geometry",
                                     Severity::error,
                                     {},
                                     horizontal ? "Supply the accepted scroll viewport width, content extent and "
                                                  "clamped horizontal offset."
                                                : "Supply the accepted scroll viewport height, content extent and "
                                                  "clamped vertical offset."});
    if (!std::isfinite(props.minimum_thumb) || props.minimum_thumb <= 0)
        data->diagnostics.push_back(
            {"scrollbar_minimum_thumb", Severity::error, {}, "Supply a finite positive minimum thumb size."});
    const bool valid = data->diagnostics.empty();
    const float length = valid ? viewport : 0;
    const float maximum = valid ? extent - viewport : 0;
    const float thumb_size =
        valid && maximum > 0
            ? std::min(length, std::max(props.minimum_thumb, static_cast<float>(double(length) * length / extent)))
            : 0;
    const float travel = length - thumb_size;
    const float before = maximum > 0 ? static_cast<float>(double(travel) * offset / maximum) : 0;
    const float minimum = valid ? std::min(length, props.minimum_thumb) : 0;
    const std::string id = props.node.id.value_or("");
    auto spacer = std::make_shared<detail::ElementData>();
    spacer->props = {.id = id + "-before", .source = props.node.source};
    (horizontal ? spacer->props.style.width : spacer->props.style.height) = Dimension::points(before);
    spacer->properties["pointer_events"] = false;
    auto thumb = std::make_shared<detail::ElementData>();
    thumb->props = {.id = id + "-thumb",
                    .style = {.display = maximum > 0 && length > 0 ? Display::flex : Display::none,
                              .background = props.focused   ? Color{0.15f, 0.35f, 0.8f, 1}
                                            : props.hovered ? Color{0.3f, 0.5f, 0.75f, 1}
                                                            : Color{0.45f, 0.5f, 0.6f, 1},
                              .corner_radius = 3.f},
                    .source = props.node.source};
    if (horizontal) {
        thumb->props.style.width = Dimension::points(thumb_size);
        thumb->props.style.min_width = minimum;
    } else {
        thumb->props.style.height = Dimension::points(thumb_size);
        thumb->props.style.min_height = minimum;
    }
    thumb->properties["scrollbar"] = true;
    thumb->properties["labelled_by"] = NodeReference{id + "-label"};
    thumb->disabled = props.disabled || maximum <= 0 || travel <= 0;
    thumb->action = std::move(action);
    thumb->children.push_back(
        Text(std::move(label) + ": " + std::to_string(valid ? offset : 0) + " / " + std::to_string(maximum),
             {.id = id + "-label", .style = {.display = Display::none}, .source = props.node.source}));
    data->children = {Element(std::move(spacer)), Element(std::move(thumb))};
    data->props = std::move(props.node);
    auto& style = data->props.style;
    style.direction = horizontal ? FlexDirection::row : FlexDirection::column;
    style.justify = Justify::start;
    style.align = Align::stretch;
    style.overflow = Overflow::clip;
    if (horizontal) {
        style.width = Dimension::points(length);
        if (!style.height) style.height = Dimension::points(12);
    } else {
        if (!style.width) style.width = Dimension::points(12);
        style.height = Dimension::points(length);
    }
    style.border = Edges{};
    style.padding = Edges{};
    style.gap = 0.f;
    style.grow = 0.f;
    style.shrink = 0.f;
    if (!style.background) style.background = Color{0.85f, 0.87f, 0.9f, 1};
    return Element(std::move(data));
}

Element VerticalScrollbar(std::string label, float viewport, ScrollGeometry scroll, std::string action,
                          ScrollbarProps props) {
    return detail::Lowerer::scrollbar(false, std::move(label), viewport, scroll, std::move(action), std::move(props));
}
Element HorizontalScrollbar(std::string label, float viewport, ScrollGeometry scroll, std::string action,
                            ScrollbarProps props) {
    return detail::Lowerer::scrollbar(true, std::move(label), viewport, scroll, std::move(action), std::move(props));
}

Result<float> split_pane_size_at(SplitPaneLimits limits, float available, float requested) {
    auto errors = split_limits_errors(limits);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (!std::isfinite(available) || available < 0 || !std::isfinite(requested))
        return {std::nullopt,
                {{"split_pane_size",
                  Severity::error,
                  {},
                  "Supply a finite nonnegative parent content size and a finite requested pane size."}}};
    const auto maximum = std::max(limits.minimum_first, available - limits.handle_size - limits.minimum_second);
    return {std::clamp(requested, limits.minimum_first, maximum), {}};
}

Element detail::Lowerer::split_pane(Element first, Element second, float size, std::string action, SplitPaneProps props,
                                    FlexDirection direction) {
    auto data = std::make_shared<detail::ElementData>();
    data->diagnostics = split_limits_errors(props.limits);
    if (!std::isfinite(size) || size < 0)
        data->diagnostics.push_back(
            {"split_pane_size", Severity::error, {}, "Supply a finite nonnegative first-pane logical size."});
    if (!props.node.id || props.node.id->empty())
        data->diagnostics.push_back({"split_pane_id",
                                     Severity::error,
                                     {},
                                     "Give SplitPane a stable author ID for its handle and retained host state."});
    const std::string id = props.node.id.value_or("");
    const bool horizontal = direction == FlexDirection::row;
    auto pane = [&](Element child, const char* suffix, bool leading) {
        auto content =
            child.data_ ? std::make_shared<detail::ElementData>(*child.data_) : std::shared_ptr<detail::ElementData>{};
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
    if (horizontal)
        handle->props.style.width = Dimension::points(props.limits.handle_size);
    else
        handle->props.style.height = Dimension::points(props.limits.handle_size);
    handle->properties[std::string(property_names::separator)] = true;
    handle->properties["labelled_by"] = NodeReference{id + "-handle-label"};
    handle->disabled = props.disabled;
    handle->action = std::move(action);
    handle->children.push_back(
        Text(props.label + ": " + std::to_string(size) + " logical units",
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
    return detail::Lowerer::split_pane(std::move(first), std::move(second), size, std::move(action), std::move(props),
                                       FlexDirection::row);
}
Element VerticalSplitPane(Element first, Element second, float size, std::string action, SplitPaneProps props) {
    return detail::Lowerer::split_pane(std::move(first), std::move(second), size, std::move(action), std::move(props),
                                       FlexDirection::column);
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
