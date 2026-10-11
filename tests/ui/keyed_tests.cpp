#include <tessera/ui/keyed.hpp>
#include "../check.hpp"
#include <iostream>
#include <map>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
template<class F>
void rejects(F f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) {
        check(e.diagnostic().code == code, "Runtime failure must keep its reactive diagnostic");
        return;
    }
    throw std::runtime_error("Expected a reactive failure: " + std::string(code));
}

std::vector<std::string> keys(const std::vector<KeyedInstance>& instances) {
    std::vector<std::string> result;
    for (const auto& instance : instances) result.push_back(instance.key);
    return result;
}

// Each created instance gets local state, a binding over host props, and a cleanup.
struct Rows {
    ReactiveRuntime& runtime;
    KeyedCollection rows;
    std::map<std::uint64_t, Signal<bool>> expanded;
    std::map<std::uint64_t, Computed<std::string>> label;
    std::vector<std::string> closed;
    int evaluations = 0;

    Result<KeyedUpdate> apply(const std::vector<KeyedChild>& children, const Signal<int>& count) {
        auto update = rows.reconcile(runtime, children);
        if (!update) return update;
        for (const auto i : update.value->created) {
            const auto& row = update.value->instances[i];
            const auto local = runtime.signal(row.owner, "expanded", false);
            expanded[row.lifetime] = local;
            label[row.lifetime] = runtime.computed<std::string>(row.owner, "label", [this, local, count, key = row.key] {
                ++evaluations;
                return key + (local.read() ? " (open)" : "") + " x" + std::to_string(count.read());
            });
            runtime.on_cleanup(row.owner, "subscription", [this, key = row.key] { closed.push_back(key); });
        }
        return update;
    }
};

void reorder_preserves_instances() {
    ReactiveRuntime r;
    auto count = r.signal(r.root(), "count", 1);
    Rows rows{r, KeyedCollection(r.owner(r.root(), "rows"))};
    const auto first = rows.apply({{"a", "item"}, {"b", "item"}, {"c", "item"}}, count);
    check(first && first.value->created == std::vector<std::size_t>{0, 1, 2} && first.value->disposed.empty(),
          "An empty collection creates every child in candidate order");
    check(first.value->instances[0].lifetime == 1 && first.value->instances[2].lifetime == 3,
          "Lifetimes are allocated in candidate order");
    r.flush();
    check(rows.evaluations == 3, "Initial bindings evaluate once each");
    const auto b = rows.rows.find("b")->lifetime;
    rows.expanded[b].write(true);
    r.flush();
    check(rows.label[b].read() == "b (open) x1" && rows.evaluations == 4, "Local state drives its row binding");

    const auto moved = rows.apply({{"c", "item"}, {"b", "item"}, {"a", "item"}}, count);
    check(moved && moved.value->created.empty() && moved.value->disposed.empty(),
          "A compatible reorder neither creates nor disposes instances");
    check(keys(rows.rows.instances()) == std::vector<std::string>{"c", "b", "a"} &&
          rows.rows.find("b")->lifetime == b, "Reorder keeps the logical instance and candidate order");
    r.flush();
    check(rows.evaluations == 4 && rows.expanded[b].read() && rows.expanded[b].revision() == 2,
          "Reorder preserves local state and recalculates no binding");
    check(r.deliver_cleanups().empty() && rows.closed.empty(), "Reorder queues no cleanup");
    count.write(2);
    r.flush();
    check(rows.evaluations == 7 && rows.label[b].read() == "b (open) x2", "Surviving bindings keep their subscriptions");
}

void removal_and_reuse_create_new_lifetimes() {
    ReactiveRuntime r;
    auto count = r.signal(r.root(), "count", 1);
    Rows rows{r, KeyedCollection(r.owner(r.root(), "rows"))};
    check(static_cast<bool>(rows.apply({{"a", "item"}, {"b", "item"}, {"c", "item"}, {"d", "item"}}, count)),
          "Initial rows are valid");
    const auto a = rows.rows.find("a")->lifetime, b = rows.rows.find("b")->lifetime;
    const auto c = rows.rows.find("c")->lifetime, d = rows.rows.find("d")->lifetime;
    rows.expanded[b].write(true);
    r.flush();
    // Remove d and a, replace c with an incompatible kind, keep b.
    const auto update = rows.apply({{"c", "folder"}, {"b", "item"}}, count);
    check(update && update.value->created == std::vector<std::size_t>{0}, "Incompatible kind creates a new instance");
    check(keys(update.value->disposed) == std::vector<std::string>{"a", "c", "d"} &&
          update.value->disposed[1].lifetime == c, "Removed and replaced instances are reported in previous order");
    const auto replaced = rows.rows.find("c")->lifetime;
    check(replaced == d + 1 && !rows.rows.current("c", c) && rows.rows.current("c", replaced) &&
          !rows.rows.current("a", a) && rows.rows.current("b", b), "Old lifetimes stop being current");
    rejects([&] { (void)rows.expanded[c].read(); }, "disposed_reactive_value");
    rejects([&] { (void)rows.label[a].read(); }, "disposed_reactive_value");
    check(rows.closed.empty() && r.deliver_cleanups().empty() && rows.closed == std::vector<std::string>{"a", "c", "d"},
          "Disposal queues cleanup once in previous order until host delivery");
    r.flush();
    check(!rows.expanded[replaced].read() && rows.label[replaced].read() == "c x1", "A replacement starts fresh");

    const auto back = rows.apply({{"a", "item"}, {"b", "item"}, {"c", "folder"}}, count);
    check(back && back.value->created == std::vector<std::size_t>{0} && rows.rows.find("a")->lifetime == replaced + 1,
          "Reusing a removed key creates a new lifetime");
    r.flush();
    check(!rows.expanded[rows.rows.find("a")->lifetime].read() && rows.expanded[b].read(),
          "Reinsertion does not revive disposed state");
    check(r.deliver_cleanups().empty() && rows.closed.size() == 3, "Each cleanup runs once");

    const auto empty = rows.apply({}, count);
    check(empty && rows.rows.instances().empty() && keys(empty.value->disposed) == std::vector<std::string>{"a", "b", "c"},
          "An empty candidate disposes every instance (conditional children)");
    check(r.deliver_cleanups().empty() && rows.closed.size() == 6, "Clearing queues every remaining cleanup");
}

void invalid_candidates_change_nothing() {
    ReactiveRuntime r;
    auto count = r.signal(r.root(), "count", 1);
    Rows rows{r, KeyedCollection(r.owner(r.root(), "rows"))};
    check(static_cast<bool>(rows.apply({{"a", "item"}, {"b", "item"}}, count)), "Initial rows are valid");
    const auto before = rows.rows.instances();
    const auto invalid = rows.apply({{"b", "item"}, {"", "item"}, {"c", ""}, {"b", "item"},
        {"bad\x01", "item"}, {std::string("\xff", 1), "item"}, {std::string(257, 'k'), "item"}, {"a", "folder"}}, count);
    check(!invalid && invalid.diagnostics.size() == 6, "Every invalid child is reported");
    check(has(invalid.diagnostics, "invalid_key", "/children/1/key") &&
          has(invalid.diagnostics, "invalid_kind", "/children/2/kind") &&
          has(invalid.diagnostics, "duplicate_key", "/children/3/key") &&
          has(invalid.diagnostics, "invalid_key", "/children/4/key") &&
          has(invalid.diagnostics, "invalid_key", "/children/5/key") &&
          has(invalid.diagnostics, "invalid_key", "/children/6/key"), "Diagnostics locate each candidate child");
    check(rows.rows.instances().size() == 2 && rows.rows.current("a", before[0].lifetime) &&
          rows.rows.current("b", before[1].lifetime), "A rejected candidate keeps every instance");
    check(r.deliver_cleanups().empty() && rows.closed.empty(), "A rejected candidate disposes nothing");
    std::vector<KeyedChild> many(max_keyed_children + 1, {"k", "item"});
    const auto limit = rows.rows.reconcile(r, many);
    check(!limit && has(limit.diagnostics, "keyed_child_limit", "/children"), "Candidates are bounded");
    const auto later = rows.apply({{"b", "item"}, {"c", "item"}}, count);
    check(later && later.value->instances[0].lifetime == before[1].lifetime && later.value->instances[1].lifetime == 3,
          "Rejected candidates consume no lifetime");
}

void runtime_failures_keep_the_collection_truthful() {
    ReactiveRuntime r, other;
    auto scope = r.owner(r.root(), "rows");
    KeyedCollection rows(scope);
    check(static_cast<bool>(rows.reconcile(r, {{"a", "item"}, {"b", "item"}})), "Initial rows are valid");
    const auto a = rows.find("a")->owner;

    // A fresh key in a foreign runtime fails before any disposal.
    rejects([&] { (void)rows.reconcile(other, {{"c", "item"}}); }, "foreign_reactive_owner");
    check(rows.instances().size() == 2 && r.deliver_cleanups().empty(), "Fresh-key failure changes nothing");
    (void)r.signal(a, "still-live", 0);

    // Reconciliation inside a calculation is a graph mutation.
    const auto inside = r.computed<int>(r.root(), "inside", [&] { (void)rows.reconcile(r, {{"c", "item"}}); return 0; });
    rejects([&] { (void)inside.read(); }, "reactive_mutation");
    check(rows.instances().size() == 2 && keys(rows.instances()) == std::vector<std::string>{"a", "b"},
          "A rejected calculation-time reconciliation changes nothing");

    // Replacement-only failures happen after disposal; the record drops what it disposed.
    rejects([&] { (void)rows.reconcile(other, {{"a", "folder"}, {"b", "item"}}); }, "foreign_reactive_owner");
    check(keys(rows.instances()) == std::vector<std::string>{"b"}, "Disposed instances leave the collection");
    rejects([&] { (void)r.signal(a, "gone", 0); }, "disposed_reactive_owner");

    // Owner budget failure while creating fresh keys disposes only owners it created.
    ReactiveRuntime budget;
    KeyedCollection big(budget.owner(budget.root(), "rows"));
    std::vector<KeyedChild> children;
    for (std::size_t i = 0; i + 3 < max_reactive_owners; ++i) children.push_back({"k" + std::to_string(i), "item"});
    check(big.reconcile(budget, children) && big.instances().size() == max_reactive_owners - 3,
          "Collections may use the remaining owner budget");
    children.push_back({"extra-1", "item"});
    children.push_back({"extra-2", "item"});
    rejects([&] { (void)big.reconcile(budget, children); }, "reactive_owner_limit");
    check(big.instances().size() == max_reactive_owners - 3, "Budget failure keeps every previous instance");
    children.pop_back();
    check(big.reconcile(budget, children) && big.instances().back().key == "extra-1",
          "Owners created by the failed pass were released");

    // A disposed scope cannot create instances.
    scope.dispose();
    rejects([&] { (void)rows.reconcile(r, {{"z", "item"}}); }, "disposed_reactive_owner");
    check(keys(rows.instances()) == std::vector<std::string>{"b"}, "Fresh-key failure keeps the record");
}
} // namespace

int main() {
    try {
        reorder_preserves_instances();
        removal_and_reuse_create_new_lifetimes();
        invalid_candidates_change_nothing();
        runtime_failures_keep_the_collection_truthful();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "Keyed reconciliation checks passed\n";
}
