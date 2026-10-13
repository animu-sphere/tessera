#include <tessera/ui/component_tree_mount.hpp>
#include <tessera/ui/async.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/input/focus.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
template<class F> void rejects(F&& f, std::string_view code) {
    try { f(); } catch (const ReactiveError& e) { check(e.diagnostic().code == code, "Expected reactive error"); return; }
    throw std::runtime_error("Expected rejection");
}
ComponentInvocation box(std::string id) {
    ComponentInvocation v;
    v.component.kind = "container"; v.component.document.root.id = id;
    v.component.slots = {{"body", id, NodeKinds::all}};
    return v;
}
std::vector<ComponentInvocation> fixture() {
    auto root = box("root"); root.invocation_slots["body"] = {1, 2};
    auto branch = box("branch"); branch.invocation_slots["body"] = {3};
    auto other = box("other"); other.invocation_slots["body"] = {4};
    ComponentInvocation a;
    a.component.kind = "row"; a.component.document.root.id = "a";
    a.component.document.root.kind = NodeKind::text;
    a.component.document.root.properties["text"] = std::string("placeholder");
    a.component.document.root.properties["focusable"] = true;
    a.component.local_state = {{"pinned", PropertyType::boolean, false}};
    a.component.props = {{"caption", PropertyType::string, std::string("Row")}};
    a.component.properties = {{"caption", "a", "text"}};
    auto b = a; b.component.document.root.id = "b"; b.component.properties[0].target = "b";
    return {root, branch, other, a, b};
}
std::vector<ComponentInvocationMount> keys() { return {{"root"}, {"branch"}, {"other"}, {"same"}, {"same"}}; }

void lifecycle_and_presentation() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "tree");
    ComponentTreeCollection tree(scope);
    auto v = fixture(); auto m = keys();
    std::map<std::uint64_t, AsyncValue> resources;
    std::map<std::uint64_t, Computed<std::string>> captions;
    std::vector<std::uint64_t> detached;
    int evaluated = 0, notified = 0;
    const auto setup = [&](ReactiveRuntime& runtime, const ComponentSetup& context) {
        const auto lifetime = context.instance().lifetime;
        AsyncValue name(runtime, context.instance().owner, "name", PropertyType::string);
        check(bool(name.ready_cached("name", std::string("Row"))), "Cache initializes row observation");
        const auto pinned = *context.state<bool>("pinned").value;
        auto caption = runtime.computed<std::string>(context.instance().owner, "caption", [&, name, pinned] {
            ++evaluated;
            return std::get<std::string>(name.read().value->value) + (pinned.read() ? " pinned" : "");
        });
        runtime.effect<std::string>(context.instance().owner, "notice", EffectPhase::notify,
            [caption] { return caption.read(); }, [&](const std::string&) { ++notified; });
        runtime.on_cleanup(context.instance().owner, "detach", [&, lifetime] { detached.push_back(lifetime); });
        resources.emplace(lifetime, name); captions.emplace(lifetime, caption);
    };
    m[3].setup = setup; m[4].setup = setup;
    const auto mounted = tree.reconcile(r, v, m);
    check(mounted && mounted.value->instances.size() == 5 && mounted.value->created == std::vector<std::size_t>{0, 1, 2, 3, 4},
          "Parent and sibling setup order follows the ordinary collections");
    const auto old = mounted.value->instances[3];
    const auto a = old.instance().lifetime, b = mounted.value->instances[4].instance().lifetime;
    check(a != b, "Equal keys in independent parent slots have distinct tree lifetimes");
    old.state<bool>("pinned").value->write(true);
    const auto pending = resources.at(a).begin("new-name");
    check(bool(pending), "Async update begins on row owner");
    for (const auto i : {3u, 4u}) v[i].arguments.props["caption"] = captions.at(mounted.value->instances[i].instance().lifetime);
    r.flush();
    const auto bound = lower_component_tree(r, v);
    check(bound && evaluated == 2 && notified == 0, "Mounting and lowering leave host effect delivery explicit");
    const auto ui = UiTree::create(bound.value->document);
    const StyleSheet sheet;
    const auto styles = resolve_styles({ui.value->get(), &sheet});
    PlaceholderTextShaper text;
    const auto layout = compute_layout({ui.value->get(), *styles.value, {200, 100}, &text});
    const auto semantics = capture_semantics({ui.value->get(), *styles.value, &*layout.value}, 1);
    check(semantics && bool(build_paint_list({ui.value->get(), *styles.value, &*layout.value, &text})), "Mounted tree uses ordinary full presentation");
    FocusDispatcher focus;
    check(bool(focus.focus({ui.value->get(), *styles.value, &*layout.value}, *(*ui.value)->find("a"))), "Row receives ordinary focus");
    r.publish(); r.deliver_effects(EffectPhase::notify);
    check(notified == 2, "Only accepted publication delivers effects");
    v[0].invocation_slots["body"] = {2, 1};
    auto reordered = tree.reconcile(r, v, m);
    check(reordered && reordered.value->created.empty() && reordered.value->instances[3].instance().lifetime == a &&
          reordered.value->instances[3].state<bool>("pinned").value->read(), "Parent reorder retains subtree state and work");
    r.flush();
    const auto reordered_bound = lower_component_tree(r, v);
    const auto reordered_ui = UiTree::create(reordered_bound.value->document);
    const auto reordered_styles = resolve_styles({reordered_ui.value->get(), &sheet});
    const auto reordered_layout = compute_layout({reordered_ui.value->get(), *reordered_styles.value, {200, 100}, &text});
    const auto focused = focus.refresh({reordered_ui.value->get(), *reordered_styles.value, &*reordered_layout.value});
    check(focused && focused.value->focused == (*reordered_ui.value)->find("a"), "Flattened author identity preserves ordinary focus across reorder");
    // Input indices and root index are ephemeral; identity follows scoped keys.
    std::swap(v[0], v[1]); std::swap(m[0], m[1]);
    v[1].invocation_slots["body"] = {2, 0};
    reordered = tree.reconcile(r, v, m, 1);
    check(reordered && reordered.value->created.empty() && reordered.value->instances[3].instance().lifetime == a,
          "Array/root index changes preserve scoped identity");
    v[0].component.kind = "new-container";
    const auto replaced = tree.reconcile(r, v, m, 1);
    check(replaced && replaced.value->instances[3].instance().lifetime != a && replaced.value->instances[4].instance().lifetime == b &&
          !replaced.value->instances[3].state<bool>("pinned").value->read(), "Parent replacement recreates descendants but retains other branches");
    rejects([&] { old.state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    check(!resources.at(a).complete(pending.value->attempt, std::string("Late")) && detached.empty(), "Disposed subtree rejects old async completion before cleanup delivery");
    r.deliver_cleanups();
    check(detached == std::vector<std::uint64_t>{a}, "Subtree detachment delivers once after reconciliation");
    check(bool(tree.reconcile(r, {}, {})), "Empty candidate unmounts the complete tree");
    r.deliver_cleanups(); check(detached.size() == 3, "All row cleanup registrations close exactly once");
    const auto fresh = tree.reconcile(r, v, m, 1);
    check(fresh && fresh.value->instances[3].instance().lifetime > replaced.value->instances[3].instance().lifetime,
          "Recreated slots cannot reuse public tree lifetimes");
    scope.dispose(); r.deliver_cleanups();
    rejects([&] { tree.reconcile(r, {}, {}); }, "disposed_reactive_owner");
}

void preflight_and_failure_repair() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "tree");
    ComponentTreeCollection tree(scope);
    auto v = fixture(); auto m = keys();
    auto accepted = tree.reconcile(r, v, m);
    check(bool(accepted), "Initial tree mounts");
    const auto old = accepted.value->instances[3];
    old.state<bool>("pinned").value->write(true);
    int setups = 0;
    m[0].key = "replacement"; m[0].setup = [&](ReactiveRuntime&, const ComponentSetup&) { ++setups; };
    v[4].component.local_state.push_back(v[4].component.local_state.front());
    check(has(tree.reconcile(r, v, m).diagnostics, "component_duplicate_state", "/invocations/4/local_state/1/name") && setups == 0,
          "Invalid deep declaration rejects before any root replacement");
    v = fixture(); m = keys();
    v[4].component.local_state[0].name = "renamed";
    check(has(tree.reconcile(r, v, m).diagnostics, "component_state_schema", "/invocations/4/local_state") && old.state<bool>("pinned").value->read(),
          "Deep same-kind schema changes reject the entire candidate");
    v = fixture(); v[0].invocation_slots["body"] = {1, 1};
    check(has(tree.reconcile(r, v, m).diagnostics, "component_invocation_reuse", "/invocations/0/slots/body/1"), "Shared invocation records reject");
    v = fixture(); m[2].key = m[1].key;
    check(has(tree.reconcile(r, v, m).diagnostics, "duplicate_key", "/invocations/2/key"), "Sibling duplicates reject while cross-parent keys are allowed");
    m = keys();
    check(has(tree.reconcile(r, v, std::span(m).first(1)).diagnostics, "component_mount_count", "/invocations"), "Parallel mount count validates");
    ReactiveRuntime foreign;
    rejects([&] { tree.reconcile(foreign, v, m); }, "foreign_reactive_owner");
    std::vector<ComponentSetup> failed;
    std::vector<std::string> closed;
    v[1].component.kind = "replace";
    m[1].setup = [&](ReactiveRuntime& runtime, const ComponentSetup& context) {
        check(has(tree.reconcile(runtime, v, m).diagnostics, "component_setup_reentry", "/invocations"), "Recursive tree entry rejects");
        failed.push_back(context);
        runtime.on_cleanup(context.instance().owner, "close", [&] { closed.push_back("parent"); });
    };
    m[3].setup = [&](ReactiveRuntime& runtime, const ComponentSetup& context) {
        failed.push_back(context);
        runtime.on_cleanup(context.instance().owner, "close", [&] { closed.push_back("child"); });
        throw std::runtime_error("Synthetic nested failure");
    };
    try { tree.reconcile(r, v, m); check(false, "Nested setup must throw"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "component_setup_failure" && e.diagnostic().path == "/invocations/3/setup", "Setup errors locate the input invocation"); }
    check(failed.size() == 2 && closed.empty(), "Failed nested registration defers cleanup");
    rejects([&] { old.state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    rejects([&] { failed.back().state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    r.deliver_cleanups();
    check(closed == std::vector<std::string>{"child", "parent"}, "Failure closes every fresh lifetime with child-first cleanup");
    m[3].setup = {};
    const auto retried = tree.reconcile(r, v, m);
    check(retried && retried.value->instances[4].instance().lifetime == accepted.value->instances[4].instance().lifetime &&
          retried.value->instances[3].instance().lifetime > failed.back().instance().lifetime, "Retry repairs records and preserves healthy reused branches");
    scope.dispose(); r.deliver_cleanups();
}

void slot_identity_and_runtime_limits() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "tree");
    ComponentTreeCollection tree(scope);
    auto v = fixture();
    // Two independent collections beneath one component, with equal row keys.
    UiNode left; left.id = "left";
    UiNode right; right.id = "right";
    v[0].component.document.root.children = {left, right};
    v[0].component.slots = {{"left", "left", NodeKinds::all}, {"right", "right", NodeKinds::all}};
    v[0].invocation_slots = {{"left", {1}}, {"right", {2}}};
    auto m = keys(); m[1].key = "same"; m[2].key = "same";
    const auto first = tree.reconcile(r, v, m);
    check(first && first.value->instances[1].instance().lifetime != first.value->instances[2].instance().lifetime,
          "Slot names scope sibling keys beneath the same parent");
    std::swap(v[0].component.slots[0], v[0].component.slots[1]);
    check(tree.reconcile(r, v, m).value->created.empty(), "Reordering slot declarations preserves named slot owners");
    m[1].key = "left-branch"; m[2].key = "right-branch";
    const auto named = tree.reconcile(r, v, m);
    const auto previous = named.value->instances[3];
    previous.state<bool>("pinned").value->write(true);
    std::swap(v[0].invocation_slots["left"], v[0].invocation_slots["right"]);
    const auto moved = tree.reconcile(r, v, m);
    check(moved && moved.value->instances[3].instance().lifetime != previous.instance().lifetime &&
          !moved.value->instances[3].state<bool>("pinned").value->read(), "Moving a subtree between slots recreates its scoped identity");
    rejects([&] { previous.state<bool>("pinned").value->read(); }, "disposed_reactive_value");
    check(bool(tree.reconcile(r, {}, {})), "Clear releases the tree before limit fixture");
    // Slot scopes count toward the runtime's ordinary owner-depth budget.
    std::vector<ComponentInvocation> deep;
    std::vector<ComponentInvocationMount> deep_mounts;
    for (std::size_t i = 0; i < max_reactive_depth; ++i) {
        deep.push_back(box("node-" + std::to_string(i)));
        deep_mounts.push_back({"node"});
        if (i) deep[i - 1].invocation_slots["body"] = {i};
    }
    std::vector<ComponentSetup> partial;
    deep_mounts[0].setup = [&](ReactiveRuntime&, const ComponentSetup& context) { partial.push_back(context); };
    bool rejected = false;
    try { tree.reconcile(r, deep, deep_mounts); }
    catch (const ReactiveError& e) { rejected = e.diagnostic().code == "reactive_owner_limit"; }
    check(rejected && partial.size() == 1, "Ordinary runtime depth limit rejects and repairs partially mounted trees");
    check(bool(tree.reconcile(r, v, m)), "A shallow tree mounts after limit failure with all fresh names released");
    m[0].setup = [&](ReactiveRuntime&, const ComponentSetup&) { scope.dispose(); };
    m[0].key = "replace-root";
    rejects([&] { tree.reconcile(r, v, m); }, "disposed_reactive_owner");
}
} // namespace
int main() {
    try { lifecycle_and_presentation(); preflight_and_failure_repair(); slot_identity_and_runtime_limits();
        std::cout << "Invocation mounting: scoped identity, nested state, disposal and failure repair agree.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
