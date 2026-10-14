#include <tessera/ui/component_tree_mount.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
ComponentDescription description() {
    ComponentDescription c;
    c.kind = "row";
    c.local_state = {{"pinned", PropertyType::boolean, false}};
    c.effects = {{"observe", PropertyType::boolean, EffectPhase::notify}};
    return c;
}
Result<KeyedUpdate> mount(ComponentCollection& c, ReactiveRuntime& r, std::initializer_list<ComponentMount> input) {
    return c.reconcile(r, {input.begin(), input.size()});
}
template<class F> Diagnostic rejects(F&& f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == code, "Expected runtime failure"); return e.diagnostic(); }
    throw std::runtime_error("Expected rejection");
}

void lifecycle_and_publication() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = description();
    std::vector<std::string> events;
    int factories = 0, calculations = 0, setups = 0;
    const auto input = [&](std::string key) {
        return ComponentMount{key, &c, [&](ReactiveRuntime&, const ComponentSetup&) { ++setups; }, {},
            {{"observe", [&, key](const ComponentSetup& setup) {
                check(setups == 2, "All fresh sibling setup precedes effect factories");
                ++factories;
                const auto state = *setup.state<bool>("pinned").value;
                return component_effect<bool>([&, state] { ++calculations; return state.read(); },
                    [&, key](bool value) {
                        events.push_back(key + (value ? ":true" : ":false"));
                        return [&, key] { events.push_back(key + ":close"); };
                    });
            }}}};
    };
    auto a = input("a"), b = input("b");
    check(bool(mount(rows, r, {a, b})) && factories == 2 && calculations == 0 && events.empty(),
        "Mount registers effects without calculation or application");
    auto state = *rows.state<bool>("a", "pinned").value;
    const auto lifetime = rows.find("a")->lifetime;
    r.flush();
    check(calculations == 2 && events.empty(), "Flush only settles calculations");
    check(bool(lower_component(r, c, {})), "Declared effects coexist with ordinary lowering");
    check(r.deliver_effects(EffectPhase::notify).empty() && events.empty(), "No delivery before publication");
    check(r.publish() == 2, "Publication captures both effects");
    r.deliver_effects(EffectPhase::prepare);
    check(events.empty(), "Delivery respects declared phase");
    r.deliver_effects(EffectPhase::notify);
    check(events == std::vector<std::string>{"a:false", "b:false"}, "Delivery follows registration order");
    a.effects["observe"] = [&](const ComponentSetup&) -> ComponentEffectBinding { throw std::runtime_error("must not run"); };
    check(bool(mount(rows, r, {b, a})) && rows.current("a", lifetime) && factories == 2 && setups == 2,
        "Reorder and changed factories retain original registrations");
    r.batch([&] { state.write(true); check(state.read(), "Demand reads see state"); });
    check(events.size() == 2, "Demand reads never deliver partial effects");
    r.flush();
    check(r.publish() == 1, "Changed effect publishes once");
    r.deliver_effects(EffectPhase::notify);
    check(events[2] == "a:close" && events[3] == "a:true", "Replacement closes old application once before new one");
    state.write(true); r.flush(); check(r.publish() == 0, "Equal output suppresses delivery");
    state.write(false); r.flush(); check(r.publish() == 1, "Pending output can be captured");
    check(bool(mount(rows, r, {b})), "Removing a row closes its owner");
    r.deliver_effects(EffectPhase::notify);
    check(events.size() == 4, "Removal suppresses pending effect delivery");
    r.deliver_cleanups(); check(events.back() == "a:close" && events.size() == 5, "Latest application closes once on disposal");
    rejects([&] { state.read(); }, "disposed_reactive_value");
    scope.dispose(); r.deliver_cleanups(); r.deliver_cleanups();
    check(events.back() == "b:close" && events.size() == 6, "Shutdown closes surviving application once");
}

void preflight_and_budgets() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    ComponentMount m{"a", &c, {}, {}, {{"observe", [](const ComponentSetup& s) {
        const auto value = *s.state<bool>("pinned").value;
        return component_effect<bool>([value] { return value.read(); }, [](bool) {});
    }}}};
    check(bool(mount(rows, r, {m})), "Valid declarations mount");
    r.flush(); // Lowering observes a settled graph, even without prop bindings.
    const auto lifetime = rows.find("a")->lifetime;
    m.effects["observe"] = {};
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_binding", "/components/0/effects/observe"), "Empty factories reject before reuse");
    m.effects["observe"] = [](const ComponentSetup&) { return component_effect<bool>([] { return false; }, [](bool) {}); };
    m.effects["unknown"] = m.effects["observe"];
    check(has(mount(rows, r, {m}).diagnostics, "component_unknown_effect", "/components/0/effects/unknown"), "Undeclared factories reject");
    m.effects.erase("unknown");
    c.effects[0].phase = EffectPhase::prepare;
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_schema", "/components/0/effects"), "Changed phase requires explicit kind recreation");
    c.effects[0].phase = EffectPhase::notify; c.effects[0].type = PropertyType::number;
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_schema", "/components/0/effects"), "Changed exact type rejects compatible reuse");
    c.effects[0].type = PropertyType::boolean; c.effects.push_back(c.effects[0]);
    check(has(lower_component(r, c, {}).diagnostics, "component_duplicate_effect", "/component/effects/1/name"), "Lowering validates effect declarations without factories");
    c.effects.pop_back(); c.effects[0].name = "pinned";
    check(bool(lower_component(r, c, {})), "State and effects have independent named declaration schemas");
    c.effects[0].name = "observe"; c.effects[0].phase = static_cast<EffectPhase>(99);
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_phase", "/components/0/effects/0/phase"), "Invalid phase rejects");
    c.effects[0].phase = EffectPhase::notify; c.effects[0].type = static_cast<PropertyType>(99);
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_type", "/components/0/effects/0/type"), "Invalid semantic type rejects");
    c.effects[0].type = PropertyType::boolean; c.effects[0].name = "bad\nname";
    check(has(mount(rows, r, {m}).diagnostics, "component_name", "/components/0/effects/0/name"), "Invalid names reject");
    c.effects[0].name = "observe";
    c.effects.resize(max_component_effects + 1);
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_limit", "/components/0/effects"), "Per-component declarations are bounded");
    c.effects.resize(1);
    for (std::size_t i = 0; i < max_component_effects; ++i) m.effects[std::to_string(i)] = m.effects["observe"];
    check(has(mount(rows, r, {m}).diagnostics, "component_effect_limit", "/components/0/effects"), "Factories are bounded before copying");
    m.effects.clear(); c.local_state.clear(); c.effects.clear();
    for (std::size_t i = 0; i < max_component_effects; ++i) {
        const auto key = std::to_string(i);
        c.effects.push_back({key, PropertyType::boolean, EffectPhase::notify});
        m.effects[key] = [](const ComponentSetup&) { return component_effect<bool>([] { return false; }, [](bool) {}); };
    }
    std::vector<ComponentMount> many(17, m);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].key = std::to_string(i);
    check(has(rows.reconcile(r, many).diagnostics, "component_effect_limit", "/components"), "Aggregate runtime budgets preflight before mutation");
    check(rows.current("a", lifetime) && rows.instances().size() == 1, "All rejected candidates retain accepted owners");
    many.resize(16);
    c.local_state = {{"pinned", PropertyType::boolean, false}};
    check(has(rows.reconcile(r, many).diagnostics, "component_effect_limit", "/components"), "State and effects share the value budget");
    c.local_state.clear(); c.cleanups = {"close"};
    for (auto& item : many) item.cleanups["close"] = [] {};
    check(has(rows.reconcile(r, many).diagnostics, "component_effect_limit", "/components"), "Cleanup and effects share the registration budget");
    c.cleanups.clear();
    ComponentCollection exact(r.owner(r.root(), "exact"));
    check(bool(mount(exact, r, {m})), "Exact per-component effect bound is accepted");
}

void factory_failure_and_runtime_budget() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    int applied = 0;
    ComponentMount m{"a", &c, {}, {}, {{"observe", [&](const ComponentSetup&) {
        return component_effect<double>([] { return 1.0; }, [&](double) { ++applied; });
    }}}};
    rejects([&] { mount(rows, r, {m}); }, "component_effect_type");
    check(rows.instances().empty() && applied == 0, "Type mismatch repairs fresh records without publication");
    m.effects["observe"] = [](const ComponentSetup&) -> ComponentEffectBinding { throw std::runtime_error("factory failed"); };
    const auto diagnostic = rejects([&] { mount(rows, r, {m}); }, "component_effect_factory_failure");
    check(diagnostic.path == "/components/0/effects/observe" && rows.instances().empty(), "Factory exceptions are located and repair records");
    m.effects["observe"] = [](const ComponentSetup&) { return ComponentEffectBinding{}; };
    rejects([&] { mount(rows, r, {m}); }, "component_effect_binding");
    m.effects["observe"] = [&](const ComponentSetup&) { return component_effect<bool>([] { return false; }, [&](bool) { ++applied; }); };
    check(bool(mount(rows, r, {m})) && rows.find("a")->lifetime == 4, "Failed lifetimes stay consumed and registration can retry");
    ComponentMount bad = m; bad.key = "b"; bad.effects["observe"] = {};
    check(!mount(rows, r, {m, bad}) && rows.find("a"), "Preflight retains reused owners");
    bad.effects["observe"] = [](const ComponentSetup&) -> ComponentEffectBinding { throw 7; };
    rejects([&] { mount(rows, r, {m, bad}); }, "component_effect_factory_failure");
    check(rows.instances().size() == 1 && rows.find("a"), "Fresh factory failure preserves reused owners");
    r.flush(); check(r.publish() == 1, "Only accepted live effect survives failed candidate");
    r.deliver_effects(EffectPhase::notify); check(applied == 1, "No failed registration leaves an orphan effect");

    ReactiveRuntime limited;
    ComponentCollection limited_rows(limited.owner(limited.root(), "rows"));
    for (std::size_t i = 0; i < max_reactive_cleanups; ++i) limited.on_cleanup(limited.root(), std::to_string(i), [] {});
    rejects([&] { mount(limited_rows, limited, {m}); }, "reactive_cleanup_limit");
    check(limited_rows.instances().empty(), "Shared runtime budget failure repairs new state and effect records");
}

void invocation_tree_and_typed_delivery() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "tree");
    ComponentTreeCollection tree(scope);
    ComponentInvocation root, child;
    root.component.kind = "container"; root.component.document.root.id = "root";
    root.component.slots = {{"body", "root"}}; root.invocation_slots["body"] = {1};
    child.component = description();
    child.component.effects = {{"text", PropertyType::string, EffectPhase::prepare},
        {"number", PropertyType::number, EffectPhase::notify}, {"ref", PropertyType::reference, EffectPhase::notify}};
    std::vector<ComponentInvocation> v{root, child};
    std::vector<ComponentInvocationMount> m{{"root"}, {"child"}};
    std::uint64_t factory_lifetime = 0;
    std::vector<Property> received;
    const auto apply = [&](const auto& value) { received.emplace_back(value); };
    m[1].effects = {{"text", [&](const ComponentSetup& s) {
        factory_lifetime = s.instance().lifetime;
        return component_effect<std::string>([] { return std::string("hello"); }, apply);
    }}, {"number", [&](const ComponentSetup&) { return component_effect<double>([] { return 2.0; }, apply); }},
        {"ref", [&](const ComponentSetup&) { return component_effect<NodeReference>([] { return NodeReference{"root"}; }, apply); }}};
    const auto accepted = tree.reconcile(r, v, m);
    check(accepted && factory_lifetime == accepted.value->instances[1].instance().lifetime && factory_lifetime == 2,
        "Factories receive tree-issued identities, independent of sibling collection counters");
    std::reverse(v[1].component.effects.begin(), v[1].component.effects.end());
    check(bool(tree.reconcile(r, v, m)), "Declaration reorder preserves effect schema and registration");
    r.flush(); check(bool(lower_component_tree(r, v)) && r.publish() == 3, "Tree effects settle alongside ordinary composed presentation");
    r.deliver_effects(EffectPhase::notify);
    check(received == std::vector<Property>{2.0, NodeReference{"root"}}, "Number and reference payloads retain exact types");
    r.deliver_effects(EffectPhase::prepare); check(std::get<std::string>(received.back()) == "hello", "String effect obeys prepare phase");
    v[0].component.kind = "replace"; m[1].effects["text"] = {};
    check(has(tree.reconcile(r, v, m).diagnostics, "component_effect_binding", "/invocations/1/effects/text"), "Deep invalid binding rejects ancestor replacement");
    m[1].effects["text"] = [](const ComponentSetup&) -> ComponentEffectBinding { throw std::runtime_error("deep factory"); };
    v[0].component.kind = "container"; v[1].component.effects[0].type = PropertyType::boolean;
    check(has(tree.reconcile(r, v, m).diagnostics, "component_effect_schema", "/invocations/1/effects"), "Deep compatible schema preflights before mutation");
    v[1].component.effects[0].type = PropertyType::reference; v[0].component.kind = "replace";
    check(rejects([&] { tree.reconcile(r, v, m); }, "component_effect_factory_failure").path == "/invocations/1/effects/text",
        "Nested factory failure maps diagnostics to invocation index");
    m[1].effects["text"] = [&](const ComponentSetup&) { return component_effect<std::string>([] { return std::string("repaired"); }, apply); };
    const auto repaired = tree.reconcile(r, v, m);
    check(repaired && repaired.value->created.size() == 2 && repaired.value->instances[1].instance().lifetime > factory_lifetime,
        "Tree repair recreates complete fresh subtree without reviving failed lifetimes");
    scope.dispose(); r.deliver_cleanups();
}

void fault_containment_and_application_failure() {
    ReactiveRuntime r;
    ComponentCollection rows(r.boundary(r.root(), "rows"));
    auto c = description();
    bool fail_calculation = true;
    int applied = 0;
    ComponentMount m{"a", &c, {}, {}, {{"observe", [&](const ComponentSetup& setup) {
        const auto pinned = *setup.state<bool>("pinned").value;
        return component_effect<bool>([&, pinned] {
            if (fail_calculation) throw std::runtime_error("unavailable");
            return pinned.read();
        }, [&](bool) { ++applied; });
    }}}};
    check(bool(mount(rows, r, {m})), "Effect calculation failures are evaluated after mounting");
    check(!r.flush().empty() && r.publish() == 0, "Contained calculation failure holds component effects");
    r.deliver_effects(EffectPhase::notify); check(applied == 0, "No failed calculation is externally applied");
    fail_calculation = false; check(r.flush().empty() && r.publish() == 1, "Recovered calculation can publish");
    r.deliver_effects(EffectPhase::notify); check(applied == 1, "Recovery applies accepted value once");

    ReactiveRuntime failures;
    ComponentCollection failed_rows(failures.owner(failures.root(), "rows"));
    c.effects.push_back({"later", PropertyType::boolean, EffectPhase::notify});
    m.effects["observe"] = [](const ComponentSetup&) {
        return component_effect<bool>([] { return false; }, [](bool) { throw std::runtime_error("host failure"); });
    };
    m.effects["later"] = [&](const ComponentSetup&) { return component_effect<bool>([] { return true; }, [&](bool) { ++applied; }); };
    check(bool(mount(failed_rows, failures, {m})), "Application failure fixture mounts");
    failures.flush(); check(failures.publish() == 2, "Both operations publish before application");
    const auto diagnostics = failures.deliver_effects(EffectPhase::notify);
    check(has(diagnostics, "reactive_effect_failed", "/reactive/root/owners/rows/owners/a/effects/observe") && applied == 2,
        "Host application failure is located and later effects continue");
    check(failures.deliver_effects(EffectPhase::notify).empty() && applied == 2, "Failed application is not retried without a new publication");
}
} // namespace

int main() {
    try {
        lifecycle_and_publication(); preflight_and_budgets(); factory_failure_and_runtime_budget(); invocation_tree_and_typed_delivery();
        fault_containment_and_application_failure();
        std::cout << "Component effect checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
