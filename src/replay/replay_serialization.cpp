#include <tessera/replay/replay_serialization.hpp>
#include "replay_detail.hpp"
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;
struct Failure { std::vector<Diagnostic> diagnostics; };

// Largest integer whose binary64 value is exact; timestamps beyond it are not serializable.
constexpr double max_exact_integer = 9007199254740991.0;

[[noreturn]] void fail(std::string code, std::string path, std::string message) {
    throw Failure{{{std::move(code), Severity::error, std::move(path), std::move(message), {}}}};
}
template<class T>
const T& as(const JsonValue& value, const std::string& path, std::string_view expected) {
    if (const auto* result = std::get_if<T>(&value.value)) return *result;
    fail("schema_type", path, "Expected " + std::string(expected) + ".");
}
const Object& object(const JsonValue& value, const std::string& path, std::initializer_list<std::string_view> fields) {
    const auto& result = as<Object>(value, path, "an object");
    for (const auto& [key, child] : result) {
        (void)child;
        if (std::find(fields.begin(), fields.end(), key) == fields.end())
            fail("unknown_field", path + "/" + detail::pointer_token(key), "Remove this unknown replay field.");
    }
    for (auto field : fields)
        if (!result.contains(field)) fail("missing_field", path + "/" + std::string(field), "Supply this required field.");
    return result;
}
const Array& array(const JsonValue& value, const std::string& path) { return as<Array>(value, path, "an array"); }
const std::string& string(const JsonValue& value, const std::string& path) {
    return as<std::string>(value, path, "a string");
}
// The discriminator of a tagged object, read before its field set is known.
const std::string& tag(const JsonValue& value, const std::string& path, const std::string& field) {
    const auto& o = as<Object>(value, path, "an object");
    const auto found = o.find(field);
    if (found == o.end()) fail("missing_field", path + "/" + field, "Supply this required field.");
    return string(found->second, path + "/" + field);
}
bool boolean(const JsonValue& value, const std::string& path) { return as<bool>(value, path, "a boolean"); }
double integer(const JsonValue& value, const std::string& path, double minimum, double maximum, std::string_view expected) {
    const auto result = as<double>(value, path, expected);
    if (std::floor(result) != result || result < minimum || result > maximum)
        fail("schema_type", path, "Expected " + std::string(expected) + ".");
    return result;
}
std::uint32_t unsigned32(const JsonValue& value, const std::string& path) {
    return static_cast<std::uint32_t>(
        integer(value, path, 0, std::numeric_limits<std::uint32_t>::max(), "an unsigned 32-bit integer"));
}
float real(const JsonValue& value, const std::string& path) {
    const auto result = as<double>(value, path, "a number");
    if (std::abs(result) > std::numeric_limits<float>::max())
        fail("out_of_range", path, "Expected a number representable as a finite binary32 value.");
    return static_cast<float>(result);
}
// A number, or the keyword selecting the field's non-numeric value.
bool keyword(const JsonValue& value, const std::string& path, std::string_view word) {
    const auto* text = std::get_if<std::string>(&value.value);
    if (!text) return false;
    if (*text != word) fail("unknown_value", path, "Expected a number or \"" + std::string(word) + "\".");
    return true;
}
template<class E, std::size_t N>
E choice(const JsonValue& value, const std::string& path, const std::array<std::string_view, N>& names) {
    const auto& text = string(value, path);
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == text) return static_cast<E>(i);
    std::string expected;
    for (std::size_t i = 0; i < N; ++i) expected += (i ? ", " : "") + std::string(names[i]);
    fail("unknown_value", path, "Expected one of: " + expected + ".");
}

// Names are indexed by enumeration value.
constexpr std::array<std::string_view, 2> display_names{"flex", "none"};
constexpr std::array<std::string_view, 2> direction_names{"column", "row"};
constexpr std::array<std::string_view, 4> justify_names{"start", "center", "end", "space_between"};
constexpr std::array<std::string_view, 4> align_names{"start", "center", "end", "stretch"};
constexpr std::array<std::string_view, 3> overflow_names{"visible", "clip", "scroll"};
constexpr std::array<std::string_view, 2> visibility_names{"visible", "hidden"};
constexpr std::array<std::string_view, 3> button_names{"primary", "secondary", "middle"};
constexpr std::array<std::string_view, 4> direction_key_names{"up", "down", "left", "right"};
constexpr std::array<std::string_view, 3> role_names{"root", "button", "text"};
constexpr std::array<std::string_view, 2> text_kind_names{"placeholder", "font_profile"};

Point point(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"x", "y"});
    return {real(o.at("x"), path + "/x"), real(o.at("y"), path + "/y")};
}
Size size(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"width", "height"});
    return {real(o.at("width"), path + "/width"), real(o.at("height"), path + "/height")};
}
Edges edges(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"top", "right", "bottom", "left"});
    return {real(o.at("top"), path + "/top"), real(o.at("right"), path + "/right"),
            real(o.at("bottom"), path + "/bottom"), real(o.at("left"), path + "/left")};
}
Color color(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"r", "g", "b", "a"});
    return {real(o.at("r"), path + "/r"), real(o.at("g"), path + "/g"), real(o.at("b"), path + "/b"),
            real(o.at("a"), path + "/a")};
}
Dimension dimension(const JsonValue& value, const std::string& path) {
    if (keyword(value, path, "auto")) return {};
    return Dimension::points(real(value, path));
}
float maximum(const JsonValue& value, const std::string& path) {
    if (keyword(value, path, "none")) return std::numeric_limits<float>::infinity();
    return real(value, path);
}
Modifiers modifiers(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"shift", "control", "alt", "super"});
    return {boolean(o.at("shift"), path + "/shift"), boolean(o.at("control"), path + "/control"),
            boolean(o.at("alt"), path + "/alt"), boolean(o.at("super"), path + "/super")};
}

ResolvedStyle style(const JsonValue& value, const std::string& path) {
    const auto& o = object(value, path, {"display", "direction", "justify", "align", "overflow", "width", "height",
        "min_width", "min_height", "max_width", "max_height", "margin", "border", "padding", "gap", "grow", "shrink",
        "visibility", "opacity", "background", "border_color", "corner_radius", "text", "color"});
    ResolvedStyle s;
    s.display = choice<Display>(o.at("display"), path + "/display", display_names);
    s.direction = choice<FlexDirection>(o.at("direction"), path + "/direction", direction_names);
    s.justify = choice<Justify>(o.at("justify"), path + "/justify", justify_names);
    s.align = choice<Align>(o.at("align"), path + "/align", align_names);
    s.overflow = choice<Overflow>(o.at("overflow"), path + "/overflow", overflow_names);
    s.width = dimension(o.at("width"), path + "/width");
    s.height = dimension(o.at("height"), path + "/height");
    s.min_width = real(o.at("min_width"), path + "/min_width");
    s.min_height = real(o.at("min_height"), path + "/min_height");
    s.max_width = maximum(o.at("max_width"), path + "/max_width");
    s.max_height = maximum(o.at("max_height"), path + "/max_height");
    s.margin = edges(o.at("margin"), path + "/margin");
    s.border = edges(o.at("border"), path + "/border");
    s.padding = edges(o.at("padding"), path + "/padding");
    s.gap = real(o.at("gap"), path + "/gap");
    s.grow = real(o.at("grow"), path + "/grow");
    s.shrink = real(o.at("shrink"), path + "/shrink");
    s.visibility = choice<Visibility>(o.at("visibility"), path + "/visibility", visibility_names);
    s.opacity = real(o.at("opacity"), path + "/opacity");
    s.background = color(o.at("background"), path + "/background");
    s.border_color = color(o.at("border_color"), path + "/border_color");
    s.corner_radius = real(o.at("corner_radius"), path + "/corner_radius");
    const auto text_path = path + "/text";
    const auto& text = object(o.at("text"), text_path, {"font", "size", "line_height", "weight"});
    s.text.font = {unsigned32(text.at("font"), text_path + "/font")};
    s.text.size = real(text.at("size"), text_path + "/size");
    if (!keyword(text.at("line_height"), text_path + "/line_height", "normal"))
        s.text.line_height = real(text.at("line_height"), text_path + "/line_height");
    s.text.weight = static_cast<std::uint16_t>(integer(text.at("weight"), text_path + "/weight", 0,
                                                       std::numeric_limits<std::uint16_t>::max(),
                                                       "an unsigned 16-bit integer"));
    s.color = color(o.at("color"), path + "/color");
    return s;
}
std::vector<ResolvedStyle> styles(const JsonValue& value, const std::string& path) {
    const auto& values = array(value, path);
    if (values.size() > max_document_nodes) fail("out_of_range", path, "Use at most one style per document node (10000).");
    std::vector<ResolvedStyle> result;
    for (std::size_t i = 0; i < values.size(); ++i) result.push_back(style(values[i], path + "/" + std::to_string(i)));
    return result;
}
UiDocument document(const JsonValue& value, const std::string& path, const ValidationContext& context) {
    auto result = detail::read_document_json(value, path, context);
    if (!result) throw Failure{std::move(result.diagnostics)};
    return std::move(*result.value);
}

InspectionTarget target(const JsonValue& value, const std::string& path) {
    const auto& kind = tag(value, path, "kind");
    if (kind == "id") {
        const auto& o = object(value, path, {"kind", "id"});
        return AuthorIdTarget{string(o.at("id"), path + "/id")};
    }
    if (kind == "semantic") {
        const auto& o = object(value, path, {"kind", "role", "name"});
        return SemanticTarget{choice<SemanticRole>(o.at("role"), path + "/role", role_names),
                              string(o.at("name"), path + "/name")};
    }
    if (kind == "path") {
        const auto& o = object(value, path, {"kind", "children"});
        PathTarget result;
        const auto& children = array(o.at("children"), path + "/children");
        for (std::size_t i = 0; i < children.size(); ++i)
            result.children.push_back(unsigned32(children[i], path + "/children/" + std::to_string(i)));
        return result;
    }
    if (kind == "point") {
        const auto& o = object(value, path, {"kind", "position"});
        return PointTarget{point(o.at("position"), path + "/position")};
    }
    fail("unknown_value", path + "/kind", "Expected one of: id, semantic, path, point.");
}

std::chrono::microseconds timestamp(const Object& o, const std::string& path) {
    return std::chrono::microseconds{static_cast<std::int64_t>(
        integer(o.at("timestamp"), path + "/timestamp", -max_exact_integer, max_exact_integer,
                "an integer microsecond count within +/-(2^53 - 1)"))};
}

ReplayStep step(const JsonValue& value, const std::string& path, const ValidationContext& context) {
    const auto& type = tag(value, path, "type");
    auto fields = [&](std::initializer_list<std::string_view> names) -> const Object& {
        return object(value, path, names);
    };
    if (type == "pointer_move") {
        const auto& o = fields({"type", "timestamp", "pointer", "position", "modifiers"});
        return InputEvent{timestamp(o, path), PointerMove{{unsigned32(o.at("pointer"), path + "/pointer")},
            point(o.at("position"), path + "/position"), modifiers(o.at("modifiers"), path + "/modifiers")}};
    }
    if (type == "pointer_down" || type == "pointer_up") {
        const auto& o = fields({"type", "timestamp", "pointer", "position", "button", "modifiers"});
        const PointerId pointer{unsigned32(o.at("pointer"), path + "/pointer")};
        const auto position = point(o.at("position"), path + "/position");
        const auto button = choice<PointerButton>(o.at("button"), path + "/button", button_names);
        const auto keys = modifiers(o.at("modifiers"), path + "/modifiers");
        if (type == "pointer_down") return InputEvent{timestamp(o, path), PointerDown{pointer, position, button, keys}};
        return InputEvent{timestamp(o, path), PointerUp{pointer, position, button, keys}};
    }
    if (type == "pointer_cancel") {
        const auto& o = fields({"type", "timestamp", "pointer"});
        return InputEvent{timestamp(o, path), PointerCancel{{unsigned32(o.at("pointer"), path + "/pointer")}}};
    }
    if (type == "scroll") {
        const auto& o = fields({"type", "timestamp", "position", "delta", "modifiers"});
        return InputEvent{timestamp(o, path), Scroll{point(o.at("position"), path + "/position"),
            point(o.at("delta"), path + "/delta"), modifiers(o.at("modifiers"), path + "/modifiers")}};
    }
    if (type == "navigate") {
        const auto& o = fields({"type", "timestamp", "direction", "repeat"});
        return InputEvent{timestamp(o, path), Navigate{choice<Direction>(o.at("direction"), path + "/direction",
            direction_key_names), boolean(o.at("repeat"), path + "/repeat")}};
    }
    if (type == "focus_next") return InputEvent{timestamp(fields({"type", "timestamp"}), path), FocusNext{}};
    if (type == "focus_previous") return InputEvent{timestamp(fields({"type", "timestamp"}), path), FocusPrevious{}};
    if (type == "activate") return InputEvent{timestamp(fields({"type", "timestamp"}), path), Activate{}};
    if (type == "cancel") return InputEvent{timestamp(fields({"type", "timestamp"}), path), Cancel{}};
    if (type == "resize") return ReplayResize{size(fields({"type", "viewport"}).at("viewport"), path + "/viewport")};
    if (type == "reload") {
        const auto& o = fields({"type", "document", "styles"});
        auto replacement = document(o.at("document"), path + "/document", context);
        return ReplayReload{std::move(replacement), styles(o.at("styles"), path + "/styles")};
    }
    if (type == "focus") return ReplayFocus{target(fields({"type", "target"}).at("target"), path + "/target")};
    if (type == "semantic_action") {
        const auto& o = fields({"type", "target", "binding"});
        return ReplaySemanticAction{target(o.at("target"), path + "/target"), string(o.at("binding"), path + "/binding")};
    }
    if (type == "key_down" || type == "key_up" || type == "text_input")
        fail("unsupported_event", path + "/type", "Replay accepts host-translated commands; record navigate, "
             "focus_next, focus_previous, activate, or cancel instead of keys or text.");
    fail("unknown_value", path + "/type",
         "Expected a pointer, scroll, command, resize, reload, focus, or semantic_action step type.");
}

bool identifier(std::string_view value) {
    return !value.empty() && detail::valid_utf8(value) &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

ReplayRecording read(const JsonValue& value) {
    const auto& root = object(value, "", {"version", "actions", "policy", "environment", "viewport", "document",
                                          "styles", "steps"});
    ReplayRecording result;
    result.version = unsigned32(root.at("version"), "/version");
    if (result.version != replay_version) fail("unsupported_version", "/version", "Expected replay version 1.");
    const auto& actions = array(root.at("actions"), "/actions");
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto location = "/actions/" + std::to_string(i);
        const auto& action = string(actions[i], location);
        if (!identifier(action))
            fail("invalid_identifier", location, "Action names must be nonempty and contain no control characters.");
        if (!result.context.actions.insert(action).second)
            fail("duplicate_action", location, "Declare each host action once.");
    }
    const auto& policy = object(root.at("policy"), "/policy", {"press_focus", "reveal_focus"});
    result.policy = {boolean(policy.at("press_focus"), "/policy/press_focus"),
                     boolean(policy.at("reveal_focus"), "/policy/reveal_focus")};
    const auto& environment = object(root.at("environment"), "/environment", {"scale", "locale", "text"});
    result.environment.scale = real(environment.at("scale"), "/environment/scale");
    result.environment.locale = string(environment.at("locale"), "/environment/locale");
    const auto& kind = tag(environment.at("text"), "/environment/text", "kind");
    if (kind == "placeholder") {
        object(environment.at("text"), "/environment/text", {"kind"});
    } else if (kind == "font_profile") {
        const auto& text = object(environment.at("text"), "/environment/text", {"kind", "id", "sha256"});
        result.environment.text = {ReplayText::Kind::font_profile, string(text.at("id"), "/environment/text/id"),
                                   string(text.at("sha256"), "/environment/text/sha256")};
    } else {
        fail("unknown_value", "/environment/text/kind", "Expected one of: placeholder, font_profile.");
    }
    result.viewport = size(root.at("viewport"), "/viewport");
    result.document = document(root.at("document"), "/document", result.context);
    result.styles = styles(root.at("styles"), "/styles");
    const auto& steps = array(root.at("steps"), "/steps");
    if (steps.size() > max_replay_steps)
        fail("out_of_range", "/steps", "Use at most " + std::to_string(max_replay_steps) + " replay steps.");
    for (std::size_t i = 0; i < steps.size(); ++i)
        result.steps.push_back(step(steps[i], "/steps/" + std::to_string(i), result.context));
    return result;
}

void relocate(std::vector<Diagnostic>& out, std::vector<Diagnostic> diagnostics, const std::string& prefix) {
    for (auto& diagnostic : diagnostics) {
        diagnostic.path = prefix + diagnostic.path;
        out.push_back(std::move(diagnostic));
    }
}

void check_target(const InspectionTarget& value, const std::string& path, detail::Checker& check) {
    if (const auto* id = std::get_if<AuthorIdTarget>(&value)) {
        if (!detail::valid_utf8(id->id)) check.error("invalid_utf8", path + "/id", "Use valid UTF-8.");
    } else if (const auto* semantic = std::get_if<SemanticTarget>(&value)) {
        check.enumeration(semantic->role, SemanticRole::text, path + "/role");
        if (!detail::valid_utf8(semantic->name)) check.error("invalid_utf8", path + "/name", "Use valid UTF-8.");
    } else if (const auto* position = std::get_if<PointTarget>(&value)) {
        check.point(position->position, path + "/position");
    }
}

// Every value check that needs no shaper or snapshot, at playback's diagnostic paths. Loaded documents were
// already validated while reading.
std::vector<Diagnostic> check_values(const ReplayRecording& recording, bool documents) {
    auto errors = detail::check_recording(recording);
    detail::Checker check(errors);
    std::size_t index = 0;
    for (const auto& action : recording.context.actions) {
        if (!identifier(action))
            check.error("invalid_identifier", "/actions/" + std::to_string(index),
                        "Action names must be nonempty UTF-8 and contain no control characters.");
        ++index;
    }
    check.size(recording.viewport, "/viewport");
    if (documents) relocate(errors, validate(recording.document, recording.context), "/document");
    auto styles = [&](const std::vector<ResolvedStyle>& values, const std::string& path) {
        if (values.size() > max_document_nodes)
            check.error("out_of_range", path, "Use at most one style per document node (10000).");
        for (std::size_t i = 0; i < values.size(); ++i)
            relocate(errors, validate(values[i], path + "/" + std::to_string(i)), "");
    };
    styles(recording.styles, "/styles");
    for (std::size_t i = 0; i < recording.steps.size(); ++i) {
        const auto at = "/steps/" + std::to_string(i);
        std::visit([&](const auto& step) {
            using T = std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<T, InputEvent>) {
                relocate(errors, validate(step), at);
                if (std::holds_alternative<KeyDown>(step.data) || std::holds_alternative<KeyUp>(step.data) ||
                    std::holds_alternative<TextInput>(step.data))
                    check.error("unsupported_event", at + "/event",
                                "Replay accepts host-translated commands, not keys or text.");
                if (std::abs(static_cast<double>(step.timestamp.count())) > max_exact_integer)
                    check.error("out_of_range", at + "/timestamp",
                                "Timestamps must stay within +/-(2^53 - 1) microseconds to be exact in JSON.");
            } else if constexpr (std::is_same_v<T, ReplayResize>) {
                check.size(step.viewport, at + "/viewport");
            } else if constexpr (std::is_same_v<T, ReplayReload>) {
                if (documents) relocate(errors, validate(step.document, recording.context), at + "/document");
                styles(step.styles, at + "/styles");
            } else if constexpr (std::is_same_v<T, ReplayFocus>) {
                check_target(step.target, at + "/target", check);
            } else {
                check_target(step.target, at + "/target", check);
                if (!detail::valid_utf8(step.binding)) check.error("invalid_utf8", at + "/binding", "Use valid UTF-8.");
            }
        }, recording.steps[i]);
    }
    return errors;
}

// The shortest binary32 spelling when it reads back to the same float through binary64, else the exact value.
JsonValue real_json(float value) {
    if (value == 0) return JsonValue{0.0};
    char buffer[32];
    const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    double shortest = 0;
    std::from_chars(buffer, written.ptr, shortest);
    return JsonValue{static_cast<float>(shortest) == value ? shortest : static_cast<double>(value)};
}
JsonValue number(double value) { return JsonValue{value}; }
JsonValue text(std::string_view value) { return JsonValue{std::string(value)}; }
JsonValue point_json(const Point& p) { return JsonValue{Object{{"x", real_json(p.x)}, {"y", real_json(p.y)}}}; }
JsonValue size_json(const Size& s) {
    return JsonValue{Object{{"width", real_json(s.width)}, {"height", real_json(s.height)}}};
}
JsonValue edges_json(const Edges& e) {
    return JsonValue{Object{{"top", real_json(e.top)}, {"right", real_json(e.right)},
                            {"bottom", real_json(e.bottom)}, {"left", real_json(e.left)}}};
}
JsonValue color_json(const Color& c) {
    return JsonValue{Object{{"r", real_json(c.r)}, {"g", real_json(c.g)}, {"b", real_json(c.b)}, {"a", real_json(c.a)}}};
}
JsonValue modifiers_json(const Modifiers& m) {
    return JsonValue{Object{{"shift", JsonValue{m.shift}}, {"control", JsonValue{m.control}},
                            {"alt", JsonValue{m.alt}}, {"super", JsonValue{m.super}}}};
}
template<class E, std::size_t N>
JsonValue name(E value, const std::array<std::string_view, N>& names) {
    return text(names[static_cast<std::size_t>(value)]);
}
JsonValue dimension_json(const Dimension& d) {
    return d.kind == Dimension::Kind::automatic ? text("auto") : real_json(d.value);
}
JsonValue maximum_json(float value) { return std::isinf(value) ? text("none") : real_json(value); }

JsonValue style_json(const ResolvedStyle& s) {
    return JsonValue{Object{
        {"display", name(s.display, display_names)}, {"direction", name(s.direction, direction_names)},
        {"justify", name(s.justify, justify_names)}, {"align", name(s.align, align_names)},
        {"overflow", name(s.overflow, overflow_names)}, {"width", dimension_json(s.width)},
        {"height", dimension_json(s.height)}, {"min_width", real_json(s.min_width)},
        {"min_height", real_json(s.min_height)}, {"max_width", maximum_json(s.max_width)},
        {"max_height", maximum_json(s.max_height)}, {"margin", edges_json(s.margin)},
        {"border", edges_json(s.border)}, {"padding", edges_json(s.padding)}, {"gap", real_json(s.gap)},
        {"grow", real_json(s.grow)}, {"shrink", real_json(s.shrink)},
        {"visibility", name(s.visibility, visibility_names)}, {"opacity", real_json(s.opacity)},
        {"background", color_json(s.background)}, {"border_color", color_json(s.border_color)},
        {"corner_radius", real_json(s.corner_radius)},
        {"text", JsonValue{Object{{"font", number(s.text.font.value)}, {"size", real_json(s.text.size)},
                                  {"line_height", s.text.line_height ? real_json(*s.text.line_height) : text("normal")},
                                  {"weight", number(s.text.weight)}}}},
        {"color", color_json(s.color)}}};
}
JsonValue styles_json(const std::vector<ResolvedStyle>& values) {
    Array result;
    for (const auto& value : values) result.push_back(style_json(value));
    return JsonValue{std::move(result)};
}
JsonValue target_json(const InspectionTarget& value) {
    return std::visit([](const auto& t) -> JsonValue {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, AuthorIdTarget>) return JsonValue{Object{{"kind", text("id")}, {"id", text(t.id)}}};
        else if constexpr (std::is_same_v<T, SemanticTarget>)
            return JsonValue{Object{{"kind", text("semantic")}, {"role", name(t.role, role_names)}, {"name", text(t.name)}}};
        else if constexpr (std::is_same_v<T, PathTarget>) {
            Array children;
            for (const auto child : t.children) children.push_back(number(child));
            return JsonValue{Object{{"kind", text("path")}, {"children", JsonValue{std::move(children)}}}};
        } else return JsonValue{Object{{"kind", text("point")}, {"position", point_json(t.position)}}};
    }, value);
}
JsonValue event_json(const InputEvent& event) {
    Object result{{"timestamp", number(static_cast<double>(event.timestamp.count()))}};
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, PointerMove>) {
            result["type"] = text("pointer_move");
            result["pointer"] = number(data.pointer.value);
            result["position"] = point_json(data.position);
            result["modifiers"] = modifiers_json(data.modifiers);
        } else if constexpr (std::is_same_v<T, PointerDown> || std::is_same_v<T, PointerUp>) {
            result["type"] = text(std::is_same_v<T, PointerDown> ? "pointer_down" : "pointer_up");
            result["pointer"] = number(data.pointer.value);
            result["position"] = point_json(data.position);
            result["button"] = name(data.button, button_names);
            result["modifiers"] = modifiers_json(data.modifiers);
        } else if constexpr (std::is_same_v<T, PointerCancel>) {
            result["type"] = text("pointer_cancel");
            result["pointer"] = number(data.pointer.value);
        } else if constexpr (std::is_same_v<T, Scroll>) {
            result["type"] = text("scroll");
            result["position"] = point_json(data.position);
            result["delta"] = point_json(data.delta);
            result["modifiers"] = modifiers_json(data.modifiers);
        } else if constexpr (std::is_same_v<T, Navigate>) {
            result["type"] = text("navigate");
            result["direction"] = name(data.direction, direction_key_names);
            result["repeat"] = JsonValue{data.repeat};
        } else if constexpr (std::is_same_v<T, FocusNext>) result["type"] = text("focus_next");
        else if constexpr (std::is_same_v<T, FocusPrevious>) result["type"] = text("focus_previous");
        else if constexpr (std::is_same_v<T, Activate>) result["type"] = text("activate");
        else if constexpr (std::is_same_v<T, Cancel>) result["type"] = text("cancel");
        // check_values rejects keys and text before writing.
    }, event.data);
    return JsonValue{std::move(result)};
}
JsonValue step_json(const ReplayStep& step) {
    return std::visit([](const auto& s) -> JsonValue {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, InputEvent>) return event_json(s);
        else if constexpr (std::is_same_v<T, ReplayResize>)
            return JsonValue{Object{{"type", text("resize")}, {"viewport", size_json(s.viewport)}}};
        else if constexpr (std::is_same_v<T, ReplayReload>)
            return JsonValue{Object{{"type", text("reload")}, {"document", detail::write_document_json(s.document)},
                                    {"styles", styles_json(s.styles)}}};
        else if constexpr (std::is_same_v<T, ReplayFocus>)
            return JsonValue{Object{{"type", text("focus")}, {"target", target_json(s.target)}}};
        else return JsonValue{Object{{"type", text("semantic_action")}, {"target", target_json(s.target)},
                                     {"binding", text(s.binding)}}};
    }, step);
}

JsonValue write(const ReplayRecording& recording) {
    Array actions, steps;
    for (const auto& action : recording.context.actions) actions.push_back(text(action));
    for (const auto& step : recording.steps) steps.push_back(step_json(step));
    const auto& service = recording.environment.text;
    Object text_service{{"kind", name(service.kind, text_kind_names)}};
    if (service.kind == ReplayText::Kind::font_profile) {
        text_service["id"] = text(service.id);
        text_service["sha256"] = text(service.sha256);
    }
    return JsonValue{Object{
        {"version", number(recording.version)},
        {"actions", JsonValue{std::move(actions)}},
        {"policy", JsonValue{Object{{"press_focus", JsonValue{recording.policy.press_focus}},
                                    {"reveal_focus", JsonValue{recording.policy.reveal_focus}}}}},
        {"environment", JsonValue{Object{{"scale", real_json(recording.environment.scale)},
                                         {"locale", text(recording.environment.locale)},
                                         {"text", JsonValue{std::move(text_service)}}}}},
        {"viewport", size_json(recording.viewport)},
        {"document", detail::write_document_json(recording.document)},
        {"styles", styles_json(recording.styles)},
        {"steps", JsonValue{std::move(steps)}}}};
}

} // namespace

Result<ReplayRecording> load_replay(std::string_view source) {
    auto json = detail::parse_json(source, max_serialized_replay_bytes, max_replay_json_values);
    if (!json) return {std::nullopt, std::move(json.diagnostics)};
    try {
        auto recording = read(json.value->root);
        auto errors = check_values(recording, false);
        if (!errors.empty()) {
            detail::annotate_json(errors, *json.value);
            return {std::nullopt, std::move(errors)};
        }
        return {std::move(recording), {}};
    } catch (Failure& failure) {
        detail::annotate_json(failure.diagnostics, *json.value);
        return {std::nullopt, std::move(failure.diagnostics)};
    }
}

Result<std::string> save_replay(const ReplayRecording& recording) {
    auto errors = check_values(recording, true);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto output = detail::write_json_value(write(recording));
    output += '\n';
    if (output.size() > max_serialized_replay_bytes)
        return {std::nullopt, {{"size_limit", Severity::error, "", "Canonical replay exceeds the byte limit (64 MiB).", {}}}};
    return {std::move(output), {}};
}

} // namespace tessera
