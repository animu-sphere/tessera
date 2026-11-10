#include <tessera/ui/authoring.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/inspection/invariants.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/text/text_caret.hpp>
#include "../check.hpp"
#include <algorithm>
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
const ValidationContext actions{{"before", "notes.edit", "after"}};
struct Fixture {
    ui::Session session;
    Signal<TextEditState> notes =
        session.state("notes", take(make_text_edit_state(1, "First line\nSecond", TextLines::multiple)));
    Signal<bool> disabled = session.state("disabled", false);
    ui::Presentation authored;
    UiDocument document;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    PlaceholderTextShaper text;
    LayoutResult layout;
    Size viewport{320, 240};
    ui::Element form() const {
        return ui::Column(
            {ui::Button("Before", "before", {.node = {.id = "before"}}),
             ui::TextEditor("Notes", notes, "notes.edit",
                            {.node = {.id = "notes", .style = {.width = Dimension::points(60)}}, .disabled = disabled}),
             ui::Button("After", "after", {.node = {.id = "after"}})},
            {.id = "root", .style = {.align = Align::start, .text = {.size = 20.f}}});
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
};
const SemanticNode& entry(const SemanticTree& tree, std::string_view id) {
    const auto at =
        std::find_if(tree.nodes.begin(), tree.nodes.end(), [&](const SemanticNode& node) { return node.id == id; });
    check(at != tree.nodes.end(), "Missing editor semantic entry");
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
    const auto& notes = find(f.document.root, "notes");
    check(notes.properties.at("edit_role") == text("text_editor") &&
              notes.properties.at("edit_value") == text("First line\nSecond") &&
              notes.properties.at("labelled_by") == Property{NodeReference{"notes-label"}} &&
              notes.events.at("activate") == "notes.edit" && notes.children.size() == 3 &&
              notes.children[0].id == "notes-label" && notes.children[1].id == "notes-value" &&
              notes.children[1].properties.at("text") == text("First line\nSecond") &&
              notes.children[2].properties.at("text") == text("Selection 17:17"),
          "TextEditor must lower to a labelled text_editor Box with label, value and selection Text");
    check(validate_sources(f.authored.document, f.authored.sources).empty(), "Generated parts need valid sources");

    // Each LF starts a line and the 60-unit field wraps "First line" (10 cells of 10 units) into two rows.
    const auto value = f.box("notes-value").content_box();
    check(value.size.width == 60 && value.size.height == 3 * 25, "The value Text wraps to the field width");
    const auto carets = take(layout_text_carets(f.text, "First line\nSecond",
                                                f.styles[f.node("notes-value").index].text, {value.size.width}));
    check(carets.lines.size() == 3 && !carets.lines[0].hard && carets.lines[1].hard && carets.lines[2].begin == 11 &&
              carets.caret(17).origin == Point{60, 50},
          "Caret geometry follows the presented rows");

    auto next = take(make_text_edit_state(1, "a\n", TextLines::multiple));
    next = take(edit_text(next, {1, next.revision, TextCompositionStart{}}));
    next = take(edit_text(next, {1, next.revision, TextCompositionUpdate{next.composition->id, "に\nほ", 4}}));
    f.session.update([&] { f.notes.write(next); });
    f.build();
    check(find(f.document.root, "notes").properties.at("edit_value") == text("a\nに\nほ") &&
              find(f.document.root, "notes-value").properties.at("text") == text("a\nに\nほ") &&
              find(f.document.root, "notes").children[2].properties.at("text") == text("Composition 2:9 caret 6"),
          "An open composition presents its multiple-line preedit");
}

void semantics() {
    Fixture f;
    f.build();
    const auto tree = take(build_semantic_tree(f.semantic()));
    const auto& notes = entry(tree, "notes");
    check(notes.role == SemanticRole::text_editor && notes.name == "Notes" &&
              notes.name_source == NameSource::relationship && notes.value == "First line\nSecond" && notes.focusable &&
              !notes.selected && !notes.expanded &&
              notes.actions == std::vector<SemanticAction>{{"activate", "notes.edit"}},
          "TextEditor must project a labelled text_editor with its presented value");
    check(std::none_of(tree.nodes.begin(), tree.nodes.end(),
                       [](const SemanticNode& node) {
                           return node.role == SemanticRole::text &&
                                  (node.name == "Notes" || node.name.starts_with("First") ||
                                   node.name.starts_with("Selection"));
                       }),
          "Label, value and selection Text are presentational");
    std::chrono::microseconds time{0};
    FocusDispatcher focus;
    (void)take(focus.focus(f.hit(), f.node("before")));
    check(take(focus.dispatch(f.hit(), {++time, FocusNext{}})).focused == f.node("notes") &&
              take(focus.dispatch(f.hit(), {++time, FocusNext{}})).focused == f.node("after"),
          "The editor is one Tab stop");

    f.session.update([&] { f.disabled.write(true); });
    f.build();
    const auto disabled_tree = take(build_semantic_tree(f.semantic()));
    const auto& disabled = entry(disabled_tree, "notes");
    check(!disabled.enabled && !disabled.focusable && disabled.actions.empty() && disabled.value,
          "A disabled editor stays exposed with its value but without actions or focus");
}

void codecs() {
    Fixture f;
    f.build();
    const auto saved_document = take(save_document(f.document, actions));
    check(saved_document.find(R"("edit_role":"text_editor")") != std::string::npos &&
              take(load_document(saved_document, actions)) == f.document,
          "The editor role and multiple-line value must survive UI JSON");
    const auto snapshot = take(capture_semantics(f.semantic(), 3));
    const auto saved = take(save_semantics(snapshot));
    check(saved.find(R"("role":"text_editor")") != std::string::npos &&
              saved.find(R"("value":"First line\u000aSecond")") != std::string::npos &&
              take(load_semantics(saved)) == snapshot,
          "The editor role and value must round trip in Semantic JSON");
    const auto inspection = take(capture_inspection({f.tree.get(), f.styles, &f.layout}));
    const SemanticTarget notes{SemanticRole::text_editor, "Notes"};
    check(take(resolve_target(inspection, notes)) == f.node("notes"), "Inspection must resolve editor targets");
    const std::vector<UiInvariant> invariants{{"notes", SemanticNameInvariant{notes, "Notes"}}};
    check(take(evaluate_invariants({&inspection, invariants, f.viewport})).passed(),
          "Invariant schema must accept editor targets");
    for (const auto version : {replay_version, replay_host_version}) {
        ReplayRecording recording;
        recording.version = version;
        recording.context = actions;
        recording.viewport = f.viewport;
        recording.document = f.document;
        recording.styles = f.styles;
        recording.steps = {ReplayFocus{notes}, InputEvent{1us, Activate{}}, ReplaySemanticAction{notes, "activate"}};
        const auto restored = take(load_replay(take(save_replay(recording))));
        check(restored == recording, "Replay codecs must retain editor targets");
        const auto output = take(play_replay(restored, f.text));
        check(output.actions.size() == 2 && output.actions[0].action == "notes.edit" &&
                  output.actions[1].action == "notes.edit",
              "Replay must share keyboard and semantic editor activation");
        check(!output.semantics.empty() &&
                  std::any_of(output.semantics.back().nodes.begin(), output.semantics.back().nodes.end(),
                              [](const ReplaySemanticNode& node) {
                                  return node.role == SemanticRole::text_editor && node.value == "First line\nSecond";
                              }),
              "Replay semantic observations carry the editor value");
    }
}

void rejection() {
    ui::Session session;
    const auto lower = [&](ui::Element element) { return session.lower(ui::Column({std::move(element)}), actions); };
    const auto multiple = take(make_text_edit_state(1, "a\nb", TextLines::multiple));
    const auto single = take(make_text_edit_state(2, "a"));
    check(has(lower(ui::TextEditor("Notes", multiple, "notes.edit")).diagnostics, "text_editor_id", "/root/children/0"),
          "TextEditor requires a stable ID");
    check(has(lower(ui::TextEditor("Notes", single, "notes.edit", {.node = {.id = "notes"}})).diagnostics, "edit_lines",
              "/root/children/0/children/1/properties/text"),
          "TextEditor rejects a single-line state at its value Text");
    check(has(lower(ui::TextInput("Name", multiple, "notes.edit", {.node = {.id = "name"}})).diagnostics, "edit_lines",
              "/root/children/0/children/1/properties/text") &&
              has(lower(ui::NumberInput("Scale", multiple, "notes.edit", {.node = {.id = "scale"}})).diagnostics,
                  "edit_lines", "/root/children/0/children/1/children/0/properties/text"),
          "Single-line inputs reject a multiple-line state");
    auto bad = multiple;
    bad.text = "a\rb";
    check(has(lower(ui::TextEditor("Notes", bad, "notes.edit", {.node = {.id = "notes"}})).diagnostics,
              "carriage_return", "/root/children/0/children/1/properties/text"),
          "A malformed observation addresses the generated value Text");

    Fixture f;
    f.build();
    auto malformed = f.document;
    malformed.root.children[1].properties.erase("edit_value");
    check(has(validate(malformed, actions), "edit_value", "/root/children/1/properties/edit_value"),
          "Editors require a presented value");
    malformed = f.document;
    malformed.root.children[1].events.clear();
    check(has(validate(malformed, actions), "edit_action", "/root/children/1/events/activate"),
          "Editors require a command");
    auto snapshot = take(capture_semantics(f.semantic(), 1));
    std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [](const auto& node) {
        return node.id == "notes";
    })->value.reset();
    check(!validate(snapshot).empty(), "A semantic editor requires a value");
}
} // namespace

int main() {
    try {
        composition();
        semantics();
        codecs();
        rejection();
        std::cout << "TextEditor composition, wrapping, semantics, codec and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
