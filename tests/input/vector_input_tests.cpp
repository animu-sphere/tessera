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
const ValidationContext actions{{"before", "after", "size.w", "size.h", "position.x", "position.y", "position.z",
                                 "color.r", "color.g", "color.b", "color.a"}};
TextEditState draft(std::uint64_t owner, std::string text) {
    return take(make_text_edit_state(owner, std::move(text)));
}
// Two, three and four components between two buttons.
struct Fixture {
    ui::Session session;
    Signal<TextEditState> y = session.state("y", draft(5, "2.5"));
    Signal<bool> disabled = session.state("disabled", false);
    ui::Presentation authored;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{320, 240};
    ui::Element form() const {
        const auto width = Dimension::points(240);
        return ui::Column(
            {ui::Button("Before", "before", {.node = {.id = "before"}}),
             ui::VectorInput("Size", {{"w", "W", draft(1, "64"), "size.w"}, {"h", "H", draft(2, "32"), "size.h"}},
                             {.node = {.id = "size", .style = {.width = width}}}),
             ui::VectorInput("Position",
                             {{"x", "X", draft(4, "0"), "position.x"},
                              {"y", "Y", y, "position.y"},
                              {"z", "Z", draft(6, "-1"), "position.z"}},
                             {.node = {.id = "position", .style = {.width = width}}, .disabled = disabled}),
             ui::VectorInput("Color",
                             {{"r", "R", draft(7, "1"), "color.r"},
                              {"g", "G", draft(8, "0.5"), "color.g"},
                              {"b", "B", draft(9, "0"), "color.b"},
                              {"a", "A", draft(10, "1"), "color.a"}},
                             {.node = {.id = "color", .style = {.width = width}}}),
             ui::Button("After", "after", {.node = {.id = "after"}})},
            {.id = "root", .style = {.align = Align::start, .gap = 4.f}});
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
    check(at != tree.nodes.end(), "Missing vector semantic entry");
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
    const auto& position = find(f.document.root, "position");
    check(position.properties.at("collection_role") == text("group") &&
              position.properties.at("labelled_by") == Property{NodeReference{"position-label"}} &&
              position.events.empty() && position.children.size() == 2 && position.children[0].id == "position-label" &&
              position.children[0].properties.at("text") == text("Position") &&
              position.children[1].id == "position-fields" && position.children[1].children.size() == 3,
          "VectorInput must lower to a labelled group with a label and a fields row");
    const auto& y = position.children[1].children[1];
    check(y.id == "position-y" && y.properties.at("edit_role") == text("number_input") &&
              y.properties.at("edit_value") == text("2.5") &&
              y.properties.at("labelled_by") == Property{NodeReference{"position-y-label"}} &&
              y.events.at("activate") == "position.y" && y.children.size() == 2 &&
              y.children[0].id == "position-y-label" && y.children[0].properties.at("text") == text("Y") &&
              y.children[1].id == "position-y-field" && y.children[1].children.size() == 1 &&
              y.children[1].children[0].id == "position-y-value" &&
              y.children[1].children[0].properties.at("text") == text("2.5"),
          "Each component must lower to a labelled number_input with its own field, draft and command");
    check(find(f.document.root, "size-fields").children.size() == 2 &&
              find(f.document.root, "color-fields").children.size() == 4 &&
              find(f.document.root, "color-a").events.at("activate") == "color.a",
          "Two and four components must lower like three");

    for (const auto& [vector, keys] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"size", {"w", "h"}}, {"position", {"x", "y", "z"}}, {"color", {"r", "g", "b", "a"}}}) {
        const auto fields = f.box(vector + "-fields").content_box();
        const float width = f.box(vector + "-" + keys.front()).border_box.size.width;
        float x = fields.origin.x;
        for (const auto& key : keys) {
            const auto component = f.box(vector + "-" + key).border_box;
            check(std::abs(component.size.width - width) < 1e-3f && std::abs(component.origin.x - x) < 1e-3f,
                  "Components must share the fields row equally in declared order");
            x = component.origin.x + component.size.width + 4;
        }
        check(std::abs(x - 4 - (fields.origin.x + fields.size.width)) < 1e-3f &&
                  f.box(vector + "-fields").border_box.origin.x > f.box(vector + "-label").border_box.origin.x,
              "The fields row must follow the label and end with the last component");
    }
    check(f.box("position-y-field").clip == std::nullopt && f.box("position-y-value").clip.has_value(),
          "Each draft field clips its value Text");
    check(validate_sources(f.authored.document, f.authored.sources).empty(), "Generated parts need valid sources");
}

void semantics() {
    Fixture f;
    f.build();
    const auto tree = take(build_semantic_tree(f.semantic()));
    const auto& group = entry(tree, "position");
    check(group.role == SemanticRole::group && group.name == "Position" &&
              group.name_source == NameSource::relationship && !group.focusable && group.actions.empty() &&
              !group.value && !group.selected,
          "VectorInput must project a named group without its own actions");
    const auto group_index = static_cast<std::uint32_t>(&group - tree.nodes.data());
    for (const auto& [id, name, value, action] :
         std::vector<std::tuple<const char*, const char*, const char*, const char*>>{
             {"position-x", "X", "0", "position.x"},
             {"position-y", "Y", "2.5", "position.y"},
             {"position-z", "Z", "-1", "position.z"}}) {
        const auto& component = entry(tree, id);
        check(component.role == SemanticRole::number_input && component.name == name && component.value == value &&
                  component.focusable && component.actions == std::vector<SemanticAction>{{"activate", action}} &&
                  component.parent == group_index,
              "Each component must project a labelled number_input inside its group");
    }
    check(entry(tree, "color").role == SemanticRole::group && entry(tree, "color-a").name == "A",
          "Four-component vectors use the same projection");
    check(std::none_of(tree.nodes.begin(), tree.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text &&
                                  (node.name == "X" || node.name == "2.5" || node.name == "-1");
                       }),
          "Component labels and drafts are presentational");

    f.session.update([&] { f.disabled.write(true); });
    f.build();
    const auto disabled = take(build_semantic_tree(f.semantic()));
    for (const auto* id : {"position-x", "position-y", "position-z"}) {
        const auto& component = entry(disabled, id);
        check(!component.enabled && !component.focusable && component.actions.empty() && component.value,
              "A disabled VectorInput disables every component but keeps its values");
    }
    check(entry(disabled, "size-w").focusable, "Disabling one vector leaves the others enabled");

    auto draft_y = draft(5, "2.5");
    draft_y = take(edit_text(draft_y, {5, draft_y.revision, TextEditInsert{"0"}}));
    f.session.update([&] {
        f.disabled.write(false);
        f.y.write(draft_y);
    });
    f.build();
    check(find(f.document.root, "position-y").properties.at("edit_value") == text("2.50") &&
              entry(take(build_semantic_tree(f.semantic())), "position-y").value == "2.50",
          "A bound component draft must settle into its value");
}

void input() {
    Fixture f;
    f.build();
    std::chrono::microseconds time{0};
    FocusDispatcher focus;
    const auto dispatch = [&](auto data) {
        time += 1us;
        return take(focus.dispatch(f.hit(), {time, std::move(data)}));
    };
    (void)take(focus.focus(f.hit(), f.node("before")));
    for (const auto* id : {"size-w", "size-h", "position-x", "position-y", "position-z", "color-r", "color-g",
                           "color-b", "color-a", "after"})
        check(dispatch(FocusNext{}).focused == f.node(id), "Each component must be its own Tab stop in order");
    for (const auto* id : {"color-a", "color-b"})
        check(dispatch(FocusPrevious{}).focused == f.node(id), "Shift+Tab must return through the components");
    (void)take(focus.focus(f.hit(), f.node("position-y")));
    check(dispatch(Activate{}).actions == std::vector<ActionRequest>{{"activate", "position.y", f.node("position-y")}},
          "Activate requests the focused component's command without a payload");
    for (const auto* part : {"position-y-field", "position-y-label"}) {
        PointerDispatcher pointer;
        const auto pressed = take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center(part)}}));
        check(pressed.pointers.size() == 1 && pressed.pointers[0].pressed == f.node("position-y"),
              "Pressing any component part presses that component");
    }
    PointerDispatcher pointer;
    const auto label = take(pointer.dispatch(f.hit(), {1us, PointerDown{{1}, f.center("position-label")}}));
    check(std::none_of(label.pointers.begin(), label.pointers.end(),
                       [](const auto& state) { return state.pressed.has_value(); }),
          "The group label is not a press target");
}

void codecs() {
    Fixture f;
    f.build();
    check(take(load_document(take(save_document(f.document, actions)), actions)) == f.document,
          "Group roles and component values must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    const auto saved = take(save_semantics(snapshot));
    check(saved.find(R"("role":"group")") != std::string::npos && take(load_semantics(saved)) == snapshot,
          "The group role must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget position{SemanticRole::group, "Position"};
    const SemanticTarget z{SemanticRole::number_input, "Z"};
    check(take(resolve_target(inspection, position)) == f.node("position") &&
              take(resolve_target(inspection, z)) == f.node("position-z"),
          "Inspection must resolve group and component targets");
    const std::vector<UiInvariant> invariants{{"position", SemanticNameInvariant{position, "Position"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept group targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{SemanticTarget{SemanticRole::number_input, "Y"}}, InputEvent{1us, FocusNext{}},
                           InputEvent{2us, Activate{}}, ReplaySemanticAction{z, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain component targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "position.z" &&
                  output.actions[1].action == "position.z",
              "Replay must move between components and share semantic activation");
        check(!output.semantics.empty() &&
                  std::any_of(output.semantics.back().nodes.begin(), output.semantics.back().nodes.end(),
                              [](const ReplaySemanticNode& node) {
                                  return node.role == SemanticRole::group && node.name == "Position";
                              }),
              "Replay semantic observations carry the group");
    }
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    const auto x = draft(1, "0");
    const ui::VectorComponent cx{"x", "X", x, "position.x"};
    const ui::VectorComponent cy{"y", "Y", x, "position.y"};
    check(has(lower(ui::VectorInput("Position", {cx, cy})).diagnostics, "vector_input_id", "/root/children/0"),
          "VectorInput requires a stable ID");
    check(has(lower(ui::VectorInput("Position", {cx}, {.node = {.id = "p"}})).diagnostics, "vector_input_components",
              "/root/children/0"),
          "One component is not a vector");
    check(has(lower(ui::VectorInput("Position", {cx, cy, cx, cy, cx}, {.node = {.id = "p"}})).diagnostics,
              "vector_input_components", "/root/children/0"),
          "More than four components are rejected");
    check(has(lower(ui::VectorInput("Position", {cx, cx}, {.node = {.id = "p"}})).diagnostics, "vector_input_keys",
              "/root/children/0"),
          "Duplicate component keys are rejected");
    check(has(lower(ui::VectorInput("Position", {cx, {"", "Y", x, "position.y"}}, {.node = {.id = "p"}})).diagnostics,
              "vector_input_keys", "/root/children/0"),
          "Empty component keys are rejected");
    check(!lower(ui::VectorInput("Position", {cx, {"y", "Y", x, "unknown"}}, {.node = {.id = "p"}})),
          "Unregistered component commands are rejected");
    auto bad = x;
    bad.caret = 7;
    check(
        has(lower(ui::VectorInput("Position", {cx, {"y", "Y", bad, "position.y"}}, {.node = {.id = "p"}})).diagnostics,
            "invalid_selection", "/root/children/0/children/1/children/1/children/1/children/0/properties/text"),
        "A malformed draft addresses its component's value Text");

    Fixture f;
    f.build();
    auto malformed = f.document;
    malformed.root.children[2].events["activate"] = "position.x";
    check(has(validate(malformed, actions), "collection_action", "/root/children/2/events/activate"),
          "A group carries no action of its own");
    malformed = f.document;
    malformed.root.children[2].properties["collection_role"] = std::string("cluster");
    check(has(validate(malformed, actions), "collection_role", "/root/children/2/properties/collection_role"),
          "Unknown collection roles are rejected");

    auto unnamed = f.document;
    unnamed.root.children[2].properties.erase("labelled_by");
    auto tree = take(UiTree::create(unnamed, actions));
    const auto layout = take(compute_layout({tree.get(), f.styles, f.viewport, &f.text, {}}));
    const auto built = build_semantic_tree({tree.get(), f.styles, &layout});
    check(bool(built) &&
              std::any_of(built.diagnostics.begin(), built.diagnostics.end(),
                          [&](const Diagnostic& diagnostic) {
                              return diagnostic.code == "missing_name" && diagnostic.severity == Severity::warning &&
                                     diagnostic.path == "/nodes/" + std::to_string(tree->find("position")->index);
                          }),
          "An unnamed group warns");

    auto snapshot = take(capture_semantics(f.semantic(), 1));
    auto selected = snapshot;
    std::find_if(selected.nodes.begin(), selected.nodes.end(), [](const auto& node) {
        return node.id == "position";
    })->selected = true;
    check(!validate(selected).empty(), "A group has no selection state");
}
} // namespace

int main() {
    try {
        composition();
        semantics();
        input();
        codecs();
        rejection();
        std::cout << "VectorInput composition, semantics, input, codec and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
