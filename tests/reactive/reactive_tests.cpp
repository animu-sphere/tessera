#include <tessera/reactive/runtime.hpp>
#include "../check.hpp"
#include <iostream>
#include <thread>

using namespace tessera;
using tessera::test::check;

namespace {
template<class F>
Diagnostic rejects(F f, std::string_view code) {
    try { f(); }
    catch (const ReactiveError& e) {
        const auto d = e.diagnostic();
        check(d.code == code && d.severity == Severity::error && !d.path.empty() && !d.message.empty(),
              "Failure must have the expected located, actionable diagnostic");
        return d;
    }
    throw std::runtime_error("Expected a reactive failure: " + std::string(code));
}

void diamond_and_equality() {
    ReactiveRuntime r;
    auto root = r.root();
    auto source = r.signal(root, "source", 1);
    int left_count = 0, right_count = 0, sink_count = 0, tail_count = 0;
    Computed<int> left, right;
    // Register the sink first to test recursive prerequisite refresh, rather than
    // accidentally relying on registration order as a topological ordering.
    auto sink = r.computed<int>(root, "sink", [&] {
        ++sink_count;
        const int l = left.read(), rr = right.read();
        check(rr == l * 10, "A diamond must never observe mixed source revisions");
        return l + rr;
    });
    left = r.computed<int>(root, "left", [&] { ++left_count; return source.read() * 2; });
    right = r.computed<int>(root, "right", [&] { ++right_count; return source.read() * 20; });
    auto tail = r.computed<int>(root, "tail", [&] { ++tail_count; return sink.read() + 1; });
    r.flush();
    check(tail.read() == 23 && left_count == 1 && right_count == 1 && sink_count == 1 && tail_count == 1,
          "Initial diamond/chain should calculate once per node");
    check(source.revision() == 1 && sink.revision() == 1, "Accepted initial values have revision one");
    check(!source.write(1), "Equal source writes should be suppressed");
    r.flush();
    check(source.revision() == 1 && sink_count == 1, "Equal source write must not invalidate");
    source.write(2); source.write(3);
    check(left_count == 1, "Writes queue work without evaluating it");
    r.flush();
    check(tail.read() == 67 && left_count == 2 && right_count == 2 && sink_count == 2 && tail_count == 2,
          "Multiple writes coalesce throughout the diamond and chain");
    check(source.revision() == 3 && sink.revision() == 2, "Revisions count accepted changes, not flush calls");

    int parity_count = 0, downstream_count = 0;
    auto parity = r.computed<int>(root, "parity", [&] { ++parity_count; return source.read() % 2; });
    auto downstream = r.computed<int>(root, "downstream", [&] { ++downstream_count; return parity.read() + 100; });
    r.flush();
    source.write(5); r.flush();
    check(parity_count == 2 && downstream_count == 1 && downstream.read() == 101 && parity.revision() == 1,
          "Equal derived output suppresses downstream calculations and revisions");
    source.write(6); r.flush();
    check(downstream_count == 2 && downstream.read() == 100, "Changed derived output reaches the sink");
    // Source return to its old value still advances its revision; the derived
    // parity output is unchanged, so its dependent should stay cached.
    source.write(8); source.write(6); r.flush();
    check(parity_count == 4 && downstream_count == 2, "Equality suppression also works after transient writes");
}

void branches_and_batches() {
    ReactiveRuntime r;
    auto root = r.root();
    auto choose_left = r.signal(root, "choose", true);
    auto a = r.signal(root, "a", 10), b = r.signal(root, "b", 10);
    int branch_count = 0, sink_count = 0;
    auto branch = r.computed<int>(root, "branch", [&] { ++branch_count; return choose_left.read() ? a.read() : b.read(); });
    auto sink = r.computed<int>(root, "sink", [&] { ++sink_count; return branch.read() * 2; });
    r.flush();
    choose_left.write(false); r.flush();
    check(branch_count == 2 && sink_count == 1, "Equal branch result must still replace dependencies");
    a.write(99); r.flush();
    check(branch_count == 2, "Abandoned dynamic source edge must be detached");
    b.write(11); r.flush();
    check(sink.read() == 22 && branch_count == 3 && sink_count == 2, "New branch source must be subscribed");
    r.batch([&] {
        b.write(12);
        check(branch.read() == 12, "Demand reads inside a batch see the newest source");
        r.batch([&] { b.write(13); });
        rejects([&] { r.flush(); }, "reactive_open_batch");
        check(sink_count == 2, "Demanding one branch does not flush downstream nodes");
    });
    check(branch_count == 4, "Batch close does not implicitly flush");
    r.flush();
    check(sink.read() == 26 && branch_count == 5 && sink_count == 3, "Nested batches join the outer update");
    try { r.batch([&] { b.write(14); throw std::runtime_error("Host update failure"); }); }
    catch (const std::runtime_error&) {}
    r.flush();
    check(sink.read() == 28, "Exceptional batch close restores bookkeeping without rolling back host writes");
    rejects([&] { r.end_batch(); }, "reactive_batch_unbalanced");
}

void failures_and_recovery() {
    ReactiveRuntime r;
    auto root = r.root();
    auto choose = r.signal(root, "choose", false);
    auto old_source = r.signal(root, "old", 1), new_source = r.signal(root, "new", 10);
    bool fail_calculation = false;
    int count = 0;
    auto branch = r.computed<int>(root, "branch/~", [&] {
        ++count;
        const int value = choose.read() ? new_source.read() : old_source.read();
        if (fail_calculation) throw std::runtime_error("Fixture rejected its candidate");
        return value;
    });
    int sink_count = 0;
    auto sink = r.computed<int>(root, "sink", [&] { ++sink_count; return branch.read() * 2; });
    r.flush();
    const auto revision = branch.revision();
    choose.write(true); fail_calculation = true;
    auto diagnostic = rejects([&] { r.flush(); }, "reactive_calculation_failed");
    check(diagnostic.path == "/reactive/root/values/branch~1~0", "Names must be escaped as pointer segments");
    check(sink_count == 1, "A failed prerequisite cannot publish a partial consumer result");
    // Failed candidate dependencies are not committed, but dirty/failed state
    // keeps the calculation retryable even when the new source had no edge.
    new_source.write(11); fail_calculation = false; r.flush();
    check(sink.read() == 22 && branch.revision() == revision + 1 && sink_count == 2,
          "Failure followed by new-source update must recover coherently");
    const auto after = count;
    old_source.write(2); r.flush();
    check(count == after, "Recovery must replace the last accepted dependency set");

    auto bad_owner = r.owner(root, "bad");
    auto bad = r.computed<int>(bad_owner, "write", [&] { new_source.write(20); return 0; });
    rejects([&] { bad.read(); }, "reactive_mutation");
    check(new_source.read() == 11, "A pure calculation cannot write reactive state");
    bad_owner.dispose();
    auto reentrant_owner = r.owner(root, "reentrant");
    auto reentrant = r.computed<int>(reentrant_owner, "flush", [&] { r.flush(); return 0; });
    rejects([&] { r.flush(); }, "reactive_reentrant_flush");
    reentrant_owner.dispose(); r.flush();
    check(sink.read() == 22, "Failed flush restores scheduler and collection guards");

    auto cycle_owner = r.owner(root, "cycle");
    auto circular = r.signal(cycle_owner, "circular", true);
    Computed<int> a, b;
    a = r.computed<int>(cycle_owner, "a", [&] { return circular.read() ? b.read() : 7; });
    b = r.computed<int>(cycle_owner, "b", [&] { return a.read() + 1; });
    diagnostic = rejects([&] { r.flush(); }, "reactive_cycle");
    check(diagnostic.message.find("/values/a ->") != std::string::npos &&
          diagnostic.message.find("/values/b ->") != std::string::npos, "Cycle diagnostic must carry the dependency path");
    circular.write(false); r.flush();
    check(a.read() == 7 && b.read() == 8, "A broken cycle can retry after a later accepted update");

    auto direct_owner = r.owner(root, "direct");
    Computed<int> direct;
    direct = r.computed<int>(direct_owner, "self", [&] { return direct.read(); });
    rejects([&] { direct.read(); }, "reactive_cycle");
    direct_owner.dispose(); r.flush();

    auto failing_owner = r.owner(root, "obsolete");
    auto input = r.signal(root, "input", 1);
    auto abandon = r.signal(root, "abandon", false);
    auto failing = r.computed<int>(failing_owner, "failing", [&] {
        if (input.read() == 2) throw std::runtime_error("Obsolete branch failed");
        return 4;
    });
    auto switching = r.computed<int>(root, "switching", [&] {
        // Switching away must detach the obsolete prerequisite.
        return abandon.read() ? 9 : failing.read();
    });
    r.flush(); input.write(2); abandon.write(true);
    check(switching.read() == 9, "A branch may detach from a newly failed prerequisite");
    failing_owner.dispose(); r.flush();

    bool equality_fails = false;
    auto compare_owner = r.owner(root, "derived_compare");
    auto compared = r.computed<int>(compare_owner, "value", [&] { return input.read() % 2; },
        [&](int a, int b) { if (equality_fails) throw std::runtime_error("Comparator rejected candidate"); return a == b; });
    r.flush(); equality_fails = true; input.write(4);
    rejects([&] { compared.read(); }, "reactive_equality_failed");
    equality_fails = false; r.flush();
    check(compared.read() == 0 && compared.revision() == 1,
          "A failed derived comparison preserves its accepted value, revision and retry bookkeeping");
}

void ownership_and_boundaries() {
    ReactiveRuntime r, other;
    auto root = r.root();
    rejects([&] { r.signal(other.root(), "foreign", 0); }, "foreign_reactive_owner");
    auto host = r.signal(root, "host", 1);
    auto popup = r.owner(root, "popup");
    auto child = r.owner(popup, "child");
    int count = 0;
    auto view = r.computed<int>(child, "view", [&] { ++count; return host.read(); });
    r.flush(); host.write(2); popup.dispose(); popup.dispose(); r.flush();
    check(count == 1 && host.read() == 2, "Closing a subtree must discard pending work without owning its sources");
    rejects([&] { view.read(); }, "disposed_reactive_value");
    rejects([&] { r.signal(child, "new", 0); }, "disposed_reactive_owner");
    popup = r.owner(root, "popup");
    auto replacement = r.signal(popup, "view", 3);
    rejects([&] { view.read(); }, "disposed_reactive_value");
    check(replacement.read() == 3, "Reusing names must create a different lifetime");

    auto use_dead = r.signal(root, "use_dead", true);
    auto surviving = r.computed<int>(root, "surviving", [&] { return use_dead.read() ? replacement.read() : host.read(); });
    r.flush(); popup.dispose();
    rejects([&] { surviving.read(); }, "disposed_reactive_value");
    use_dead.write(false); r.flush();
    check(surviving.read() == 2, "A surviving calculation may switch away from a disposed dependency");

    auto external = other.signal(other.root(), "external", 9);
    auto foreign_owner = r.owner(root, "foreign");
    auto foreign = r.computed<int>(foreign_owner, "read", [&] { return external.read(); });
    rejects([&] { foreign.read(); }, "foreign_reactive_runtime");
    foreign_owner.dispose();
    bool thread_rejected = false;
    std::thread worker([&] {
        try { host.read(); }
        catch (const ReactiveError& e) { thread_rejected = e.diagnostic().code == "reactive_thread"; }
    });
    worker.join(); check(thread_rejected, "Graph entry from a background thread must fail");

    Signal<int> expired;
    { ReactiveRuntime temporary; expired = temporary.signal(temporary.root(), "value", 4); }
    rejects([&] { expired.read(); }, "expired_reactive_runtime");
    rejects([&] { r.signal(root, "host", 0); }, "duplicate_reactive_name");
    rejects([&] { r.owner(root, ""); }, "invalid_reactive_name");
    rejects([&] { r.signal(root, std::string(257, 'x'), 0); }, "invalid_reactive_name");
    rejects([&] { r.signal(root, std::string("\xff"), 0); }, "invalid_reactive_name");

    auto compare = r.signal(root, "compare", 1, [](int, int) -> bool { throw std::runtime_error("bad equality"); });
    rejects([&] { compare.write(2); }, "reactive_equality_failed");
    check(compare.read() == 1 && compare.revision() == 1, "Failed equality must leave the accepted source unchanged");
    auto recursive_compare = r.signal(root, "recursive_compare", 0, [&](int a, int b) { host.read(); return a == b; });
    rejects([&] { recursive_compare.write(1); }, "reactive_comparator");
    auto always_changed = r.signal(root, "always", 1, [](int, int) { return false; });
    check(always_changed.write(1) && always_changed.revision() == 2, "Host can explicitly select always-changed equality");
    r.flush();
}

void stable_order_and_limits() {
    ReactiveRuntime r;
    auto root = r.root();
    auto source = r.signal(root, "source", 0);
    std::vector<int> order;
    r.computed<int>(root, "first", [&] { order.push_back(1); return source.read(); });
    r.computed<int>(root, "second", [&] { order.push_back(2); return source.read(); });
    r.computed<int>(root, "third", [&] { order.push_back(3); return source.read(); });
    r.flush(); source.write(1); r.flush();
    check(order == std::vector<int>({1, 2, 3, 1, 2, 3}), "Independent scheduled work has stable creation order");
    for (std::size_t i = 0; i < max_reactive_depth; ++i) r.begin_batch();
    rejects([&] { r.begin_batch(); }, "reactive_batch_limit");
    for (std::size_t i = 0; i < max_reactive_depth; ++i) r.end_batch();
    auto deepest = root;
    for (std::size_t i = 1; i < max_reactive_depth; ++i) deepest = r.owner(deepest, "child");
    rejects([&] { r.owner(deepest, "too_deep"); }, "reactive_owner_limit");
    auto chain_owner = r.owner(root, "chain");
    Computed<int> chain;
    for (std::size_t i = 0; i <= max_reactive_depth; ++i) {
        auto prior = chain;
        chain = r.computed<int>(chain_owner, "v" + std::to_string(i), [prior, i] { return i ? prior.read() + 1 : 0; });
    }
    rejects([&] { chain.read(); }, "reactive_depth");
    // Earlier failed nodes are retryable without residual stack entries.
    r.flush(); check(chain.read() == static_cast<int>(max_reactive_depth), "Bounded demand failure leaves the graph retryable");

    struct Lease {
        std::vector<int>& closed;
        int id;
        ~Lease() { closed.push_back(id); }
    };
    std::vector<int> closed;
    auto parent = r.owner(root, "parent");
    auto first = r.owner(parent, "first"), second = r.owner(parent, "second");
    auto attach = [&](const ReactiveOwner& owner, int id) {
        auto lease = std::make_shared<Lease>(closed, id);
        r.computed<int>(owner, "lease", [lease] { return lease->id; });
    };
    attach(parent, 3); attach(first, 1); attach(second, 2);
    parent.dispose(); parent.dispose();
    check(closed == std::vector<int>({1, 2, 3}), "Owned callable captures release once in child-first, creation order");

    ReactiveRuntime bounded;
    auto bounded_owner = bounded.owner(bounded.root(), "bounded");
    Signal<int> stale;
    for (std::size_t i = 0; i < max_reactive_nodes; ++i)
        stale = bounded.signal(bounded_owner, "s" + std::to_string(i), 0);
    rejects([&] { bounded.signal(bounded_owner, "overflow", 0); }, "reactive_node_limit");
    bounded_owner.dispose();
    auto fresh = bounded.signal(bounded.root(), "fresh", 1);
    check(fresh.read() == 1, "Disposal releases the live node budget");
    rejects([&] { stale.read(); }, "disposed_reactive_value");
}
}
int main() {
    try {
        diamond_and_equality(); branches_and_batches(); failures_and_recovery();
        ownership_and_boundaries(); stable_order_and_limits();
        std::cout << "Reactive graph, equality, batches, ownership and failure checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
