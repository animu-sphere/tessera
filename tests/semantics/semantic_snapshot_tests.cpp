#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/ui/serialization.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using tessera::test::check;
using tessera::test::has;

namespace {
struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper text;
    tessera::LayoutResult layout;
    Fixture() {
        auto document = tessera::load_document(R"({"version":1,"root":{"type":"Box","id":"menu","children":[
          {"type":"Box","id":"start","properties":{"focusable":true,"labelled_by":{"ref":"caption"}},
           "events":{"activate":"start"}},
          {"type":"Box","id":"quit","properties":{"disabled":true},"events":{"activate":"quit"},
           "children":[{"type":"Text","properties":{"text":"終了"}}]},
          {"type":"Text","id":"caption","properties":{"text":"Start スタート"}}]}})", {{"start", "quit"}});
        check(bool(document), "Semantic fixture document rejected");
        auto created = tessera::UiTree::create(*document.value, {{"start", "quit"}});
        check(bool(created), "Semantic fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        styles[4].display = tessera::Display::none;
        auto computed = tessera::compute_layout({tree.get(), styles, {200, 120}, &text});
        check(bool(computed), "Semantic fixture layout rejected");
        layout = std::move(*computed.value);
    }
    tessera::SemanticInput input() const { return {tree.get(), styles, &layout, tree->find("start")}; }
};

tessera::SemanticSnapshot snapshot() {
    Fixture fixture;
    auto captured = tessera::capture_semantics(fixture.input(), std::numeric_limits<std::uint64_t>::max());
    check(captured && captured.diagnostics.empty(), "Semantic capture failed");
    check(captured.value->node_count == 5 && captured.value->nodes.size() == 3 &&
              captured.value->nodes[1].name == "Start スタート" && captured.value->nodes[1].labelled_by == 4 &&
              captured.value->nodes[1].focused && !captured.value->nodes[2].enabled &&
              captured.value->nodes[2].actions.empty(), "Semantic capture lost projection state or hidden relationship");
    return std::move(*captured.value); // Still valid after the live tree expires.
}

void canonical_round_trip_and_rejections() {
    const auto original = snapshot();
    const auto saved = tessera::save_semantics(original);
    check(bool(saved), "Projected semantic snapshot cannot save");
    check(saved.value->find("\"generation\":\"18446744073709551615\"") != std::string::npos &&
              saved.value->find("\"parent\":null") != std::string::npos && saved.value->back() == '\n',
          "Canonical generation, null, or newline differs");
    const auto loaded = tessera::load_semantics(*saved.value);
    check(loaded && *loaded.value == original && tessera::save_semantics(*loaded.value).value == saved.value,
          "Semantic round trip or canonical bytes differ");
    auto changed = *saved.value;
    const auto version = changed.find("\"version\":1");
    changed.replace(version, 11, "\"version\":2");
    const auto rejected = tessera::load_semantics(changed);
    check(has(rejected.diagnostics, "unsupported_version", "/version") && rejected.diagnostics[0].byte_offset,
          "Unsupported semantic version lacks location");
    check(has(tessera::load_semantics(R"({"version":1,"generation":"01","node_count":1,"nodes":[]})").diagnostics,
              "schema_type", "/generation"), "Noncanonical generation accepted");
    check(has(tessera::load_semantics(R"({"version":1,"generation":"18446744073709551616","node_count":1,"nodes":[]})").diagnostics,
              "schema_type", "/generation"), "Overflow generation accepted");
    check(has(tessera::load_semantics(R"({"version":1,"generation":"0","node_count":1,"nodes":[],"extra":true})").diagnostics,
              "unknown_field", "/extra"), "Unknown semantic field accepted");
    changed = *saved.value;
    const auto parent = changed.find("\"parent\":null");
    changed.replace(parent, 13, "\"parent\":4294967295");
    check(has(tessera::load_semantics(changed).diagnostics, "invalid_parent", "/nodes/0/parent"),
          "Serialized runtime parent sentinel accepted");
    check(has(tessera::load_semantics(std::string(tessera::max_serialized_semantic_bytes + 1, ' ')).diagnostics,
              "size_limit", ""), "Semantic byte bound missing");

    auto bad = original;
    bad.nodes[2].node = bad.nodes[1].node;
    bad.nodes[1].parent = 2;
    bad.nodes[2].focusable = true;
    bad.nodes[2].focused = true;
    bad.nodes[2].actions = {{"cancel", "quit"}};
    bad.nodes[1].labelled_by = 99;
    auto errors = tessera::save_semantics(bad).diagnostics;
    check(has(errors, "invalid_identity", "/nodes/2/node") && has(errors, "invalid_parent", "/nodes/1/parent") &&
              has(errors, "invalid_state", "/nodes/2/focused") && has(errors, "invalid_action", "/nodes/2/actions") &&
              has(errors, "unsupported_action", "/nodes/2/actions/0/binding") &&
              has(errors, "invalid_reference", "/nodes/1/labelled_by"), "Semantic state/identity/action validation incomplete");
    bad = original;
    bad.nodes[1].role = tessera::SemanticRole{99};
    bad.nodes[1].name = "\xff";
    bad.nodes[2].id = bad.nodes[1].id;
    errors = tessera::save_semantics(bad).diagnostics;
    check(has(errors, "unknown_value", "/nodes/1/role") && has(errors, "invalid_utf8", "/nodes/1/name") &&
              has(errors, "duplicate_id", "/nodes/2/id"), "Malformed semantic values accepted");
    bad = original;
    bad.node_count = 10001;
    check(has(tessera::validate(bad), "out_of_range", "/node_count"), "Authored node bound missing");
    // An invisible root can produce a forest or no entries.
    check(tessera::validate(tessera::SemanticSnapshot{1, 0, 1, {}}).empty(), "Empty semantic forest rejected");
}

void generation_aware_actions_use_live_eligibility() {
    Fixture fixture;
    const auto invoked = tessera::request_semantic_action(fixture.input(), 8, tessera::SemanticIdentity{8, 1});
    const auto ordinary = tessera::request_semantic_action(fixture.input(), *fixture.tree->find("start"), "activate");
    check(invoked && ordinary && invoked.value == ordinary.value, "Schema identity bypassed ordinary action path");
    check(has(tessera::request_semantic_action(fixture.input(), 9, tessera::SemanticIdentity{8, 1}).diagnostics,
              "stale_target", "/target"), "Stale external generation invoked");
    Fixture reloaded;
    check(has(tessera::request_semantic_action(reloaded.input(), 9, tessera::SemanticIdentity{8, 1}).diagnostics,
              "stale_target", "/target"), "Reused author identity accepted old generation");
    check(has(tessera::request_semantic_action(fixture.input(), 8, tessera::SemanticIdentity{8, 2}).diagnostics,
              "disabled_target", "/target"), "Disabled semantic node invoked");
    check(has(tessera::request_semantic_action(fixture.input(), 8, tessera::SemanticIdentity{8, 4}).diagnostics,
              "hidden_target", "/target"), "Hidden semantic node invoked");
    check(has(tessera::request_semantic_action(fixture.input(), 8, tessera::SemanticIdentity{8, 1}, "cancel").diagnostics,
              "unsupported_action", "/binding"), "Unexposed binding invoked");
}
} // namespace

int main() {
    try {
        canonical_round_trip_and_rejections();
        generation_aware_actions_use_live_eligibility();
        std::cout << "Semantic schema, canonical JSON, state validation and generation-aware actions passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
