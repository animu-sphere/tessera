#include <tessera/inspection/invariants.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
constexpr std::string_view json = R"({"version":1,"root":{"type":"Box","id":"panel","children":[
 {"type":"Box","id":"save","properties":{"focusable":true},"events":{"activate":"save"},
  "children":[{"type":"Text","id":"caption","properties":{"text":"保存"}}]},
 {"type":"Box","id":"spare","properties":{"focusable":true},"events":{"activate":"save"},
  "children":[{"type":"Text","properties":{"text":"保存"}}]},
 {"type":"Box","id":"disabled","properties":{"disabled":true},"children":[
  {"type":"Box","id":"blocked","properties":{"focusable":true},"events":{"activate":"save"},
   "children":[{"type":"Text","properties":{"text":"Blocked"}}]}]},
 {"type":"Text","id":"hidden","properties":{"text":"Hidden"}},
 {"type":"Text","id":"none","properties":{"text":"None"}},
 {"type":"Box","id":"empty"}
]}})";
struct Fixture {
    SourcedDocument loaded;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot snapshot;
    Fixture() {
        auto source = load_document_with_sources(json, "tool-panel.json", {{"save"}});
        check(static_cast<bool>(source), "Source fixture loads"); loaded = std::move(*source.value);
        auto created = UiTree::create(loaded.document, {{"save"}});
        check(static_cast<bool>(created), "Fixture tree validates"); tree = std::move(*created.value);
        styles.resize(tree->size());
        for (const auto id : {"save", "spare", "blocked"}) styles[tree->find(id)->index].height = Dimension::points(24);
        styles[tree->find("hidden")->index].visibility = Visibility::hidden;
        styles[tree->find("none")->index].display = Display::none;
        PlaceholderTextShaper text;
        auto geometry = compute_layout({tree.get(), styles, {240, 160}, &text});
        check(static_cast<bool>(geometry), "Fixture geometry computes"); layout = std::move(*geometry.value);
        auto captured = capture_inspection({tree.get(), styles, &layout, &loaded.sources});
        check(static_cast<bool>(captured), "Fixture inspection captures"); snapshot = std::move(*captured.value);
    }
    NodeHandle node(std::string_view id) const { return *tree->find(id); }
};
AuthorIdTarget id(std::string value) { return {std::move(value)}; }
InvariantReport run(const InspectionSnapshot& snapshot, const std::vector<UiInvariant>& declarations,
                    std::optional<Size> viewport = {}) {
    auto report = evaluate_invariants({&snapshot, declarations, viewport});
    if (!report) {
        for (const auto& d : report.diagnostics) std::cerr << d.code << ' ' << d.path << ' ' << d.message << '\n';
    }
    check(static_cast<bool>(report), "Valid evaluation produces a report, including failed predicates");
    return std::move(*report.value);
}
void semantic_checks_and_owned_report() {
    InvariantReport owned;
    {
        Fixture f;
        const std::vector<UiInvariant> declarations{
            {"visible", VisibleInvariant{id("save")}},
            {"enabled", EnabledInvariant{id("save")}},
            {"focusable", FocusableInvariant{id("save")}},
            {"name", SemanticNameInvariant{id("save"), "保存"}},
            {"disabled-descendant", EnabledInvariant{id("blocked"), false}},
            {"disabled-focus", FocusableInvariant{id("blocked"), false}},
            {"hidden", VisibleInvariant{id("hidden"), false}},
            {"display-none", VisibleInvariant{id("none"), false}},
            {"viewport", InsideViewportInvariant{id("save")}},
            {"separate", NoOverlapInvariant{id("save"), id("spare")}}
        };
        owned = run(f.snapshot, declarations, Size{240, 160});
        check(owned.passed() && owned.generation == f.snapshot.generation && owned.results.size() == declarations.size(),
              "State, inherited semantic eligibility, name, geometry and hidden targets pass");
        check(owned == run(f.snapshot, declarations, Size{240, 160}), "Repeated evaluation is deterministic");
        check(owned.results[3].observations[0].semantic_name == "保存" &&
            owned.results[0].observations[0].target == f.node("save") && owned.source_file == "tool-panel.json",
            "Reports own actual values, generation identities, and source file");
        auto failures = run(f.snapshot, {
            {"wrong-state", EnabledInvariant{id("blocked")}},
            {"wrong-name", SemanticNameInvariant{id("save"), "Save"}},
            {"wrong-hidden", VisibleInvariant{id("none")}},
            {"missing-semantic", SemanticNameInvariant{id("caption"), "保存"}},
            {"missing-geometry", InsideViewportInvariant{id("none")}},
            {"after-failure", VisibleInvariant{id("save")}}
        }, Size{240, 160});
        check(!failures.passed() && failures.results[0].status == InvariantStatus::failed &&
            has(failures.results[0].diagnostics, "invariant_failed", "/invariants/0") &&
            failures.results[0].observations[0].state == false &&
            failures.results[1].observations[0].semantic_name == "保存", "Failures retain actual semantic values");
        check(failures.results[0].diagnostics[0].byte_offset == f.snapshot.elements[f.node("blocked").index].source->span.begin,
              "Located failures retain available source offsets");
        check(failures.results[3].status == InvariantStatus::error &&
            has(failures.results[3].diagnostics, "invariant_unavailable", "/invariants/3/target") &&
            failures.results[4].status == InvariantStatus::error && failures.results[5].status == InvariantStatus::passed,
            "Unavailable observations are errors; later predicates continue");
        auto unmapped = capture_inspection({f.tree.get(), f.styles, &f.layout});
        check(static_cast<bool>(unmapped), "Unmapped fixture captures");
        auto report = run(*unmapped.value, {{"name", SemanticNameInvariant{id("save"), "Wrong"}}});
        check(report.source_file.empty() && !report.results[0].observations[0].source &&
            !report.results[0].diagnostics[0].byte_offset, "Unavailable source locations are never guessed");
    }
    check(owned.passed() && owned.results[3].observations[0].semantic_name == "保存" &&
        owned.results[0].observations[0].source->pointer == "/root/children/0" &&
        owned.results[3].declaration.name == "name", "Report survives destruction of snapshot, declarations, and tree");
}
void resolution_and_generations() {
    Fixture f;
    const auto before = f.snapshot;
    auto report = run(f.snapshot, {
        {"missing", VisibleInvariant{id("absent"), false}},
        {"ambiguous", SemanticNameInvariant{SemanticTarget{SemanticRole::button, "保存"}, "保存"}},
        {"scoped", SemanticNameInvariant{SemanticTarget{SemanticRole::button, "保存"}, "保存"}, f.node("save")},
        {"path", VisibleInvariant{PathTarget{{0}}}},
        {"scoped-path", VisibleInvariant{PathTarget{}}, f.node("save")},
        {"bad-path", VisibleInvariant{PathTarget{{99}}}},
        {"missing-other", NoOverlapInvariant{id("save"), id("absent")}},
        {"same", NoOverlapInvariant{id("save"), id("save")}},
        {"point", VisibleInvariant{PointTarget{{1, 1}}}}
    });
    check(has(report.results[0].diagnostics, "target_not_found", "/invariants/0/target") &&
        report.results[0].status == InvariantStatus::error &&
        has(report.results[1].diagnostics, "ambiguous_target", "/invariants/1/target"),
        "Missing negated target and ambiguity never pass");
    check(report.results[2].status == InvariantStatus::passed && report.results[3].status == InvariantStatus::passed &&
        report.results[4].status == InvariantStatus::passed && report.results[8].status == InvariantStatus::passed,
        "Scoped semantics, absolute/scoped paths and ordinary point resolution are shared");
    check(has(report.results[5].diagnostics, "target_not_found", "/invariants/5/target/children/0") &&
        has(report.results[6].diagnostics, "target_not_found", "/invariants/6/other") &&
        report.results[6].observations.size() == 1 &&
        has(report.results[7].diagnostics, "invariant_same_target", "/invariants/7/other"),
        "Resolution failures preserve their codes and target fields");
    Fixture replacement;
    report = run(replacement.snapshot, {{"stale", VisibleInvariant{id("save")}, f.node("save")}});
    check(has(report.results[0].diagnostics, "stale_target", "/invariants/0/scope") &&
        report.generation != f.snapshot.generation, "Reused author IDs do not revive a scope from the old generation");
    report = run(f.snapshot, {{"out-of-bounds", VisibleInvariant{id("save")}, NodeHandle{f.snapshot.generation, 999}}});
    check(has(report.results[0].diagnostics, "stale_target", "/invariants/0/scope") && f.snapshot == before,
          "Evaluation rejects expired scopes and changes no observations");
}
void geometric_policies() {
    Fixture f;
    auto& a = *f.snapshot.elements[f.node("save").index].geometry;
    auto& b = *f.snapshot.elements[f.node("spare").index].geometry;
    a.border_box = {{0, 0}, {10, 10}}; b.border_box = {{10, 0}, {10, 10}};
    auto report = run(f.snapshot, {{"touch", NoOverlapInvariant{id("save"), id("spare")}}});
    check(report.passed(), "Touching half-open edges do not overlap");
    b.border_box.origin.x = 9.75f;
    report = run(f.snapshot, {
        {"overlap", NoOverlapInvariant{id("save"), id("spare")}},
        {"tolerated", NoOverlapInvariant{id("save"), id("spare"), .25f}}
    });
    check(report.results[0].status == InvariantStatus::failed && report.results[1].status == InvariantStatus::passed &&
        report.results[0].observations.size() == 2 && report.results[0].observations[1].bounds == b.border_box,
        "Positive penetration fails; declared logical tolerance passes and owns both bounds");
    a.clip = Rect{{20, 0}, {10, 10}};
    report = run(f.snapshot, {
        {"fully-clipped", VisibleInvariant{id("save"), false}},
        {"raw-overlap", NoOverlapInvariant{id("save"), id("spare")}}
    });
    check(report.results[0].status == InvariantStatus::passed && report.results[1].status == InvariantStatus::failed,
          "Visibility respects clips; overlap explicitly compares full layout boxes");
    a.clip = Rect{{9, 0}, {10, 10}};
    check(run(f.snapshot, {{"partial", VisibleInvariant{id("save")}}}).passed(), "A positive clipped intersection is visible");
    a.clip = Rect{{10, 0}, {10, 10}};
    check(run(f.snapshot, {{"clip-touch", VisibleInvariant{id("save"), false}}}).passed(), "Clip-edge contact is invisible");
    a.border_box = {{-.25f, 0}, {10, 10}};
    report = run(f.snapshot, {
        {"outside", InsideViewportInvariant{id("save")}},
        {"tolerated", InsideViewportInvariant{id("save"), .25f}}
    }, Size{10, 10});
    check(report.results[0].status == InvariantStatus::failed && report.results[1].status == InvariantStatus::passed,
          "Viewport tests check the entire unclipped border with explicit tolerance");
    a.clip.reset(); a.border_box = {{5, 5}, {0, 10}};
    check(run(f.snapshot, {{"zero", VisibleInvariant{id("save"), false}},
        {"zero-overlap", NoOverlapInvariant{id("save"), id("spare")}}}).passed(), "Zero-area bounds are invisible and cannot overlap");
    a.border_box = {{std::numeric_limits<float>::max(), 0}, {std::numeric_limits<float>::max(), 10}};
    report = run(f.snapshot, {{"large", InsideViewportInvariant{id("save")}}}, Size{std::numeric_limits<float>::max(), 10});
    check(report.results[0].status == InvariantStatus::failed, "Finite float endpoints use double arithmetic without overflow");
}
void declaration_and_snapshot_rejection() {
    Fixture f;
    check(has(evaluate_invariants({}).diagnostics, "missing_input", "/snapshot"), "Missing input rejects");
    std::vector<UiInvariant> declarations{{"inside", InsideViewportInvariant{id("save")}}};
    check(has(evaluate_invariants({&f.snapshot, declarations}).diagnostics, "missing_input", "/viewport"), "Viewport is never inferred from root bounds");
    check(has(evaluate_invariants({&f.snapshot, declarations, Size{0, 10}}).diagnostics, "out_of_range", "/viewport/width"), "Viewport must be positive");
    declarations = {{"same", VisibleInvariant{id("save")}}, {"same", EnabledInvariant{id("save")}},
        {"bad", NoOverlapInvariant{id("save"), id("spare"), -1}},
        {"invalid", SemanticNameInvariant{id("save"), std::string("\xff", 1)}}};
    auto invalid = evaluate_invariants({&f.snapshot, declarations});
    check(!invalid && has(invalid.diagnostics, "invariant_duplicate_name", "/invariants/1/name") &&
        has(invalid.diagnostics, "out_of_range", "/invariants/2/tolerance") &&
        has(invalid.diagnostics, "invalid_utf8", "/invariants/3/expected"), "Declarations preflight with located errors and no partial report");
    declarations = {{"", VisibleInvariant{id("save")}},
        {"long", SemanticNameInvariant{id("save"), std::string(max_invariant_text_bytes + 1, 'a')}},
        {"point", VisibleInvariant{PointTarget{{std::numeric_limits<float>::infinity(), 0}}}},
        {"path", VisibleInvariant{PathTarget{std::vector<std::uint32_t>(max_document_depth)}}}};
    invalid = evaluate_invariants({&f.snapshot, declarations});
    check(has(invalid.diagnostics, "invariant_name", "/invariants/0/name") &&
        has(invalid.diagnostics, "invariant_limit", "/invariants/1/expected") &&
        has(invalid.diagnostics, "invalid_number", "/invariants/2/target/position/x") &&
        has(invalid.diagnostics, "invariant_limit", "/invariants/3/target/children"), "Name, text, point and path bounds are explicit");
    declarations.clear();
    for (std::size_t i = 0; i < max_ui_invariants; ++i) declarations.push_back({"visible-" + std::to_string(i), VisibleInvariant{id("save")}});
    check(run(f.snapshot, declarations).passed(), "Exact declaration bound is accepted");
    declarations.push_back({"excess", VisibleInvariant{id("save")}});
    check(has(evaluate_invariants({&f.snapshot, declarations}).diagnostics, "invariant_limit", "/invariants"), "Excess declarations reject before copying");
    check(run(f.snapshot, {}).passed(), "An explicit empty report retains a generation and zero results");
    const auto original = f.snapshot;
    f.snapshot.elements[0].children[0] = 999;
    check(has(evaluate_invariants({&f.snapshot, {}}).diagnostics, "invariant_snapshot", "/snapshot/elements/999"), "Invalid child index cannot reach the resolver");
    f.snapshot = original; f.snapshot.elements[0].children[0] = 0;
    check(!evaluate_invariants({&f.snapshot, {}}), "Cyclic edges reject without recursion");
    f.snapshot = original; f.snapshot.semantics.nodes[1].node.tree++;
    check(!evaluate_invariants({&f.snapshot, {}}), "Mixed semantic generations reject");
    f.snapshot = original; f.snapshot.elements[f.node("save").index].semantic = 999;
    check(!evaluate_invariants({&f.snapshot, {}}), "Invalid semantic links cannot be read");
    f.snapshot = original; f.snapshot.elements[f.node("save").index].geometry->border_box.size.width = -1;
    check(!evaluate_invariants({&f.snapshot, {}}), "Invalid rectangle sizes reject");
    f.snapshot = original; f.snapshot.generation = 0;
    check(!evaluate_invariants({&f.snapshot, {}}), "Zero generation is not a captured snapshot");
}
} // namespace
int main() {
    try {
        semantic_checks_and_owned_report(); resolution_and_generations(); geometric_policies(); declaration_and_snapshot_rejection();
        std::cout << "UI invariant checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
