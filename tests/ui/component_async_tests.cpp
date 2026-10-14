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
    c.async = {{"name", PropertyType::string}};
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
void lifecycle() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "rows");
    ComponentCollection rows(scope);
    auto c = description();
    std::vector<std::string> detached;
    int setups = 0;
    std::optional<ComponentSetup> retained;
    ComponentMount a{"a", &c, [&](ReactiveRuntime&, const ComponentSetup& s) {
        ++setups; retained = s;
        check(s.async("name").value->read().status == AsyncStatus::idle, "Declaration creates idle state before setup");
        check(!s.async("missing"), "Unknown setup lookup rejects");
    }, {}, {}, {{"name", [&](const AsyncAttempt& t) { detached.push_back("a:" + t.request()); }}}};
    auto b = a; b.key = "b";
    b.async["name"] = [&](const AsyncAttempt& t) { detached.push_back("b:" + t.request()); };
    check(bool(mount(rows, r, {a, b})) && setups == 2, "Fresh declarations mount automatically");
    const auto lifetime = rows.find("a")->lifetime;
    const auto work = *rows.async("a", "name").value;
    check(!rows.async("missing", "name") && !rows.async("a", "missing"), "Lookup requires mounted key and declared name");
    const auto cached = work.ready_cached("cache", std::string{"old"});
    check(bool(cached), "Declared payload obeys ordinary cache boundary");
    const auto first = *work.begin("first").value;
    const auto latest = *work.begin("latest").value;
    check(latest.detached && latest.detached->request() == "first", "Supersession stays explicit host work");
    check(!work.complete(first.attempt, std::string{"obsolete"}), "Stale attempt rejects");
    check(!work.complete(latest.attempt, false), "Declared exact payload type rejects coercion");
    a.async["name"] = [](const AsyncAttempt&) { throw std::runtime_error("replacement must not run"); };
    check(bool(mount(rows, r, {b, a})) && rows.current("a", lifetime) && setups == 2,
        "Reorder retains original observation and setup");
    check(rows.async("a", "name").value->read() == work.read() && work.read().stale(), "Accepted stale value survives reorder");
    r.flush(); check(bool(lower_component(r, c, {})), "Ordinary lowering accepts declarations without host bindings");
    check(bool(mount(rows, r, {b})) && detached.empty(), "Disposal queues without external delivery");
    check(has(work.complete(latest.attempt, std::string{"late"}).diagnostics, "disposed_reactive_value", "/async/name"),
        "Late completion rejects disposed observation");
    check(r.deliver_cleanups().empty() && detached == std::vector<std::string>{"a:latest"}, "Latest address delivers once with original capture");
    r.deliver_cleanups(); check(detached.size() == 1, "Repeated delivery is empty");
    check(bool(mount(rows, r, {b, a})) && !rows.current("a", lifetime), "Recreated key gets a new lifetime");
    check(rows.async("a", "name").value->read().status == AsyncStatus::idle, "Recreation starts idle");
    check(!rows.async("a", "name").value->complete(latest.attempt, std::string{"late"}), "Old address cannot reach recreated owner");
    const auto ready = *rows.async("b", "name").value;
    const auto token = ready.begin("done").value->attempt;
    check(bool(ready.complete(token, std::string{"value"})), "Terminal completion accepted");
    scope.dispose(); r.deliver_cleanups();
    check(detached.size() == 1, "Idle and ready declarations suppress detachment");
    rejects([&] { retained->async("name").value->read(); }, "disposed_reactive_value");
}
void preflight() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    ComponentMount m{"a", &c, {}, {}, {}, {{"name", [](const AsyncAttempt&) {}}}};
    check(bool(mount(rows, r, {m})), "Initial schema mounts");
    const auto lifetime = rows.find("a")->lifetime;
    m.async.clear();
    check(has(mount(rows, r, {m}).diagnostics, "component_async_binding", "/components/0/async/name"), "Missing binding rejects reuse");
    m.async["name"] = {};
    check(has(mount(rows, r, {m}).diagnostics, "component_async_binding", "/components/0/async/name"), "Empty binding rejects");
    m.async["name"] = [](const AsyncAttempt&) {};
    m.async["unknown/~"] = m.async["name"];
    check(has(mount(rows, r, {m}).diagnostics, "component_unknown_async", "/components/0/async/unknown~1~0"), "Unknown binding has escaped path");
    m.async.erase("unknown/~"); c.async[0].type = PropertyType::boolean;
    check(has(mount(rows, r, {m}).diagnostics, "component_async_schema", "/components/0/async"), "Same kind cannot change payload schema");
    c.async[0].type = static_cast<PropertyType>(99);
    check(has(mount(rows, r, {m}).diagnostics, "component_async_type", "/components/0/async/0/type"), "Invalid type rejects before allocation");
    c.async[0].type = PropertyType::string; c.async.push_back(c.async[0]);
    r.flush();
    check(has(lower_component(r, c, {}).diagnostics, "component_duplicate_async", "/component/async/1/name"), "Lowering validates declarations");
    c.async.pop_back(); c.local_state = {{"name", PropertyType::boolean, false}};
    check(has(mount(rows, r, {m}).diagnostics, "component_async_name_conflict", "/components/0/async/0/name"), "Async and state share value names");
    c.local_state.clear(); c.cleanups = {"name"}; m.cleanups["name"] = [] {};
    check(has(mount(rows, r, {m}).diagnostics, "component_async_name_conflict", "/components/0/async/0/name"), "Async detachment and cleanup share names");
    c.cleanups.clear(); m.cleanups.clear(); c.async[0].name = "bad\n";
    check(has(mount(rows, r, {m}).diagnostics, "component_name", "/components/0/async/0/name"), "Invalid names reject");
    c.async.resize(max_component_async + 1);
    check(has(mount(rows, r, {m}).diagnostics, "component_async_limit", "/components/0/async"), "Declaration count is bounded");
    c.async.clear(); m.async.clear();
    for (std::size_t i = 0; i < max_component_async; ++i) {
        const auto name = std::to_string(i);
        c.async.push_back({name, PropertyType::boolean}); m.async[name] = [](const AsyncAttempt&) {};
    }
    auto excessive = m; excessive.async["extra"] = [](const AsyncAttempt&) {};
    check(has(mount(rows, r, {excessive}).diagnostics, "component_async_limit", "/components/0/async"), "Binding count bounded before copies");
    std::vector<ComponentMount> many(17, m);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].key = std::to_string(i);
    check(has(rows.reconcile(r, many).diagnostics, "component_async_limit", "/components"), "Aggregate observations fit runtime budgets");
    many.resize(16); c.local_state = {{"pinned", PropertyType::boolean, false}};
    check(has(rows.reconcile(r, many).diagnostics, "component_async_limit", "/components"), "State and async share aggregate value budget");
    c.local_state.clear(); c.cleanups = {"close"};
    for (auto& entry : many) entry.cleanups["close"] = [] {};
    check(has(rows.reconcile(r, many).diagnostics, "component_async_limit", "/components"), "Cleanup and detachment share aggregate budget");
    c.cleanups.clear();
    c.effects = {{"observe", PropertyType::boolean, EffectPhase::notify}};
    for (auto& entry : many) {
        entry.cleanups.clear();
        entry.effects["observe"] = [](const ComponentSetup&) {
            return component_effect<bool>([] { return false; }, [](bool) {});
        };
    }
    check(has(rows.reconcile(r, many).diagnostics, "component_async_limit", "/components"), "Effects share async value and detachment budgets");
    check(rows.current("a", lifetime) && rows.instances().size() == 1, "Every invalid candidate leaves accepted owners intact");
}
void declaration_order_and_registration_budget() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    c.async = {{"flag", PropertyType::boolean}, {"number", PropertyType::number},
        {"text", PropertyType::string}, {"reference", PropertyType::reference}};
    ComponentMount m{"a", &c};
    for (const auto& declaration : c.async) m.async[declaration.name] = [](const AsyncAttempt&) {};
    check(bool(mount(rows, r, {m})), "All exact primitive schemas mount");
    check(bool(rows.async("a", "flag").value->ready_cached("cache", true)) &&
        bool(rows.async("a", "number").value->ready_cached("cache", 2.0)) &&
        bool(rows.async("a", "text").value->ready_cached("cache", std::string{"text"})) &&
        bool(rows.async("a", "reference").value->ready_cached("cache", NodeReference{"target"})), "Exact payloads accepted");
    std::reverse(c.async.begin(), c.async.end());
    check(bool(mount(rows, r, {m})) && rows.async("a", "text").value->read().value->revision == 1,
        "Declaration order does not identify an accepted observation");
    r.flush(); check(bool(lower_component(r, c, {})), "Reference payload existence belongs to eventual document use");

    ReactiveRuntime limited;
    const auto external = limited.owner(limited.root(), "host");
    for (std::size_t i = 0; i < max_reactive_cleanups - 1; ++i)
        limited.on_cleanup(external, std::to_string(i), [] {});
    ComponentCollection candidate(limited.owner(limited.root(), "rows"));
    c.async.resize(2); m.component = &c; m.async.clear();
    for (const auto& declaration : c.async) m.async[declaration.name] = [](const AsyncAttempt&) {};
    rejects([&] { mount(candidate, limited, {m}); }, "reactive_cleanup_limit");
    check(candidate.instances().empty(), "Runtime budget failure releases every fresh owner and repairs records");
    external.dispose(); limited.deliver_cleanups();
    check(bool(mount(candidate, limited, {m})), "Explicit delivery releases budget for subsequent mounting");
}
void failure_repair() {
    ReactiveRuntime r;
    ComponentCollection rows(r.owner(r.root(), "rows"));
    auto c = description();
    std::optional<AsyncValue> failed;
    int detached = 0;
    ComponentMount m{"a", &c, [&](ReactiveRuntime&, const ComponentSetup& s) {
        failed = *s.async("name").value;
        check(bool(failed->begin("failed")), "Setup can start declared observation");
        throw std::runtime_error("bad setup");
    }, {}, {}, {{"name", [&](const AsyncAttempt& t) { check(t.request() == "failed", "Correct failed setup address"); ++detached; }}}};
    check(rejects([&] { mount(rows, r, {m}); }, "component_setup_failure").path == "/components/0/setup", "Setup failure is located");
    check(rows.instances().empty() && detached == 0, "Failed setup repairs records and queues work");
    rejects([&] { failed->read(); }, "disposed_reactive_value");
    check(r.deliver_cleanups().empty() && detached == 1, "Detachment was registered before failing setup");
    m.setup = {}; check(bool(mount(rows, r, {m})), "Collection recovers after setup failure");
    auto fresh = c; fresh.async[0].name = "new";
    ComponentMount other{"b", &fresh, {}, {}, {}, {{"new", [](const AsyncAttempt&) {}}}};
    // Force a live runtime cleanup-name collision from a separate declaration.
    fresh.cleanups = {"other"}; other.cleanups["other"] = [] {};
    other.setup = [](ReactiveRuntime& runtime, const ComponentSetup& s) {
        runtime.on_cleanup(s.instance().owner, "new", [] {});
    };
    rejects([&] { mount(rows, r, {m, other}); }, "duplicate_reactive_name");
    check(rows.find("a") && !rows.find("b"), "Runtime registration failure preserves compatible owner only");
    r.deliver_cleanups(); other.setup = {};
    check(bool(mount(rows, r, {m, other})), "Registration failure can be repaired");
}
void invocation_tree() {
    ReactiveRuntime r;
    ComponentTreeCollection tree(r.owner(r.root(), "tree"));
    ComponentInvocation parent;
    parent.component.kind = "list"; parent.component.document.root.id = "list";
    parent.component.slots = {{"rows", "list", NodeKinds::all}};
    parent.invocation_slots["rows"] = {1, 2};
    ComponentInvocation child{description(), {}, {}};
    std::vector<ComponentInvocation> invocations{parent, child, child};
    int detached = 0, setups = 0;
    const auto input = [&](std::string key) {
        return ComponentInvocationMount{key, [&](ReactiveRuntime&, const ComponentSetup& s) {
            ++setups; check(bool(s.async("name").value->begin("work")), "Tree setup sees declarations");
        }, {}, {}, {{"name", [&](const AsyncAttempt&) { ++detached; }}}};
    };
    std::vector<ComponentInvocationMount> mounts{{"root"}, input("a"), input("b")};
    auto first = tree.reconcile(r, invocations, mounts);
    check(bool(first) && setups == 2, "Tree mounts parent and declared child observations");
    const auto old = *first.value->instances[1].async("name").value;
    const auto lifetime = first.value->instances[1].instance().lifetime;
    invocations[0].invocation_slots["rows"] = {2, 1};
    auto reordered = tree.reconcile(r, invocations, mounts);
    check(bool(reordered) && reordered.value->instances[1].instance().lifetime == lifetime && setups == 2,
        "Tree retains scoped identity and observation on reorder");
    invocations[1].component.kind = "replacement"; invocations[2].component.async[0].type = PropertyType::number;
    check(has(tree.reconcile(r, invocations, mounts).diagnostics, "component_async_schema", "/invocations/2/async"),
        "Deep incompatible schema rejects before replacing other branch");
    check(old.read().status == AsyncStatus::loading && detached == 0, "Deep rejection leaves previous work live");
    invocations[2].component.async[0].type = PropertyType::string;
    mounts[1].setup = [&](ReactiveRuntime&, const ComponentSetup& s) {
        check(bool(s.async("name").value->begin("failure")), "Replacement starts work");
        throw std::runtime_error("replacement failed");
    };
    check(rejects([&] { tree.reconcile(r, invocations, mounts); }, "component_setup_failure").path == "/invocations/1/setup",
        "Tree failure keeps invocation path");
    rejects([&] { old.read(); }, "disposed_reactive_value");
    check(detached == 0 && r.deliver_cleanups().empty() && detached == 2, "Replaced and failed fresh lifetimes each detach once");
    mounts[1] = input("a");
    auto repaired = tree.reconcile(r, invocations, mounts);
    check(bool(repaired) && repaired.value->instances[1].instance().lifetime != lifetime, "Tree repairs failed branch with fresh observation");
    r.flush(); check(bool(lower_component_tree(r, invocations)), "Tree declaration lowering follows ordinary pipeline");
    check(bool(tree.reconcile(r, {}, {})) && r.deliver_cleanups().empty() && detached == 4, "Shutdown detaches both surviving observations");
}
} // namespace
int main() {
    try {
        lifecycle(); preflight(); declaration_order_and_registration_budget(); failure_repair(); invocation_tree();
        std::cout << "Declared component async lifecycle, schema, budgets and failure repair agree.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
