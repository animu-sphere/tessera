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
    c.cleanups = {"first", "second"};
    return c;
}
Result<KeyedUpdate> mount(ComponentCollection& c, ReactiveRuntime& r, std::initializer_list<ComponentMount> input) {
    return c.reconcile(r, {input.begin(), input.size()});
}
template<class F> void rejects(F&& f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == code, "Expected runtime failure"); return; }
    throw std::runtime_error("Expected rejection");
}

void reuse_disposal_and_bindings() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = description();
    std::vector<std::string> closed;
    int setups = 0;
    const auto input = [&](std::string key) {
        return ComponentMount{key, &c, [&](ReactiveRuntime&, const ComponentSetup&) { ++setups; },
            {{"first", [&, key] { closed.push_back(key + ":first"); }},
             {"second", [&, key] { closed.push_back(key + ":second"); }}}};
    };
    auto a = input("a"), b = input("b");
    auto accepted = mount(rows, r, {a, b});
    check(accepted && setups == 2 && closed.empty(), "Mount registers but invokes no cleanup");
    auto pinned = *rows.state<bool>("a", "pinned").value;
    pinned.write(true);
    const auto lifetime = rows.find("a")->lifetime;
    std::reverse(c.cleanups.begin(), c.cleanups.end());
    a.cleanups["first"] = [&] { closed.push_back("replacement must not run"); };
    check(bool(mount(rows, r, {b, a})) && setups == 2 && rows.current("a", lifetime) && pinned.read(),
        "Reorder retains state, lifetime and original callbacks by name");
    a.cleanups.erase("first");
    check(has(mount(rows, r, {b, a}).diagnostics, "component_cleanup_binding", "/components/1/cleanups/first") && rows.current("a", lifetime),
        "Missing bindings reject even compatible reuse");
    a = input("a");
    c.cleanups.push_back("third"); a.cleanups["third"] = [] {}; b.cleanups["third"] = [] {};
    check(has(mount(rows, r, {b, a}).diagnostics, "component_cleanup_schema", "/components/1/cleanups") && closed.empty(),
        "Same-kind schema changes reject before disposal");
    c.cleanups.pop_back(); a.cleanups.erase("third"); b.cleanups.erase("third");
    check(bool(mount(rows, r, {a})) && closed.empty(), "Removal queues operations for host delivery");
    check(r.deliver_cleanups().empty() && closed == std::vector<std::string>{"b:first", "b:second"},
        "Removal uses original registration order despite declaration reorder");
    c.kind = "replacement";
    check(bool(mount(rows, r, {a})) && !rows.current("a", lifetime), "Kind replacement recreates lifetime");
    rejects([&] { pinned.read(); }, "disposed_reactive_value");
    r.deliver_cleanups();
    check(closed == std::vector<std::string>{"b:first", "b:second", "a:first", "a:second"}, "Old operations close once");
    scope.dispose(); r.deliver_cleanups(); r.deliver_cleanups();
    check(closed.size() == 6 && closed[4] == "a:second" && closed[5] == "a:first", "Fresh declarations determine new registration order");
}

void preflight_and_lowering() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    ComponentMount m{"a", &c, {}, {{"first", [] {}}, {"second", [] {}}}};
    m.cleanups["first"] = {};
    check(has(mount(rows, r, {m}).diagnostics, "component_cleanup_binding", "/components/0/cleanups/first"), "Empty operations reject");
    m.cleanups["first"] = [] {}; m.cleanups["unknown"] = [] {};
    check(has(mount(rows, r, {m}).diagnostics, "component_unknown_cleanup", "/components/0/cleanups/unknown"), "Unknown bindings reject");
    m.cleanups.erase("unknown"); c.cleanups.push_back("first");
    check(has(mount(rows, r, {m}).diagnostics, "component_duplicate_cleanup", "/components/0/cleanups/2"), "Duplicate declarations reject");
    check(has(lower_component(r, c, {}).diagnostics, "component_duplicate_cleanup", "/component/cleanups/2"), "Lowering shares declaration validation");
    c.cleanups.back() = std::string("bad\nname");
    check(has(mount(rows, r, {m}).diagnostics, "component_name", "/components/0/cleanups/2"), "Invalid names reject");
    c.cleanups.clear(); m.cleanups.clear();
    for (std::size_t i = 0; i < max_component_cleanups; ++i) {
        c.cleanups.push_back(std::to_string(i)); m.cleanups[std::to_string(i)] = [] {};
    }
    check(bool(lower_component(r, c, {})) && bool(mount(rows, r, {m})), "Exact per-component bound is accepted without lowering callbacks");
    c.cleanups.push_back("overflow");
    check(has(mount(rows, r, {m}).diagnostics, "component_cleanup_limit", "/components/0/cleanups"), "Excess declarations reject");
    c.cleanups.pop_back(); m.cleanups["overflow"] = [] {};
    check(has(mount(rows, r, {m}).diagnostics, "component_cleanup_limit", "/components/0/cleanups"), "Excess bindings reject");
    m.cleanups.erase("overflow");
    std::vector<ComponentMount> many(17, m);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].key = std::to_string(i);
    check(has(rows.reconcile(r, many).diagnostics, "component_cleanup_limit", "/components"), "Aggregate bounds reject before owner changes");
    check(rows.find("a") && rows.instances().size() == 1, "Every rejected candidate preserves accepted owners");
    r.root().dispose(); r.deliver_cleanups();
}

void tree_preflight_and_failure() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "tree");
    ComponentTreeCollection tree(scope);
    ComponentInvocation root, child;
    root.component.kind = "container"; root.component.document.root.id = "root";
    root.component.slots = {{"body", "root"}}; root.invocation_slots["body"] = {1};
    child.component = description();
    root.component.cleanups = {"close"};
    std::vector<ComponentInvocation> v{root, child};
    std::vector<std::string> closed;
    std::vector<ComponentInvocationMount> m{{"root", {}, {{"close", [&] { closed.push_back("parent"); }}}},
        {"child", {}, {{"first", [&] { closed.push_back("child:first"); }}, {"second", [&] { closed.push_back("child:second"); }}}}};
    auto accepted = tree.reconcile(r, v, m);
    check(bool(accepted), "Declared operations mount throughout invocation tree");
    const auto old = accepted.value->instances[1].instance().lifetime;
    v[0].component.kind = "replace"; m[1].cleanups.erase("first");
    check(has(tree.reconcile(r, v, m).diagnostics, "component_cleanup_binding", "/invocations/1/cleanups/first") && closed.empty(),
        "Deep invalid binding rejects before ancestor replacement");
    m[1].cleanups["first"] = [&] { closed.push_back("child:first"); }; v[0].component.kind = "container";
    v[1].component.cleanups = {"first"}; m[1].cleanups.erase("second");
    check(has(tree.reconcile(r, v, m).diagnostics, "component_cleanup_schema", "/invocations/1/cleanups"), "Deep schema change rejects before mutation");
    v[1].component.cleanups = {"first", "second"}; m[1].cleanups["second"] = [&] { closed.push_back("child:second"); };
    v[0].component.kind = "replace";
    m[1].setup = [](ReactiveRuntime&, const ComponentSetup&) { throw std::runtime_error("setup failed"); };
    rejects([&] { tree.reconcile(r, v, m); }, "component_setup_failure");
    check(closed.empty(), "Failed setup only queues declared operations");
    r.deliver_cleanups();
    check(closed == std::vector<std::string>{"child:first", "child:second", "parent", "child:first", "child:second", "parent"},
        "Replaced and failed subtrees close child before parent exactly once");
    m[1].setup = {};
    const auto repaired = tree.reconcile(r, v, m);
    check(repaired && repaired.value->created.size() == 2 && repaired.value->instances[1].instance().lifetime > old,
        "Failed setup repairs records and never revives old lifetimes");
    scope.dispose(); r.deliver_cleanups(); check(closed.size() == 9, "Repaired subtree owns new cleanup registrations");
}

void registration_budget_and_delivery_failure() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = description();
    int invoked = 0;
    ComponentMount m{"a", &c, {}, {{"first", [&] { ++invoked; }}, {"second", [] {}}}};
    for (std::size_t i = 0; i < max_reactive_cleanups - 1; ++i) r.on_cleanup(r.root(), std::to_string(i), [] {});
    rejects([&] { mount(rows, r, {m}); }, "reactive_cleanup_limit");
    check(rows.instances().empty() && invoked == 0, "Partial registration failure disposes fresh owners and repairs records");
    r.deliver_cleanups(); check(invoked == 1, "Partially registered operation is delivered once");
    r.root().dispose(); r.deliver_cleanups();

    ReactiveRuntime failures;
    ComponentCollection failed_rows(failures.owner(failures.root(), "rows"));
    m.cleanups["first"] = [] { throw std::runtime_error("host cleanup failed"); };
    m.cleanups["second"] = [&] { ++invoked; };
    check(bool(mount(failed_rows, failures, {m})) && bool(mount(failed_rows, failures, {})), "Failure fixture mounts and removes normally");
    const auto diagnostics = failures.deliver_cleanups();
    check(has(diagnostics, "reactive_cleanup_failed", "/reactive/root/owners/rows/owners/a/cleanups/first") && invoked == 2,
        "Host failure is located and delivery continues");
    check(failures.deliver_cleanups().empty() && invoked == 2, "Failed callbacks are never retried");
}
} // namespace

int main() {
    try {
        reuse_disposal_and_bindings(); preflight_and_lowering(); tree_preflight_and_failure(); registration_budget_and_delivery_failure();
        std::cout << "Component cleanup checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
