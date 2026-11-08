#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
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
const ValidationContext actions{{"before", "name.edit", "scale.edit"}};
constexpr ui::NumberRange scale_range{0, 4, 0.25};
struct Fixture {
    ui::Session session;
    Signal<TextEditState> name = session.state("name", take(make_text_edit_state(1, "Cube")));
    Signal<TextEditState> scale = session.state("scale", take(make_text_edit_state(2, "1.5")));
    Signal<bool> disabled = session.state("disabled", false);
    ui::Presentation authored;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{320, 240};
    ui::Element form() const {
        return ui::Column({ui::Button("Before", "before", {.node = {.id = "before"}}),
                           ui::TextInput("Name", name, "name.edit", {.node = {.id = "name"}, .disabled = disabled}),
                           ui::NumberInput("Scale", scale, "scale.edit",
                                           {.node = {.id = "scale", .style = {.width = Dimension::points(200)}},
                                            .disabled = disabled})},
                          {.id = "root"});
    }
    void build() {
        authored = take(session.lower(form(), actions));
        document = authored.document;
        tree = take(UiTree::create(document, actions));
        const StyleSheet sheet;
        styles = take(resolve_styles({tree.get(), &sheet, {}, authored.styles}));
        layout = take(compute_layout({tree.get(), styles, viewport, &text, {}}));
    }
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic(std::optional<NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
    const LayoutBox& box(std::string_view id) const {
        const auto target = node(id);
        return *std::find_if(layout.boxes.begin(), layout.boxes.end(),
                             [&](const LayoutBox& box) { return box.node == target; });
    }
    Point center(std::string_view id) const {
        const auto& rect = box(id).border_box;
        return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    }
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing input semantic entry");
    return *at;
}
const UiNode* search(const UiNode& node, std::string_view id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children)
        if (const auto* found = search(child, id)) return found;
    return nullptr;
}
const UiNode& find(const UiNode& node, std::string_view id) {
    const auto* found = search(node, id);
    check(found != nullptr, "Missing authored node");
    return *found;
}
Property text(const char* value) { return Property{std::string(value)}; }

void composition() {
    Fixture f;
    f.build();
    const auto& name = find(f.document.root, "name");
    check(name.properties.at("edit_role") == text("text_input") && name.properties.at("edit_value") == text("Cube") &&
              name.properties.at("labelled_by") == Property{NodeReference{"name-label"}} &&
              name.events.at("activate") == "name.edit" && name.children.size() == 3 &&
              name.children[0].id == "name-label" && name.children[1].id == "name-value" &&
              name.children[1].properties.at("text") == text("Cube") &&
              name.children[2].properties.at("text") == text("Selection 4:4"),
          "TextInput must lower to a labelled text_input Box with label, value and selection Text");
    const auto& scale = find(f.document.root, "scale");
    check(scale.properties.at("edit_role") == text("number_input") &&
              scale.properties.at("edit_value") == text("1.5") &&
              scale.properties.at("labelled_by") == Property{NodeReference{"scale-label"}} &&
              scale.events.at("activate") == "scale.edit" && scale.children.size() == 4 &&
              scale.children[0].id == "scale-label" && scale.children[1].id == "scale-field" &&
              scale.children[1].children.size() == 1 && scale.children[1].children[0].id == "scale-value" &&
              scale.children[1].children[0].properties.at("text") == text("1.5") &&
              scale.children[2].id == "scale-decrease" &&
              scale.children[2].children[0].properties.at("text") == text("-") &&
              scale.children[3].id == "scale-increase" &&
              scale.children[3].children[0].properties.at("text") == text("+"),
          "NumberInput must lower to a labelled number_input row with field, value and step parts");
    const auto row = f.box("scale").content_box();
    const auto field = f.box("scale-field").border_box;
    const auto decrease = f.box("scale-decrease").border_box;
    const auto increase = f.box("scale-increase").border_box;
    check(decrease.size.width == 20 && increase.size.width == 20 &&
              field.origin.x > f.box("scale-label").border_box.origin.x &&
              decrease.origin.x >= field.origin.x + field.size.width &&
              increase.origin.x >= decrease.origin.x + decrease.size.width &&
              std::abs(increase.origin.x + increase.size.width - (row.origin.x + row.size.width)) < .01f,
          "The draft field grows between the label and the trailing step parts");
    check(validate_sources(f.authored.document, f.authored.sources).empty(), "Generated parts need valid sources");
}

void reactive_values() {
    Fixture f;
    f.build();
    auto draft = take(make_text_edit_state(2, "1.5"));
    draft = take(edit_text(draft, {2, draft.revision, TextEditInsert{"0"}}));
    auto name = take(make_text_edit_state(1, "Cube"));
    name = take(edit_text(name, {1, name.revision, TextCompositionStart{}}));
    name = take(edit_text(name, {1, name.revision, TextCompositionUpdate{name.composition->id, "ご", 3}}));
    f.session.update([&] {
        f.scale.write(draft);
        f.name.write(name);
    });
    f.build();
    check(find(f.document.root, "scale").properties.at("edit_value") == text("1.50") &&
              find(f.document.root, "scale-value").properties.at("text") == text("1.50"),
          "A bound draft must settle into the owner value and its value Text together");
    check(find(f.document.root, "name").properties.at("edit_value") == text("Cubeご") &&
              find(f.document.root, "name-value").properties.at("text") == text("Cubeご"),
          "An open composition presents its preedit as the input value");
}

void semantics() {
    Fixture f;
    f.build();
    const auto tree = take(build_semantic_tree(f.semantic()));
    const auto& name = entry(tree, "name");
    check(name.role == SemanticRole::text_input && name.name == "Name" &&
              name.name_source == NameSource::relationship && name.value == "Cube" && name.focusable &&
              !name.selected && name.actions == std::vector<SemanticAction>{{"activate", "name.edit"}},
          "TextInput must project a labelled text_input with its presented value");
    const auto& scale = entry(tree, "scale");
    check(scale.role == SemanticRole::number_input && scale.name == "Scale" && scale.value == "1.5" &&
              scale.actions == std::vector<SemanticAction>{{"activate", "scale.edit"}},
          "NumberInput must project a labelled number_input with its draft text");
    check(entry(tree, "before").value == std::nullopt, "Only inputs carry a semantic value");
    check(std::none_of(tree.nodes.begin(), tree.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text &&
                                  (node.name == "Name" || node.name == "Cube" || node.name == "1.5" ||
                                   node.name == "+" || node.name.starts_with("Selection"));
                       }),
          "Labels, values, selection and step glyphs are presentational");

    f.session.update([&] { f.disabled.write(true); });
    f.build();
    const auto disabled = take(build_semantic_tree(f.semantic()));
    for (const auto* id : {"name", "scale"}) {
        const auto& control = entry(disabled, id);
        check(!control.enabled && !control.focusable && control.actions.empty() && control.value,
              "A disabled input stays exposed with its value but without actions or focus");
    }
}

void input() {
    Fixture f;
    f.build();
    std::chrono::microseconds time{0};
    const auto dispatch = [&](FocusDispatcher& focus, auto data) {
        time += 1us;
        return take(focus.dispatch(f.hit(), {time, std::move(data)}));
    };
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(dispatch(focus, FocusNext{}).focused == f.node("name"), "Tab must reach the TextInput");
    check(dispatch(focus, FocusNext{}).focused == f.node("scale"), "The NumberInput is one Tab stop");
    check(dispatch(focus, Activate{}).actions ==
              std::vector<ActionRequest>{{"activate", "scale.edit", f.node("scale")}},
          "Activate requests the NumberInput command without a payload");
    for (const auto* part : {"scale-field", "scale-decrease", "scale-increase"}) {
        const auto target = take(hit_test(f.hit(), f.center(part))).target;
        const auto parts = f.tree->children(f.node(part));
        check(target && (*target == f.node(part) || std::find(parts.begin(), parts.end(), *target) != parts.end()),
              "Each NumberInput part is a reachable host hit target");
        PointerDispatcher pointer;
        const auto pressed = take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center(part)}}));
        check(pressed.pointers.size() == 1 && pressed.pointers[0].pressed == f.node("scale"),
              "Pressing any NumberInput part presses the input owner");
    }
}

void numbers() {
    for (const auto& [source, expected] : std::vector<std::pair<std::string_view, double>>{
             {"0", 0}, {"-0", 0}, {"1", 1}, {"1.25", 1.25}, {"4", 4}, {"0.0", 0}, {"3.999", 3.999}}) {
        const auto parsed = take(ui::parse_number(scale_range, source));
        check(parsed == expected && !std::signbit(parsed), "Accepted decimal differs");
    }
    for (const auto* source : {"", "-", "+1", " 1", "1 ", "01", "1.", ".5", "1e1", "1,5", "nan", "inf", "0x1", "--1"})
        check(has(ui::parse_number(scale_range, source).diagnostics, "number_syntax", "/text"),
              "Malformed decimal accepted");
    for (const auto* source : {"4.01", "-0.25", "5", "100000000000000000000000000000000000000000000000000"})
        check(has(ui::parse_number(scale_range, source).diagnostics, "number_out_of_range", "/text"),
              "Out-of-range decimal accepted");
    check(has(ui::parse_number({1, 0, 1}, "0").diagnostics, "number_range", "/range") &&
              has(ui::parse_number({0, 1, 0}, "0").diagnostics, "number_range", "/range") &&
              has(ui::step_number({0, NAN, 1}, 0, 1).diagnostics, "number_range", "/range"),
          "Invalid ranges are rejected");

    check(ui::format_number(0) == "0" && ui::format_number(-0.0) == "0" && ui::format_number(1) == "1" &&
              ui::format_number(1.25) == "1.25" && ui::format_number(-0.5) == "-0.5" &&
              ui::format_number(0.1) == "0.1" && ui::format_number(1e21) == "1000000000000000000000" &&
              ui::format_number(INFINITY).empty(),
          "Canonical decimal text differs");
    for (const double value : {0.1, 1.0 / 3, 123.456, 2.5e-7})
        check(take(ui::parse_number({-1e9, 1e9, 1}, ui::format_number(value))) == value,
              "Canonical text must round-trip its value");

    check(take(ui::step_number(scale_range, 1.5, 1)) == 1.75 && take(ui::step_number(scale_range, 1.5, -2)) == 1 &&
              take(ui::step_number(scale_range, 3.9, 1)) == 4 && take(ui::step_number(scale_range, 0.1, -1)) == 0 &&
              take(ui::step_number(scale_range, 1.3, 1)) == 1.55,
          "Steps add the step from the value and clamp without snapping");
    const ui::NumberRange tenths{0, 1, 0.1};
    check(take(ui::step_number(tenths, 0.2, 1)) == 0.3 &&
              ui::format_number(take(ui::step_number(tenths, 0.7, 1))) == "0.8",
          "Steps drop binary noise beyond the value and step decimals");
    check(!std::signbit(take(ui::step_number({-1, 1, 0.5}, 0.5, -1))), "A step to zero is positive zero");
    check(has(ui::step_number(scale_range, NAN, 1).diagnostics, "number_value", "/text"),
          "Stepping a nonfinite value is rejected");
}

void codecs() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Edit roles and values must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    const auto saved = take(save_semantics(snapshot));
    check(saved.find(R"("role":"number_input")") != std::string::npos &&
              saved.find(R"("value":"1.5")") != std::string::npos && take(load_semantics(saved)) == snapshot,
          "Edit roles and values must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget name{SemanticRole::text_input, "Name"};
    const SemanticTarget scale{SemanticRole::number_input, "Scale"};
    check(take(resolve_target(inspection, name)) == f.node("name") &&
              take(resolve_target(inspection, scale)) == f.node("scale"),
          "Inspection must resolve edit role targets");
    const std::vector<UiInvariant> invariants{{"name", SemanticNameInvariant{name, "Name"}},
                                              {"scale", SemanticNameInvariant{scale, "Scale"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept edit role targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{name}, InputEvent{1us, Activate{}}, ReplaySemanticAction{scale, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain edit role targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "name.edit" &&
                  output.actions[1].action == "scale.edit",
              "Replay must share keyboard and semantic input activation");
        check(!output.semantics.empty() &&
                  std::any_of(output.semantics.back().nodes.begin(), output.semantics.back().nodes.end(),
                              [](const ReplaySemanticNode& node) { return node.value == "1.5"; }),
              "Replay semantic observations carry input values");
    }
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    const auto state = take(make_text_edit_state(1, "1"));
    check(has(lower(ui::TextInput("Name", state, "name.edit")).diagnostics, "text_input_id", "/root/children/0"),
          "TextInput requires a stable ID");
    check(has(lower(ui::NumberInput("Scale", state, "scale.edit")).diagnostics, "number_input_id", "/root/children/0"),
          "NumberInput requires a stable ID");
    check(!lower(ui::NumberInput("Scale", state, "unknown", {.node = {.id = "scale"}})),
          "Unregistered NumberInput command accepted");
    auto bad = state;
    bad.caret = 7;
    check(has(lower(ui::NumberInput("Scale", bad, "scale.edit", {.node = {.id = "scale"}})).diagnostics,
              "invalid_selection", "/root/children/0/children/1/children/0/properties/text"),
          "A malformed draft addresses the generated value Text");
    auto deepest = ui::NumberInput("Bounded", state, "scale.edit", {.node = {.id = "bounded"}});
    for (std::size_t i = 3; i < max_document_depth; ++i) deepest = ui::Column({deepest});
    check(bool(session.lower(deepest, actions)) && !session.lower(ui::Column({deepest}), actions),
          "Generated NumberInput parts must count against depth bounds");

    Fixture f;
    f.build();
    const auto at = [](const char* tail) { return std::string("/root/children/2") + tail; };
    auto malformed = f.document;
    malformed.root.children[2].properties["edit_role"] = std::string("slider");
    check(has(validate(malformed, actions), "edit_role", at("/properties/edit_role")), "Unknown edit roles rejected");
    malformed = f.document;
    malformed.root.children[2].properties.erase("edit_value");
    check(has(validate(malformed, actions), "edit_value", at("/properties/edit_value")),
          "Inputs require a presented value");
    malformed = f.document;
    malformed.root.children[2].properties.erase("edit_role");
    check(has(validate(malformed, actions), "edit_value", at("/properties/edit_value")),
          "Values belong only to inputs");
    malformed = f.document;
    malformed.root.children[2].events.clear();
    check(has(validate(malformed, actions), "edit_action", at("/events/activate")), "Inputs require a command");
    malformed = f.document;
    malformed.root.children[2].properties["toggle_role"] = std::string("switch");
    malformed.root.children[2].properties["selected"] = false;
    check(has(validate(malformed, actions), "edit_role", at("/properties/edit_role")),
          "An input cannot also be a toggle");
    malformed = f.document;
    malformed.root.children[2].children[0].properties["edit_role"] = std::string("text_input");
    check(!validate(malformed, actions).empty(), "Text cannot carry an edit role");

    auto snapshot = take(capture_semantics(f.semantic(), 1));
    auto button = snapshot;
    std::find_if(button.nodes.begin(), button.nodes.end(), [](const auto& node) {
        return node.id == "before";
    })->value = "1";
    check(has(validate(button), "invalid_state", "/nodes/1/value"), "A button cannot carry a value");
    auto missing = snapshot;
    std::find_if(missing.nodes.begin(), missing.nodes.end(), [](const auto& node) {
        return node.id == "scale";
    })->value.reset();
    check(!validate(missing).empty(), "An input requires a value");
}
} // namespace

int main() {
    try {
        composition();
        reactive_values();
        semantics();
        input();
        numbers();
        codecs();
        rejection();
        std::cout
            << "TextInput/NumberInput composition, semantics, input, number, codec and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
