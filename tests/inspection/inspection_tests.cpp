#include <tessera/inspection/inspection.hpp>
#include <tessera/input/pointer.hpp>
#include "../check.hpp"
#include <iostream>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

// Preorder: 0 menu, 1 title, 2 start, 3 "Start", 4 options, 5 "Options", 6 quit, 7 "Quit",
// 8 spare (also "Start"), 9 "Start", 10 hint (display none), 11 "Hidden", 12 blank (unnamed button).
constexpr std::string_view menu_json = R"({"version":1,"root":{"type":"Box","id":"menu",
 "properties":{"labelled_by":{"ref":"title"}},"children":[
 {"type":"Text","id":"title","properties":{"text":"Main Menu"}},
 {"type":"Box","id":"start","classes":["item"],"properties":{"focusable":true},
  "events":{"activate":"start-game"},"children":[{"type":"Text","properties":{"text":"Start"}}]},
 {"type":"Box","id":"options","properties":{"disabled":true},"events":{"activate":"open-options"},
  "children":[{"type":"Text","properties":{"text":"Options"}}]},
 {"type":"Box","id":"quit","events":{"activate":"quit-game"},"children":[{"type":"Text","properties":{"text":"Quit"}}]},
 {"type":"Box","id":"spare","events":{"activate":"start-game"},"children":[{"type":"Text","properties":{"text":"Start"}}]},
 {"type":"Box","id":"hint","children":[{"type":"Text","properties":{"text":"Hidden"}}]},
 {"type":"Box","id":"blank","events":{"activate":"quit-game"}}]}})";

const tessera::ValidationContext actions{{"start-game", "open-options", "quit-game"}};

tessera::SourcedDocument load(std::string_view json = menu_json) {
    auto loaded = tessera::load_document_with_sources(json, "menu.json", actions);
    check(static_cast<bool>(loaded), "Fixture document rejected");
    return std::move(*loaded.value);
}

struct Fixture {
    tessera::SourcedDocument sourced = load();
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;

    Fixture() {
        auto created = tessera::UiTree::create(sourced.document, actions);
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        styles[10].display = tessera::Display::none;
        compute();
    }
    void compute() {
        auto result = tessera::compute_layout({tree.get(), styles, {200, 200}, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::NodeHandle node(std::uint32_t index) const { return {tree->root().tree, index}; }
    tessera::InspectionInput input(bool sources = true) const {
        return {tree.get(), styles, &layout, sources ? &sourced.sources : nullptr};
    }
    tessera::SemanticInput semantic_input() const { return {tree.get(), styles, &layout}; }
    tessera::InspectionSnapshot capture(bool sources = true) const {
        auto result = tessera::capture_inspection(input(sources));
        check(static_cast<bool>(result) && result.diagnostics.empty(), "Valid capture rejected or diagnosed");
        return std::move(*result.value);
    }
};

std::string_view spanned(const tessera::SourceSpan& span) {
    return menu_json.substr(span.begin, span.end - span.begin);
}

void capture_joins_one_generation() {
    const Fixture fixture;
    const auto snapshot = fixture.capture();
    const auto& elements = snapshot.elements;
    check(snapshot.generation == fixture.tree->root().tree && elements.size() == 13, "One element per tree node");

    const auto& start = elements[2];
    check(start.id == "start" && start.kind == tessera::NodeKind::box && start.parent == 0 &&
              start.children == std::vector<std::uint32_t>{3} && start.classes == std::vector<std::string>{"item"} &&
              start.events.at("activate") == "start-game",
          "Authored identity, hierarchy, classes, and events");
    const std::vector<tessera::PropertyObservation> box_properties{
        {"disabled", tessera::Property{false}, false},
        {"focusable", tessera::Property{true}, true},
        {"labelled_by", std::nullopt, false}};
    check(start.properties == box_properties, "Box properties follow descriptors with absence values");
    check(elements[1].properties.size() == 4 && elements[1].properties[3].name == "text" &&
              elements[1].properties[3].authored,
          "Text elements also observe the text descriptor");

    const auto& box = fixture.layout.boxes[2];
    check(start.geometry && start.geometry->border_box == box.border_box &&
              start.geometry->content_box == box.content_box() && start.geometry->visible && start.style == fixture.styles[2],
          "Geometry and style come from the captured snapshot");
    check(!elements[10].geometry && !elements[11].geometry && elements[11].parent == 10 &&
              elements[0].children.size() == 7,
          "Display-none elements stay in the runtime hierarchy without geometry");

    check(start.semantic && snapshot.semantics.nodes[*start.semantic].node == fixture.node(2) &&
              snapshot.semantics.nodes[*start.semantic].name == "Start",
          "Element links to its semantic entry");
    check(!elements[3].semantic && !elements[11].semantic && elements[4].semantic,
          "Presentational and display-none elements have no semantic entry; disabled buttons do");

    check(snapshot.sources == tessera::SourceStatus::mapped && snapshot.source_file == "menu.json",
          "Source availability is explicit");
    check(start.source && start.source->pointer == "/root/children/1" &&
              spanned(start.source->span).starts_with(R"({"type":"Box","id":"start")") &&
              spanned(start.source->span).ends_with("}") &&
              spanned(start.source->properties.at("focusable")) == "true",
          "Node and property spans map through the loaded source");
    check(spanned(elements[1].source->properties.at("text")) == R"("Main Menu")" &&
              elements[0].source->pointer == "/root" &&
              spanned(elements[0].source->span).starts_with(R"({"type":"Box","id":"menu")") &&
              elements[0].source->span.end == menu_json.size() - 1,
          "Root and Text spans");

    check(snapshot.diagnostics.size() == 1 && snapshot.diagnostics[0].code == "missing_name" &&
              snapshot.diagnostics[0].severity == tessera::Severity::warning &&
              snapshot.diagnostics[0].path == "/nodes/12" &&
              snapshot.diagnostics[0].byte_offset == elements[12].source->span.begin,
          "Warnings belong to the generation and carry a source offset when mapped");
    check(fixture.capture() == snapshot, "Capture is deterministic for one snapshot");

    const auto unmapped = fixture.capture(false);
    check(unmapped.sources == tessera::SourceStatus::not_supplied && unmapped.source_file.empty() &&
              !unmapped.elements[2].source && !unmapped.diagnostics[0].byte_offset,
          "Without a map, sources are reported unavailable rather than guessed");
    check(unmapped.elements[2].properties == start.properties && unmapped.semantics == snapshot.semantics,
          "Source mapping does not change other observations");
}

void capture_rejects_incoherent_inputs() {
    const Fixture fixture;
    auto input = fixture.input();
    input.styles = std::span(fixture.styles).first(3);
    check(has(tessera::capture_inspection(input).diagnostics, "style_count", "/styles"), "Style count must match the tree");

    const Fixture other;
    input = fixture.input();
    input.layout = &other.layout;
    check(has(tessera::capture_inspection(input).diagnostics, "layout_node", "/layout/boxes/0/node"),
          "Layout from another generation is rejected");

    const auto smaller = load(R"({"version":1,"root":{"type":"Box"}})");
    input = fixture.input();
    input.sources = &smaller.sources;
    check(has(tessera::capture_inspection(input).diagnostics, "source_map_mismatch", "/sources/nodes"),
          "A source map for another document shape is rejected");

    auto altered = fixture.sourced.sources;
    altered.nodes[2].properties.clear();
    input.sources = &altered;
    check(has(tessera::capture_inspection(input).diagnostics, "source_map_mismatch", "/sources/nodes/2"),
          "Property spans must match the node's authored properties");
}

void targets_resolve_exactly_one_element() {
    const Fixture fixture;
    const auto snapshot = fixture.capture();
    const auto resolves = [&](const tessera::InspectionTarget& target, std::uint32_t index) {
        const auto result = tessera::resolve_target(snapshot, target);
        return result && result.diagnostics.empty() && *result.value == fixture.node(index);
    };
    const auto fails = [&](const tessera::InspectionTarget& target, std::string_view code, std::string_view path) {
        const auto result = tessera::resolve_target(snapshot, target);
        return !result && has(result.diagnostics, code, path);
    };
    using tessera::SemanticRole;

    check(resolves(tessera::AuthorIdTarget{"quit"}, 6) && resolves(tessera::AuthorIdTarget{"hint"}, 10),
          "Author IDs resolve, including display-none elements");
    check(fails(tessera::AuthorIdTarget{"missing"}, "target_not_found", "/target"), "Unknown ID");

    check(resolves(tessera::SemanticTarget{SemanticRole::button, "Quit"}, 6) &&
              resolves(tessera::SemanticTarget{SemanticRole::text, "Main Menu"}, 1) &&
              resolves(tessera::SemanticTarget{SemanticRole::button, "Options"}, 4),
          "Role and name resolve semantic entries, including disabled ones");
    const auto ambiguous = tessera::resolve_target(snapshot, tessera::SemanticTarget{SemanticRole::button, "Start"});
    check(!ambiguous && has(ambiguous.diagnostics, "ambiguous_target", "/target") &&
              ambiguous.diagnostics[0].message.find("/nodes/2, /nodes/8") != std::string::npos,
          "Duplicate role/name pairs are ambiguous and list the candidates");
    check(fails(tessera::SemanticTarget{SemanticRole::text, "Hidden"}, "target_not_found", "/target") &&
              fails(tessera::SemanticTarget{SemanticRole::text, "Start"}, "target_not_found", "/target"),
          "Display-none and presentational text have no semantic target");

    check(resolves(tessera::PathTarget{}, 0) && resolves(tessera::PathTarget{{1, 0}}, 3) &&
              resolves(tessera::PathTarget{{5, 0}}, 11),
          "Structural paths follow the runtime hierarchy");
    check(fails(tessera::PathTarget{{1, 1}}, "target_not_found", "/target/children/1") &&
              fails(tessera::PathTarget{{9}}, "target_not_found", "/target/children/0"),
          "Out-of-range path steps are located");

    const auto center = [&](std::uint32_t index) {
        const auto& box = *snapshot.elements[index].geometry;
        return tessera::Point{box.border_box.origin.x + box.border_box.size.width / 2,
                              box.border_box.origin.y + box.border_box.size.height / 2};
    };
    check(resolves(tessera::PointTarget{center(3)}, 3) && resolves(tessera::PointTarget{center(5)}, 5),
          "Points select the topmost element, including disabled content");
    check(resolves(tessera::PointTarget{{199, 199}}, 0) &&
              fails(tessera::PointTarget{{200, 0}}, "target_not_found", "/target") &&
              fails(tessera::PointTarget{{std::numeric_limits<float>::quiet_NaN(), 0}}, "invalid_number",
                    "/target/position/x"),
          "Half-open bounds and finite coordinates");

    check(!tessera::resolve_target(tessera::InspectionSnapshot{}, tessera::PathTarget{}),
          "An empty snapshot resolves nothing");

    // The menu clips its children to the top 50 units; its border area remains below.
    Fixture clipped;
    clipped.styles[0].border = {0, 0, 150, 0};
    clipped.styles[0].overflow = tessera::Overflow::clip;
    clipped.compute();
    const auto clipped_snapshot = clipped.capture();
    const auto& quit = *clipped_snapshot.elements[7].geometry;
    check(!clipped_snapshot.elements[0].geometry->clip && quit.clip == tessera::Rect{{0, 0}, {200, 50}} &&
              quit.border_box.origin.y >= 50, "Geometry must record ancestor clips");
    const tessera::Point hidden{quit.border_box.origin.x + 1, quit.border_box.origin.y + 1};
    const auto beneath = tessera::resolve_target(clipped_snapshot, tessera::PointTarget{hidden});
    check(beneath && *beneath.value == clipped.node(0), "Points must not select clipped-out elements");
}

void actions_use_ordinary_eligibility_and_reject_stale_generations() {
    const Fixture fixture;
    const auto snapshot = fixture.capture();
    const auto quit = *tessera::resolve_target(snapshot, tessera::AuthorIdTarget{"quit"}).value;
    const auto requested = tessera::request_semantic_action(fixture.semantic_input(), quit, "activate");
    check(requested && requested.value->action == "quit-game" && requested.value->target == quit,
          "A resolved target invokes through semantic actions");

    tessera::PointerDispatcher pointer;
    const tessera::HitTestInput hit{fixture.tree.get(), fixture.styles, &fixture.layout};
    const auto position = snapshot.elements[7].geometry->border_box.origin;
    pointer.dispatch(hit, {0us, tessera::PointerDown{{1}, position}});
    const auto clicked = pointer.dispatch(hit, {1us, tessera::PointerUp{{1}, position}});
    check(clicked && clicked.value->actions.size() == 1 && clicked.value->actions[0] == *requested.value,
          "Semantic invocation of a resolved target equals a pointer click");

    const auto options = *tessera::resolve_target(snapshot, tessera::AuthorIdTarget{"options"}).value;
    check(has(tessera::request_semantic_action(fixture.semantic_input(), options, "activate").diagnostics,
              "disabled_target", "/target"),
          "Resolution does not bypass disabled eligibility");

    // Reload: the same document produces a new generation that reuses every author ID.
    const Fixture reloaded;
    const auto stale = tessera::request_semantic_action(reloaded.semantic_input(), quit, "activate");
    check(!stale && has(stale.diagnostics, "stale_target", "/target"),
          "A handle from an earlier generation fails after reload despite a reused author ID");
    const auto current = reloaded.capture();
    const auto fresh = *tessera::resolve_target(current, tessera::AuthorIdTarget{"quit"}).value;
    check(current.generation != snapshot.generation && fresh.tree == current.generation && fresh.index == quit.index &&
              tessera::request_semantic_action(reloaded.semantic_input(), fresh, "activate"),
          "Resolving again in the new generation succeeds");
}

void sourced_loading_matches_plain_loading() {
    const std::string_view invalid[] = {
        R"({"version":1,"root":{"type":"Box","children":[{"type":"Text"}]}})",
        R"({"version":1,"root":{"type":"Box","id":"a","children":[{"type":"Box","id":"a"}]}})",
        R"({"version":1,"root":{"type":"Box",})",
        R"({"version":2,"root":{"type":"Box"}})",
    };
    for (const auto json : invalid) {
        const auto plain = tessera::load_document(json);
        const auto sourced = tessera::load_document_with_sources(json, "x.json");
        check(!plain && !sourced && !plain.diagnostics.empty() && plain.diagnostics == sourced.diagnostics,
              "Sourced loading rejects with the same diagnostics and offsets");
    }
    const auto sourced = load();
    check(sourced.document == *tessera::load_document(menu_json, actions).value && sourced.sources.nodes.size() == 13,
          "Sourced loading yields the same document plus one entry per node");
    check(sourced.sources.nodes[12].pointer == "/root/children/6" && sourced.sources.nodes[12].properties.empty(),
          "Nodes without properties map no property spans");
}

} // namespace

int main() {
    try {
        capture_joins_one_generation();
        capture_rejects_incoherent_inputs();
        targets_resolve_exactly_one_element();
        actions_use_ordinary_eligibility_and_reject_stale_generations();
        sourced_loading_matches_plain_loading();
        std::cout << "inspection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
