#include <tessera/ui/bindings.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <iostream>
#include <thread>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {
template<class Source>
concept TextBindable = requires(PropertyBindings& bindings, Source source) {
    bindings.bind<BoundProperty::text>("label", source);
};
static_assert(TextBindable<Signal<std::string>> && TextBindable<Computed<std::string>>);
static_assert(!TextBindable<Signal<bool>> && !TextBindable<Signal<NodeReference>> && !TextBindable<Signal<double>>);

UiDocument document() {
    UiDocument doc;
    doc.root.id = "panel";
    doc.extensions["host:data"].value = std::string("preserved");
    UiNode label;
    label.kind = NodeKind::text; label.id = "label"; label.properties["text"] = std::string("Initial");
    UiNode button;
    button.id = "button"; button.events["activate"] = "inspect";
    doc.root.children = {label, button};
    return doc;
}
const ValidationContext context{{"inspect"}};

template<class F>
void rejects(F f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == code, "Snapshot preserves the reactive diagnostic"); return; }
    throw std::runtime_error("Expected snapshot failure: " + std::string(code));
}

void typed_values_and_full_tree_parity() {
    ReactiveRuntime r;
    auto name = r.signal(r.root(), "name", std::string("Rope"));
    auto pinned = r.signal(r.root(), "pinned", false);
    int evaluated = 0;
    const auto caption = r.computed<std::string>(r.root(), "caption", [&] {
        ++evaluated; return name.read() + (pinned.read() ? " (pinned)" : "");
    });
    auto focusable = r.signal(r.root(), "focusable", true);
    auto disabled = r.computed<bool>(r.root(), "disabled", [&] { return !pinned.read(); });
    auto reference = r.signal(r.root(), "reference", NodeReference{"label"});
    PropertyBindings bindings;
    check(bindings.bind<BoundProperty::text>("label", caption).value == 0 &&
          bindings.bind<BoundProperty::focusable>("button", focusable).value == 1 &&
          bindings.bind<BoundProperty::disabled>("button", disabled).value == 2 &&
          bindings.bind<BoundProperty::labelled_by>("button", reference).value == 3,
          "Typed bindings retain registration order");
    const auto authored = document();
    auto before_flush = bindings.apply(r, authored, context);
    check(!before_flush && has(before_flush.diagnostics, "reactive_unsettled", "/reactive/root/values/caption") && evaluated == 0,
          "Presentation must not demand-evaluate a calculation");
    r.batch([&] { name.write("Lantern"); name.write("Map"); pinned.write(true); });
    check(r.flush().empty() && evaluated == 1, "The batch settles only its final caption");
    const auto bound = bindings.apply(r, authored, context);
    auto expected = authored;
    expected.root.children[0].properties["text"] = std::string("Map (pinned)");
    expected.root.children[1].properties["focusable"] = true;
    expected.root.children[1].properties["disabled"] = false;
    expected.root.children[1].properties["labelled_by"] = NodeReference{"label"};
    check(bound && bound.value->document == expected && authored == document() && evaluated == 1,
          "Binding copies final typed values without mutating authored input or evaluating again");
    check(bound.value->stages == (PropertyStages::layout | PropertyStages::paint | PropertyStages::input | PropertyStages::semantics),
          "Affected stages come from property reflection");
    auto same = bindings.apply(r, expected, context);
    check(same && same.value->stages == PropertyStages::none, "Stage differences are relative to the supplied document");
    auto encoded = save_document(bound.value->document, context);
    auto decoded = encoded ? load_document(*encoded.value, context) : Result<UiDocument>{};
    check(decoded && *decoded.value == expected, "Lowered bindings use ordinary JSON and preserve extensions");
    auto bound_tree = UiTree::create(bound.value->document, context), reference_tree = UiTree::create(expected, context);
    check(bound_tree && reference_tree, "Both construction paths validate");
    auto bt = bound_tree.value->get(), rt = reference_tree.value->get();
    const StyleSheet sheet;
    const auto bs = resolve_styles({bt, &sheet}), rs = resolve_styles({rt, &sheet});
    check(bs && rs && *bs.value == *rs.value, "Full-tree style output matches authored values");
    PlaceholderTextShaper shaper;
    auto bl = compute_layout({bt, *bs.value, {240, 100}, &shaper});
    auto rl = compute_layout({rt, *rs.value, {240, 100}, &shaper});
    check(bl && rl && bl.value->boxes.size() == rl.value->boxes.size(), "Full-tree layout succeeds");
    for (std::size_t i = 0; i < bl.value->boxes.size(); ++i) {
        auto actual = bl.value->boxes[i];
        actual.node = rl.value->boxes[i].node; // Tree identities are deliberately unique.
        check(actual == rl.value->boxes[i], "Bound and authored layout geometry agrees");
    }
    const auto bp = build_paint_list({bt, *bs.value, &*bl.value, &shaper});
    const auto rp = build_paint_list({rt, *rs.value, &*rl.value, &shaper});
    const auto bsem = capture_semantics({bt, *bs.value, &*bl.value}, 1);
    const auto rsem = capture_semantics({rt, *rs.value, &*rl.value}, 1);
    check(bp && rp && *bp.value == *rp.value && bsem && rsem && *bsem.value == *rsem.value,
          "Paint and owned semantics match the full-tree reference");
    FocusDispatcher focus;
    const HitTestInput hit{bt, *bs.value, &*bl.value};
    check(static_cast<bool>(focus.dispatch(hit, {1us, FocusNext{}})), "Bound focus intent is navigable");
    auto activated = focus.dispatch(hit, {2us, Activate{}});
    check(activated && activated.value->actions.size() == 1, "Bound disabled state uses ordinary action eligibility");
    pinned.write(false); r.flush();
    const auto off = bindings.apply(r, expected, context);
    check(off && std::get<bool>(off.value->document.root.children[1].properties.at("disabled")),
          "Later state changes lower to ordinary disabled properties");
}

void invalid_candidates_and_publication() {
    ReactiveRuntime r;
    auto caption = r.signal(r.root(), "caption", std::string("Accepted"));
    auto reference = r.signal(r.root(), "reference", NodeReference{"label"});
    int notifications = 0;
    r.effect<std::string>(r.root(), "notify", EffectPhase::notify, [&] { return caption.read(); },
                         [&](const std::string&) { ++notifications; });
    PropertyBindings bindings;
    check(static_cast<bool>(bindings.bind<BoundProperty::text>("label", caption)), "Register caption");
    check(static_cast<bool>(bindings.bind<BoundProperty::labelled_by>("button", reference)), "Register reference");
    r.flush();
    auto accepted = bindings.apply(r, document(), context);
    check(static_cast<bool>(accepted), "Initial candidate is valid");
    const auto published = accepted.value->document;
    check(r.deliver_effects(EffectPhase::notify).empty() && notifications == 0 && r.publish() == 1,
          "Binding application captures no effects");
    r.deliver_effects(EffectPhase::notify);
    r.batch([&] { caption.write(std::string("\xff", 1)); reference.write({"missing"}); });
    r.flush();
    const auto rejected = bindings.apply(r, published, context);
    check(!rejected && has(rejected.diagnostics, "invalid_utf8", "/root/children/0/properties/text") &&
          has(rejected.diagnostics, "invalid_reference", "/root/children/1/properties/labelled_by/ref"),
          "Schema failures return actionable locations without a partial candidate");
    check(published == accepted.value->document && r.deliver_effects(EffectPhase::notify).empty() && notifications == 1,
          "Host keeps its accepted document and rejected UI delivers no effect");
    caption.write("Recovered"); reference.write({"label"}); r.flush();
    check(static_cast<bool>(bindings.apply(r, published, context)) && r.publish() == 1,
          "A corrected candidate can be accepted without rebuilding bindings");
    r.deliver_effects(EffectPhase::notify);
    check(notifications == 2, "Only accepted candidates publish notifications");
}

void targets_bounds_and_schema() {
    ReactiveRuntime r;
    auto caption = r.signal(r.root(), "caption", std::string("Text"));
    PropertyBindings bindings;
    for (const auto& invalid : std::vector<std::string>{"", "bad\n", std::string("\xff", 1), std::string(257, 'x')})
        check(has(bindings.bind<BoundProperty::text>(invalid, caption).diagnostics, "invalid_binding_target", "/bindings/0/target"),
              "Invalid target IDs do not consume a binding slot");
    check(static_cast<bool>(bindings.bind<BoundProperty::text>("label", caption)), "First binding succeeds");
    check(has(bindings.bind<BoundProperty::text>("label", caption).diagnostics, "duplicate_binding", "/bindings/1/property") && bindings.size() == 1,
          "Competing writers are rejected during registration");
    check(static_cast<bool>(bindings.bind<BoundProperty::text>("button", caption)) &&
          static_cast<bool>(bindings.bind<BoundProperty::text>("absent", caption)), "Register targets for validation");
    r.begin_batch(); // Structural target errors are checked before touching the runtime.
    auto invalid = bindings.apply(r, document(), context);
    check(!invalid && invalid.diagnostics.size() == 2 &&
          has(invalid.diagnostics, "binding_node_kind", "/root/children/1/properties/text") &&
          has(invalid.diagnostics, "binding_target_missing", "/bindings/2/target"), "Report all invalid targets before reading");
    r.end_batch();
    auto authored = document(); authored.root.children[0].properties.erase("text");
    check(has(bindings.apply(r, authored, context).diagnostics, "missing_property", "/root/children/0/properties/text"),
          "An invalid authored document is rejected before copying/lowering");
    PropertyBindings bounded;
    for (std::size_t i = 0; i < max_property_bindings; ++i)
        check(static_cast<bool>(bounded.bind<BoundProperty::text>("id-" + std::to_string(i), caption)), "Capacity accepts bounded registrations");
    check(has(bounded.bind<BoundProperty::text>("overflow", caption).diagnostics, "binding_limit", "/bindings") &&
          bounded.size() == max_property_bindings, "Overflow preserves the bounded binding set");
}

void snapshot_entry_and_faults() {
    ReactiveRuntime r;
    auto source = r.signal(r.root(), "source", std::string("Ready"));
    PropertyBindings bindings;
    bindings.bind<BoundProperty::text>("label", source);
    r.begin_batch();
    check(has(bindings.apply(r, document(), context).diagnostics, "reactive_open_batch", "/reactive"), "Open batches cannot build presentation");
    rejects([&] { (void)r.snapshot(source); }, "reactive_open_batch");
    r.end_batch();
    auto calculation_owner = r.owner(r.root(), "calculation");
    auto reentrant = r.computed<std::string>(calculation_owner, "reentrant", [&] { return r.snapshot(source); });
    rejects([&] { (void)reentrant.read(); }, "reactive_mutation");
    calculation_owner.dispose(); r.flush();
    r.on_cleanup(r.root(), "cleanup", [&] { rejects([&] { r.check_settled(); }, "reactive_cleanup_entry"); });
    r.effect<std::string>(r.root(), "effect", EffectPhase::notify, [&] { return source.read(); },
                         [&](const std::string&) { rejects([&] { (void)r.snapshot(source); }, "reactive_effect_entry"); });
    r.flush(); r.publish();
    check(r.deliver_effects(EffectPhase::notify).empty(), "Effect delivery cannot enter presentation capture");
    std::string thread_error;
    std::thread thread([&] { try { (void)r.snapshot(source); } catch (const ReactiveError& e) { thread_error = e.diagnostic().code; } });
    thread.join(); check(thread_error == "reactive_thread", "Presentation capture stays on the creating thread");
    auto boundary = r.boundary(r.root(), "details");
    auto bad = r.computed<std::string>(boundary, "bad", []() -> std::string { throw std::runtime_error("Missing record"); });
    auto held = r.signal(boundary, "held", std::string("Old"));
    check(r.flush().size() == 1, "Calculation failure is contained");
    r.check_settled(); // Healthy bindings can still build the host fallback document.
    check(static_cast<bool>(bindings.apply(r, document(), context)), "Healthy bindings survive an unrelated faulted subtree");
    PropertyBindings faulted;
    faulted.bind<BoundProperty::text>("label", bad);
    const auto rejected = faulted.apply(r, document(), context);
    check(!rejected && has(rejected.diagnostics, "reactive_faulted_snapshot", "/root/children/0/properties/text"),
          "Faulted presentation reads are rejected without retrying calculation");
    rejects([&] { (void)r.snapshot(held); }, "reactive_faulted_snapshot");
    r.root().dispose(); check(r.deliver_cleanups().empty(), "Explicit shutdown keeps cleanup entry rules");
}

void lifetimes_and_runtime_isolation() {
    ReactiveRuntime r, foreign;
    auto other = foreign.signal(foreign.root(), "other", std::string("Foreign"));
    PropertyBindings bindings;
    bindings.bind<BoundProperty::text>("label", other);
    check(has(bindings.apply(r, document(), context).diagnostics, "foreign_reactive_runtime", "/root/children/0/properties/text"),
          "Every binding belongs to the presentation runtime");
    PropertyBindings expired;
    {
        ReactiveRuntime temporary;
        expired.bind<BoundProperty::text>("label", temporary.signal(temporary.root(), "temporary", std::string("Gone")));
    }
    check(has(expired.apply(r, document(), context).diagnostics, "expired_reactive_runtime", "/root/children/0/properties/text"),
          "Bindings do not keep runtimes alive");
    KeyedCollection rows(r.owner(r.root(), "rows"));
    check(static_cast<bool>(rows.reconcile(r, {{"label", "item"}})), "Create keyed label");
    auto old_owner = rows.find("label")->owner;
    auto source = r.signal(old_owner, "caption", std::string("Old"));
    PropertyBindings old;
    old.bind<BoundProperty::text>("label", source);
    check(static_cast<bool>(old.apply(r, document(), context)), "The live instance binds successfully");
    rows.reconcile(r, {}); rows.reconcile(r, {{"label", "item"}});
    r.signal(rows.find("label")->owner, "caption", std::string("New"));
    const auto stale = old.apply(r, document(), context);
    check(!stale && has(stale.diagnostics, "disposed_reactive_value", "/root/children/0/properties/text"),
          "Reused author IDs cannot revive bindings to a disposed instance");
}
} // namespace

int main() {
    try {
        typed_values_and_full_tree_parity(); invalid_candidates_and_publication();
        targets_bounds_and_schema(); snapshot_entry_and_faults(); lifetimes_and_runtime_isolation();
        std::cout << "Typed property bindings, candidate validation, full-tree parity and lifetime checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
