#include <tessera/ui/component_mount.hpp>
#include <tessera/ui/async.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <thread>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
template<class F> void rejects(F f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) {
        check(e.diagnostic().code == code, "Preserve the runtime failure code");
        return;
    }
    throw std::runtime_error("Expected reactive failure: " + std::string(code));
}
Result<KeyedUpdate> mount(ComponentCollection& collection, ReactiveRuntime& runtime,
                          std::initializer_list<ComponentMount> input) {
    return collection.reconcile(runtime, {input.begin(), input.size()});
}
template<class T> Signal<T> state(const ComponentCollection& collection, std::string_view key, std::string_view slot) {
    const auto result = collection.state<T>(key, slot);
    check(static_cast<bool>(result), "Declared exact-type state must be available");
    return *result.value;
}
ComponentDescription row() {
    ComponentDescription c;
    c.kind = "item-row";
    c.document.root.kind = NodeKind::text; c.document.root.id = "row";
    c.document.root.properties["text"] = std::string{};
    c.props = {{"caption", PropertyType::string, {}}, {"focusable", PropertyType::boolean, Property{true}}};
    c.properties = {{"caption", "row", "text"}, {"focusable", "row", "focusable"}};
    c.local_state = {{"pinned", PropertyType::boolean, Property{false}},
        {"caption", PropertyType::string, Property{std::string("Map")}},
        {"count", PropertyType::number, Property{1.0}},
        {"target", PropertyType::reference, Property{NodeReference{"row"}}}};
    return c;
}

void compatible_named_state() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = row();
    const auto first = mount(rows, r, {{"a", &c}, {"b", &c}});
    check(first && first.value->created == std::vector<std::size_t>{0, 1}, "Mount creates owners and registered state");
    const auto a = rows.find("a")->lifetime;
    auto pinned = state<bool>(rows, "a", "pinned");
    auto caption = state<std::string>(rows, "a", "caption");
    auto count = state<double>(rows, "a", "count");
    auto target = state<NodeReference>(rows, "a", "target");
    int evaluations = 0;
    const auto label = r.computed<std::string>(rows.find("a")->owner, "label", [&] {
        ++evaluations; return caption.read() + (pinned.read() ? " pinned" : "");
    });
    r.flush();
    check(count.read() == 1 && target.read() == NodeReference{"row"}, "Number and reference state retain exact types");
    r.batch([&] { pinned.write(true); caption.write("Lantern"); }); r.flush();
    check(r.snapshot(label) == "Lantern pinned" && evaluations == 2, "Ordinary state signals drive existing calculations");
    std::reverse(c.local_state.begin(), c.local_state.end());
    c.local_state.back().initial_value = true;
    c.local_state[2].initial_value = std::string("Reset must not run");
    const auto moved = mount(rows, r, {{"b", &c}, {"a", &c}});
    check(moved && moved.value->created.empty() && moved.value->disposed.empty() && rows.current("a", a),
          "Reorder and declaration reorder retain the owner and lifetime");
    r.flush();
    check(pinned.read() && state<std::string>(rows, "a", "caption").read() == "Lantern" && evaluations == 2 &&
        caption.revision() == 2, "Changed initial values do not overwrite existing state or invalidate consumers");
    check(has(rows.state<double>("a", "pinned").diagnostics, "component_state_type", "/components/a/local_state/pinned") &&
        has(rows.state<bool>("a", "missing").diagnostics, "component_unknown_state", "/components/a/local_state/missing") &&
        has(rows.state<bool>("x/~", "p/~").diagnostics, "component_instance", "/components/x~1~0/local_state/p~1~0"),
        "Access rejects wrong types, undeclared slots and missing keys with escaped paths");
    scope.dispose();
    rejects([&] { pinned.read(); }, "disposed_reactive_value");
    rejects([&] { mount(rows, r, {}); }, "disposed_reactive_owner");
}

void complete_candidate_rejection() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = row();
    check(static_cast<bool>(mount(rows, r, {{"a", &c}, {"b", &c}})), "Initial mount succeeds");
    const auto a = rows.find("a")->lifetime, b = rows.find("b")->lifetime;
    auto pinned = state<bool>(rows, "a", "pinned"); pinned.write(true);
    auto incompatible = c; incompatible.local_state[0] = {"pinned", PropertyType::number, Property{1.0}};
    auto invalid = mount(rows, r, {{"a", &incompatible}});
    check(!invalid && has(invalid.diagnostics, "component_state_schema", "/components/0/local_state") &&
        rows.current("a", a) && rows.current("b", b) && pinned.read(), "Schema rejection does not remove absent candidates");
    incompatible = c; incompatible.local_state.pop_back();
    check(has(mount(rows, r, {{"a", &incompatible}}).diagnostics, "component_state_schema", "/components/0/local_state"),
          "Adding or removing a slot requires explicit kind replacement");
    incompatible = c; incompatible.local_state[0].name = "renamed";
    check(has(mount(rows, r, {{"a", &incompatible}}).diagnostics, "component_state_schema", "/components/0/local_state"),
          "Renaming an exact-type slot is incompatible");
    auto bad = c; bad.local_state.push_back(bad.local_state[0]);
    ReactiveRuntime foreign;
    invalid = mount(rows, foreign, {{"new", &c}, {"a", &bad}});
    check(has(invalid.diagnostics, "component_duplicate_state", "/components/1/local_state/4/name") && rows.current("b", b),
          "Validate every candidate before runtime entry, even with an invalid runtime");
    check(has(mount(rows, r, {{"a", nullptr}}).diagnostics, "component_description", "/components/0/component") &&
        has(mount(rows, r, {{"a", &c}, {"a", &c}}).diagnostics, "duplicate_key", "/components/1/key"),
        "Null descriptions and duplicate keys reject before mutations");
    bad = c; bad.local_state[0].initial_value = 1.0;
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_state_type", "/components/0/local_state/0/initial"),
          "Initial values are never coerced");
    bad = c; bad.local_state[2].initial_value = std::numeric_limits<double>::infinity();
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "invalid_number", "/components/0/local_state/2/initial"),
          "Nonfinite state initialization rejects");
    bad = c; bad.local_state[1].initial_value = std::string("\xff");
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "invalid_utf8", "/components/0/local_state/1/initial"),
          "Invalid UTF-8 rejects");
    bad.local_state[1].initial_value = std::string(max_component_state_text_bytes + 1, 'x');
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_state_limit", "/components/0/local_state/1/initial"),
          "Oversized state text rejects before copying");
    bad = c; bad.local_state[3].initial_value = NodeReference{"bad\n"};
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_name", "/components/0/local_state/3/initial/ref"),
          "Reference initial values use bounded names");
    bad = c; bad.kind = std::string(257, 'x');
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_name", "/components/0/kind"), "Kinds are bounded");
    bad = c; bad.local_state[0].name = "bad\n";
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_name", "/components/0/local_state/0/name"), "State names reject controls");
    bad = c; bad.local_state[0].type = static_cast<PropertyType>(255);
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_state_type", "/components/0/local_state/0/initial"), "Invalid type tags reject");
    bad = c; bad.local_state.resize(max_component_local_state + 1);
    check(has(mount(rows, r, {{"a", &bad}}).diagnostics, "component_state_limit", "/components/0/local_state"), "Per-component count rejects");
    auto many_slots = c; many_slots.local_state.clear();
    for (std::size_t i = 0; i < max_component_local_state; ++i)
        many_slots.local_state.push_back({std::to_string(i), PropertyType::boolean, Property{false}});
    std::vector<ComponentMount> many;
    for (std::size_t i = 0; i < 17; ++i) many.push_back({std::to_string(i), &many_slots});
    check(has(rows.reconcile(r, many).diagnostics, "component_state_limit", "/components"), "Aggregate declared-state count rejects");
    many.resize(max_keyed_children + 1);
    check(has(rows.reconcile(r, many).diagnostics, "keyed_child_limit", "/components"), "Collection count rejects before traversal");
    check(rows.instances().size() == 2 && rows.current("a", a) && rows.current("b", b), "All invalid candidates preserve accepted owners");
}

void disposal_and_host_owned_operations() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = row();
    check(static_cast<bool>(mount(rows, r, {{"a", &c}, {"b", &c}, {"c", &c}})), "Initial rows mount");
    const auto a = *rows.find("a"), b = *rows.find("b"), old_c = *rows.find("c");
    const auto pinned = state<bool>(rows, "a", "pinned");
    pinned.write(true);
    std::vector<std::string> closed;
    for (const auto& instance : rows.instances())
        r.on_cleanup(instance.owner, "detach", [&, key = instance.key] { closed.push_back(key); });
    AsyncValue resource(r, a.owner, "asset", PropertyType::string);
    const auto attempt = resource.begin("request"); check(static_cast<bool>(attempt), "Host work begins under mounted lifetime");
    int applied = 0, evaluations = 0;
    r.effect<bool>(a.owner, "effect", EffectPhase::notify, [&] { ++evaluations; return pinned.read(); },
        [&](bool) { ++applied; });
    r.flush(); check(r.publish() == 1 && applied == 0, "Mount does not deliver external work");
    auto replacement = c; replacement.kind = "new-row";
    replacement.local_state[0].initial_value = true;
    check(static_cast<bool>(mount(rows, r, {{"c", &replacement}, {"b", &c}})) && closed.empty(),
          "Removal and kind replacement defer all external cleanup");
    check(!rows.current("a", a.lifetime) && !rows.current("c", old_c.lifetime) && rows.current("b", b.lifetime),
          "Only compatible owners survive");
    check(r.deliver_effects(EffectPhase::notify).empty() && applied == 0, "Disposal invalidates queued effects");
    const auto late = resource.complete(attempt.value->attempt, std::string("Late"));
    check(!late, "Old async completions cannot update a removed owner");
    check(r.deliver_cleanups().empty() && closed == std::vector<std::string>{"a", "c"}, "Cleanup follows previous collection order");
    check(static_cast<bool>(mount(rows, r, {{"a", &c}, {"b", &c}, {"c", &replacement}})), "Removed key can mount again");
    check(!state<bool>(rows, "a", "pinned").read() && state<bool>(rows, "c", "pinned").read() &&
        rows.find("a")->lifetime > old_c.lifetime, "Recreated keys initialize fresh state with never-reused lifetimes");
    rejects([&] { pinned.write(false); }, "disposed_reactive_value");
    r.flush(); check(evaluations == 1, "Disposed effect never evaluates again");
    check(r.deliver_cleanups().empty() && closed.size() == 2, "Cleanup runs once");
}

void exact_initialization_bounds() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    ComponentDescription c; c.kind = "bounded";
    for (std::size_t i = 0; i < max_component_local_state; ++i)
        c.local_state.push_back({std::to_string(i), PropertyType::boolean, Property{false}});
    c.local_state[0] = {std::string(256, 's'), PropertyType::string, Property{std::string(max_component_state_text_bytes, 'x')}};
    std::vector<ComponentMount> input;
    for (std::size_t i = 0; i < max_reactive_nodes / max_component_local_state; ++i) input.push_back({std::to_string(i), &c});
    const auto update = rows.reconcile(r, input);
    check(update && update.value->created.size() == input.size() &&
        state<std::string>(rows, "0", c.local_state[0].name).read().size() == max_component_state_text_bytes,
        "Exact per-component, collection, name and text limits are accepted");
    auto empty = c; empty.kind = "empty"; empty.local_state.clear();
    const auto replacement = mount(rows, r, {{"0", &empty}});
    check(replacement && replacement.value->created == std::vector<std::size_t>{0} &&
        !rows.state<bool>("0", "1"), "An explicit kind replacement can remove all local state");
}

void runtime_failure_repair_and_entry() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = row(); c.local_state.resize(1);
    check(static_cast<bool>(mount(rows, r, {{"kept", &c}})), "Kept row mounts");
    const auto kept = rows.find("kept")->lifetime;
    const auto filler = r.owner(r.root(), "filler");
    // Permit one new signal, then fail in the following sibling's setup.
    for (std::size_t i = 0; i < max_reactive_nodes - 2; ++i) r.signal(filler, std::to_string(i), false);
    rejects([&] { mount(rows, r, {{"kept", &c}, {"first", &c}, {"second", &c}}); }, "reactive_node_limit");
    check(rows.instances().size() == 1 && rows.current("kept", kept) && !rows.state<bool>("first", "pinned"),
          "Failed setup disposes every newly created owner and repairs state records");
    filler.dispose();
    const auto recovered = mount(rows, r, {{"kept", &c}, {"first", &c}});
    check(recovered && rows.find("first")->lifetime > 3 && !state<bool>(rows, "first", "pinned").read(),
          "Retry starts fresh after consuming failed creation lifetimes");
    const auto first = rows.find("first")->lifetime;
    const auto more_filler = r.owner(r.root(), "more-filler");
    for (std::size_t i = 0; i < max_reactive_nodes - 3; ++i) r.signal(more_filler, std::to_string(i), false);
    auto replacement = row(); replacement.kind = "replacement"; replacement.local_state.resize(3);
    const auto old = state<bool>(rows, "kept", "pinned");
    rejects([&] { mount(rows, r, {{"kept", &replacement}, {"first", &c}}); }, "reactive_node_limit");
    check(rows.instances().size() == 1 && rows.current("first", first) && !rows.find("kept") &&
        !rows.state<bool>("kept", "pinned"), "Failed replacement drops disposed state without restoring an expired owner");
    rejects([&] { old.read(); }, "disposed_reactive_value");
    more_filler.dispose();
    check(static_cast<bool>(mount(rows, r, {{"kept", &c}, {"first", &c}})), "Replacement setup failure permits a fresh retry");
    ReactiveRuntime foreign;
    rejects([&] { mount(rows, foreign, {{"kept", &c}, {"first", &c}}); }, "foreign_reactive_owner");
    std::string thread_error;
    std::thread worker([&] {
        try { mount(rows, r, {{"kept", &c}, {"first", &c}}); }
        catch (const ReactiveError& e) { thread_error = e.diagnostic().code; }
    }); worker.join();
    check(thread_error == "reactive_thread", "Compatible reuse also enforces the runtime UI thread");
    const auto trigger = r.signal(r.root(), "trigger", 0);
    const auto bad = r.computed<int>(r.root(), "bad", [&] { mount(rows, r, {}); return trigger.read(); });
    rejects([&] { bad.read(); }, "reactive_mutation");
    r.root().dispose();

    ReactiveRuntime effect_runtime;
    ComponentCollection effect_rows(effect_runtime.owner(effect_runtime.root(), "rows"));
    check(static_cast<bool>(mount(effect_rows, effect_runtime, {{"a", &c}})), "Effect row mounts");
    bool rejected_effect_entry = false;
    effect_runtime.effect<bool>(effect_runtime.root(), "bad", EffectPhase::notify, [] { return true; },
        [&](bool) {
            rejects([&] { mount(effect_rows, effect_runtime, {{"a", &c}}); }, "reactive_effect_entry");
            rejected_effect_entry = true;
        });
    effect_runtime.flush(); effect_runtime.publish();
    check(effect_runtime.deliver_effects(EffectPhase::notify).empty() && rejected_effect_entry,
          "Compatible reuse cannot mutate component records during effect delivery");
}

void lowering_and_full_presentation() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = row();
    auto loaded = load_document_with_sources(R"({"version":1,"root":{"type":"Text","id":"row","properties":{"text":""}}})", "fixture:row");
    check(static_cast<bool>(loaded), "Sourced template loads");
    c.document = loaded.value->document; c.sources = loaded.value->sources;
    check(static_cast<bool>(mount(rows, r, {{"row", &c}})), "Sourced description mounts");
    ComponentArguments a; a.props["caption"] = state<std::string>(rows, "row", "caption");
    auto bad = c; bad.local_state[0].initial_value = std::string("invalid");
    r.begin_batch();
    check(has(lower_component(r, bad, a).diagnostics, "component_state_type", "/component/local_state/0/initial"),
          "Ordinary lowering rejects invalid state schema before settled reads");
    check(static_cast<bool>(mount(rows, r, {{"row", &c}})), "Mount joins an existing host batch without closing it");
    check(has(lower_component(r, c, a).diagnostics, "reactive_open_batch", "/reactive"), "Mount never closes the outer batch");
    r.end_batch();
    state<std::string>(rows, "row", "caption").write("Lantern"); r.flush();
    const auto bound = lower_component(r, c, a);
    check(bound && bound.value->document.root.properties.at("text") == Property{std::string("Lantern")},
          "Registered state passes through ordinary settled property lowering");
    const auto saved = save_document(bound.value->document);
    check(saved && load_document(*saved.value).value == bound.value->document && saved.value->find("local_state") == std::string::npos,
          "State declarations do not change serialized documents");
    const auto tree = UiTree::create(bound.value->document);
    const StyleSheet sheet;
    const auto styles = resolve_styles({tree.value->get(), &sheet});
    PlaceholderTextShaper text;
    const auto layout = compute_layout({tree.value->get(), *styles.value, {240, 120}, &text});
    const auto semantics = capture_semantics({tree.value->get(), *styles.value, &*layout.value}, 1);
    const auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    const auto inspection = capture_inspection({tree.value->get(), *styles.value, &*layout.value, &*bound.value->sources});
    check(tree && styles && layout && semantics && paint && inspection &&
        inspection.value->elements[0].source->file == "fixture:row" && !paint.value->commands.empty(),
        "Full-tree styles, layout, semantics, paint and owned source inspection accept mounted state");
    check(r.publish() == 0, "Host publishes explicitly after accepting the presentation");
    ComponentInvocation invocation{bad, a, {}};
    check(has(lower_component_tree(r, {&invocation, 1}).diagnostics, "component_state_type", "/invocations/0/component/local_state/0/initial"),
          "Nested invocation lowering shares local-state validation");
    scope.dispose();
    check(inspection.value->elements[0].source->file == "fixture:row", "Accepted inspection owns provenance after disposal");
}

void automatic_setup_lifetime() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = row();
    std::vector<std::string> setup_order, detached;
    std::map<std::string, AsyncValue, std::less<>> resources;
    std::map<std::string, ComponentSetup, std::less<>> contexts;
    int evaluations = 0, applied = 0;
    const auto setup = [&](ReactiveRuntime& runtime, const ComponentSetup& context) {
        const auto& instance = context.instance();
        setup_order.push_back(instance.key);
        contexts.insert_or_assign(instance.key, context); // Owned weak handles.
        const auto pinned = context.state<bool>("pinned");
        check(pinned && !pinned.value->read(), "Setup sees freshly registered state");
        check(has(context.state<double>("pinned").diagnostics, "component_state_type",
                  "/components/" + instance.key + "/local_state/pinned") &&
              has(context.state<bool>("x/~").diagnostics, "component_unknown_state",
                  "/components/" + instance.key + "/local_state/x~1~0"), "Setup uses exact-type, located state access");
        check(has(rows.reconcile(runtime, {}).diagnostics, "component_setup_reentry", "/components"),
              "Recursive reconciliation cannot change provisional records");
        rejects([&] { runtime.snapshot(*pinned.value); }, "reactive_open_batch");
        resources.insert_or_assign(instance.key, AsyncValue(runtime, instance.owner, "asset", PropertyType::string));
        runtime.on_cleanup(instance.owner, "detach", [&, key = instance.key] { detached.push_back(key); });
        runtime.effect<bool>(instance.owner, "status", EffectPhase::notify,
            [&, value = *pinned.value] { ++evaluations; return value.read(); }, [&](bool) { ++applied; });
    };
    const auto first = mount(rows, r, {{"a", &c, setup}, {"b", &c, setup}});
    check(first && setup_order == std::vector<std::string>{"a", "b"} && evaluations == 0 && applied == 0,
          "Setup runs in candidate order without evaluation or effect delivery");
    const auto a = rows.find("a")->lifetime;
    const auto old_context = contexts.at("a");
    const auto old_resource = resources.at("a");
    const auto attempt = old_resource.begin("thumbnail");
    check(static_cast<bool>(attempt), "Host starts work on the automatically attached observation");
    state<bool>(rows, "a", "pinned").write(true);
    r.flush(); check(r.publish() == 2, "Publication is explicit after setup settlement");
    check(r.deliver_effects(EffectPhase::notify).empty() && applied == 2, "Host delivers owned effects explicitly");
    const auto reordered = mount(rows, r, {{"b", &c, setup}, {"a", &c, setup}});
    r.flush();
    check(reordered && reordered.value->created.empty() && setup_order.size() == 2 && evaluations == 2 &&
          rows.current("a", a) && contexts.at("a").state<bool>("pinned").value->read(),
          "Reorder preserves state and registrations without invoking setup again");
    const auto removed = mount(rows, r, {{"b", &c, setup}});
    check(removed && detached.empty() && !old_resource.complete(attempt.value->attempt, std::string("late")),
          "Removal expires async delivery and defers host detachment");
    rejects([&] { old_context.state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    r.flush();
    check(evaluations == 2 && r.deliver_cleanups().empty() && detached == std::vector<std::string>{"a"},
          "Removed effects do not evaluate and cleanup runs after mounting returns");
    const auto recreated = mount(rows, r, {{"a", &c, setup}, {"b", &c, setup}});
    check(recreated && setup_order == std::vector<std::string>{"a", "b", "a"} && !rows.current("a", a) &&
          !state<bool>(rows, "a", "pinned").read(), "Reinsertion invokes setup for a fresh state lifetime");
    scope.dispose();
    check(r.deliver_cleanups().empty() && detached == std::vector<std::string>{"a", "b", "a"},
          "Scope shutdown preserves deterministic once-only cleanup order");
}

void automatic_setup_failure_repair() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = row();
    check(static_cast<bool>(mount(rows, r, {{"kept", &c}})), "Compatible row exists before failed setup");
    const auto kept = rows.find("kept")->lifetime;
    state<bool>(rows, "kept", "pinned").write(true);
    std::vector<ComponentSetup> contexts;
    std::vector<std::string> closed;
    int evaluated = 0, applied = 0;
    bool fail = true;
    const auto setup = [&](ReactiveRuntime& runtime, const ComponentSetup& context) {
        contexts.push_back(context);
        runtime.on_cleanup(context.instance().owner, "detach", [&, key = context.instance().key] { closed.push_back(key); });
        runtime.effect<bool>(context.instance().owner, "effect", EffectPhase::notify,
            [&] { ++evaluated; return true; }, [&](bool) { ++applied; });
        if (fail && context.instance().key == "second") throw std::runtime_error("Synthetic setup failure");
    };
    try { mount(rows, r, {{"kept", &c, setup}, {"first", &c, setup}, {"second", &c, setup}}); }
    catch (const ReactiveError& e) {
        check(e.diagnostic().code == "component_setup_failure" && e.diagnostic().path == "/components/2/setup",
              "Host setup exceptions identify their candidate position");
    }
    check(contexts.size() == 2 && rows.instances().size() == 1 && rows.current("kept", kept) &&
          state<bool>(rows, "kept", "pinned").read() && !rows.state<bool>("first", "pinned"),
          "Failure disposes initialized siblings and retains compatible owner state");
    for (const auto& context : contexts)
        rejects([&] { context.state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    r.flush();
    check(r.publish() == 0 && evaluated == 0 && applied == 0 && closed.empty(),
          "Failed setup never settles or publishes its disposed effects");
    check(r.deliver_cleanups().empty() && closed == std::vector<std::string>{"first", "second"},
          "Host delivers cleanup for every partially initialized owner once");
    const auto failed_lifetime = contexts.back().instance().lifetime;
    fail = false;
    const auto retried = mount(rows, r, {{"kept", &c, setup}, {"second", &c, setup}});
    check(retried && contexts.size() == 3 && rows.find("second")->lifetime > failed_lifetime,
          "Retry invokes setup with a never-reused lifetime");
    auto invalid = c; invalid.local_state.push_back(invalid.local_state.front());
    check(!mount(rows, r, {{"new", &c, setup}, {"bad", &invalid, setup}}) && contexts.size() == 3,
          "Complete candidate rejection invokes no setup and changes no owners");
    auto replacement = c; replacement.kind = "replacement";
    fail = true;
    rejects([&] { mount(rows, r, {{"kept", &c, setup}, {"second", &replacement, setup}}); }, "component_setup_failure");
    check(rows.instances().size() == 1 && rows.current("kept", kept) && !rows.find("second"),
          "Failed replacement does not restore its disposed predecessor");
    fail = false;
    check(static_cast<bool>(mount(rows, r, {{"kept", &c}, {"second", &replacement, setup}})),
          "Failure resets the reconciliation guard for later setup");
}

void setup_disposal_and_runtime_errors() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = row();
    const auto self_dispose = [](ReactiveRuntime&, const ComponentSetup& setup) { setup.instance().owner.dispose(); };
    rejects([&] { mount(rows, r, {{"a", &c, self_dispose}}); }, "disposed_reactive_owner");
    check(rows.instances().empty(), "Setup cannot return an expired owner as a successful mount");
    const auto duplicate = [](ReactiveRuntime& runtime, const ComponentSetup& setup) {
        runtime.signal(setup.instance().owner, "pinned", false);
    };
    rejects([&] { mount(rows, r, {{"a", &c, duplicate}}); }, "duplicate_reactive_name");
    check(rows.instances().empty(), "Runtime setup failures preserve their original diagnostic and repair records");
    bool allocation_failed = false;
    try { mount(rows, r, {{"a", &c, [](ReactiveRuntime&, const ComponentSetup&) { throw std::bad_alloc(); }}}); }
    catch (const std::bad_alloc&) { allocation_failed = true; }
    check(allocation_failed && rows.instances().empty(), "Allocation failures propagate after disposing fresh owners");
    check(static_cast<bool>(mount(rows, r, {{"kept", &c}})), "A compatible owner mounts after failed setup");
    rejects([&] { mount(rows, r, {{"kept", &c}, {"a", &c,
        [&](ReactiveRuntime&, const ComponentSetup&) { scope.dispose(); }}}); }, "disposed_reactive_owner");
    check(rows.instances().empty() && !rows.state<bool>("kept", "pinned"),
          "Host scope disposal during setup cannot leave expired compatible records");
}
} // namespace

int main() {
    try {
        compatible_named_state();
        complete_candidate_rejection();
        disposal_and_host_owned_operations();
        exact_initialization_bounds();
        runtime_failure_repair_and_entry();
        lowering_and_full_presentation();
        automatic_setup_lifetime();
        automatic_setup_failure_repair();
        setup_disposal_and_runtime_errors();
        std::cout << "Component mounting: named state, keyed ownership, rejection, failure repair and presentation agree.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
