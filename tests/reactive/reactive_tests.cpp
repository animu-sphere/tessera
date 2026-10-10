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

void cleanup_order_and_lifetimes() {
    ReactiveRuntime r;
    auto root = r.root();
    auto parent = r.owner(root, "parent");
    auto first = r.owner(parent, "first"), second = r.owner(parent, "second");
    auto source = r.signal(root, "source", 1);
    int evaluations = 0;
    auto view = r.computed<int>(first, "view", [&] { ++evaluations; return source.read(); });
    std::vector<int> calls;
    std::weak_ptr<int> capture;
    {
        auto owned = std::make_shared<int>(1);
        capture = owned;
        r.on_cleanup(first, "first", [&, owned] { calls.push_back(*owned); });
    }
    // Registration across scopes deliberately differs from child-first order.
    r.on_cleanup(parent, "parent", [&] { calls.push_back(4); });
    r.on_cleanup(second, "second", [&] { calls.push_back(3); });
    r.on_cleanup(first, "last", [&] { calls.push_back(2); });
    r.on_cleanup(root, "root", [&] { calls.push_back(6); });
    r.flush();
    check(r.deliver_cleanups().empty() && calls.empty(), "Live owners do not deliver cleanup");
    source.write(2);
    r.batch([&] {
        parent.dispose(); parent.dispose();
        rejects([&] { view.read(); }, "disposed_reactive_value");
        rejects([&] { r.on_cleanup(first, "late", [] {}); }, "disposed_reactive_owner");
        rejects([&] { r.deliver_cleanups(); }, "reactive_open_batch");
        check(calls.empty() && !capture.expired(), "Disposal closes scopes but retains queued cleanup captures");
    });
    r.flush();
    check(evaluations == 1 && calls.empty(), "Flush discards disposed graph work without invoking cleanup");
    // Reused names can queue new lifetimes while the old cleanup is pending.
    auto replacement = r.owner(root, "parent");
    r.on_cleanup(replacement, "parent", [&] { calls.push_back(5); });
    replacement.dispose();
    check(r.deliver_cleanups().empty() && calls == std::vector<int>({1, 2, 3, 4, 5}),
          "Cleanup uses disposal order, child-first scopes, and registration order within each scope");
    check(capture.expired(), "Delivered callbacks release owned captures");
    parent.dispose();
    check(r.deliver_cleanups().empty() && calls.size() == 5, "Repeated disposal and delivery never repeat callbacks");
    root.dispose();
    check(calls.size() == 5 && r.deliver_cleanups().empty() && calls.back() == 6,
          "Explicit root shutdown delivers cleanup after the root is closed");

    int implicit_calls = 0;
    std::weak_ptr<int> implicit_capture;
    {
        ReactiveRuntime temporary;
        auto held = std::make_shared<int>(7);
        implicit_capture = held;
        temporary.on_cleanup(temporary.root(), "live", [held, &implicit_calls] { ++implicit_calls; });
        auto pending = temporary.owner(temporary.root(), "pending");
        temporary.on_cleanup(pending, "queued", [&implicit_calls] { ++implicit_calls; });
        pending.dispose();
    }
    check(implicit_calls == 0 && implicit_capture.expired(),
          "Runtime destruction releases live and queued callbacks without implicit external delivery");
}

void cleanup_failures_and_boundaries() {
    ReactiveRuntime r, other;
    auto root = r.root();
    auto scope = r.owner(root, "cleanup/~");
    auto source = r.signal(root, "source", 0);
    auto external = other.signal(other.root(), "external", 0);
    std::vector<int> calls;
    r.on_cleanup(scope, "standard/~", [&] { calls.push_back(1); throw std::runtime_error("cancel failed"); });
    r.on_cleanup(scope, "nonstandard", [&] { calls.push_back(2); throw 42; });
    r.on_cleanup(scope, "guard", [&] {
        rejects([&] { source.read(); }, "reactive_cleanup_entry");
        rejects([&] { source.write(1); }, "reactive_cleanup_entry");
        rejects([&] { external.read(); }, "reactive_cleanup_entry");
        rejects([&] { r.flush(); }, "reactive_cleanup_entry");
        rejects([&] { r.deliver_cleanups(); }, "reactive_cleanup_entry");
        rejects([&] { r.begin_batch(); }, "reactive_cleanup_entry");
        rejects([&] { root.dispose(); }, "reactive_cleanup_entry");
        rejects([&] { r.on_cleanup(root, "feedback", [] {}); }, "reactive_cleanup_entry");
        calls.push_back(3);
    });
    r.on_cleanup(scope, "uncaught_entry", [&] { source.write(2); });
    r.on_cleanup(scope, "last", [&] { calls.push_back(4); });
    scope.dispose();
    const auto diagnostics = r.deliver_cleanups();
    check(calls == std::vector<int>({1, 2, 3, 4}) && diagnostics.size() == 3,
          "Callable and forbidden-entry failures do not skip later queued cleanup");
    check(diagnostics[0].code == "reactive_cleanup_failed" && diagnostics[0].severity == Severity::error &&
          diagnostics[0].path == "/reactive/root/owners/cleanup~1~0/cleanups/standard~1~0" &&
          diagnostics[0].message.find("cancel failed") != std::string::npos,
          "Cleanup diagnostics locate the escaped registration name and retain the failure reason");
    check(diagnostics[1].code == "reactive_cleanup_failed" &&
          diagnostics[2].path.ends_with("/cleanups/uncaught_entry"), "All failures retain their cleanup location");
    check(r.deliver_cleanups().empty() && calls.size() == 4 && source.read() == 0,
          "Failed callbacks are consumed once, and rejected reactive writes accept nothing");
    source.write(3); r.flush();
    check(source.read() == 3 && external.read() == 0, "Cleanup guards restore both runtimes after delivery");

    int after_allocation_failure = 0;
    auto allocation = r.owner(root, "allocation");
    r.on_cleanup(allocation, "failure", [] { throw std::bad_alloc{}; });
    r.on_cleanup(allocation, "later", [&] { ++after_allocation_failure; });
    allocation.dispose();
    bool allocation_threw = false;
    try { r.deliver_cleanups(); }
    catch (const std::bad_alloc&) { allocation_threw = true; }
    check(allocation_threw && after_allocation_failure == 0 && source.read() == 3,
          "Allocation failure propagates and restores delivery guards");
    check(r.deliver_cleanups().empty() && after_allocation_failure == 1,
          "After allocation failure, only callbacks not yet invoked remain queued");

    auto pure = r.owner(root, "pure");
    auto registering = r.computed<int>(pure, "register", [&] { r.on_cleanup(root, "illegal", [] {}); return 0; });
    rejects([&] { registering.read(); }, "reactive_mutation");
    pure.dispose();
    auto evaluator = r.owner(root, "evaluator");
    auto delivery = r.computed<int>(evaluator, "deliver", [&] { r.deliver_cleanups(); return 0; });
    rejects([&] { delivery.read(); }, "reactive_mutation");
    evaluator.dispose();
    auto comparing = r.signal(root, "comparing", 0, [&](int, int) { r.deliver_cleanups(); return false; });
    rejects([&] { comparing.write(1); }, "reactive_comparator");
    r.flush();

    bool thread_rejected = false;
    std::thread worker([&] {
        try { r.deliver_cleanups(); }
        catch (const ReactiveError& e) { thread_rejected = e.diagnostic().code == "reactive_thread"; }
    });
    worker.join();
    check(thread_rejected, "Cleanup delivery uses the creating UI thread");
    rejects([&] { r.on_cleanup(other.root(), "foreign", [] {}); }, "foreign_reactive_owner");
    rejects([&] { r.on_cleanup(root, "", [] {}); }, "invalid_reactive_name");
    rejects([&] { r.on_cleanup(root, "empty", {}); }, "invalid_reactive_cleanup");
    r.on_cleanup(root, "unique", [] {});
    rejects([&] { r.on_cleanup(root, "unique", [] {}); }, "duplicate_reactive_name");
    // Cleanup and value names have separate namespaces.
    r.signal(root, "unique", 0);

    auto invalid = r.owner(root, "invalid");
    r.computed<int>(invalid, "view", []() -> int { throw std::runtime_error("No valid graph candidate"); });
    auto closed = r.owner(root, "closed");
    int released = 0;
    r.on_cleanup(closed, "subscription", [&] { ++released; });
    closed.dispose();
    rejects([&] { r.flush(); }, "reactive_calculation_failed");
    check(released == 0 && r.deliver_cleanups().empty() && released == 1,
          "Cleanup of a disposed subscription is independent of a failed surviving graph calculation");
    invalid.dispose(); r.flush();
}

void cleanup_limits() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "bounded");
    std::size_t calls = 0;
    for (std::size_t i = 0; i < max_reactive_cleanups; ++i)
        r.on_cleanup(scope, "c" + std::to_string(i), [&] { ++calls; });
    rejects([&] { r.on_cleanup(scope, "overflow", [] {}); }, "reactive_cleanup_limit");
    scope.dispose();
    rejects([&] { r.on_cleanup(r.root(), "pending_overflow", [] {}); }, "reactive_cleanup_limit");
    check(r.deliver_cleanups().empty() && calls == max_reactive_cleanups,
          "Pending registrations retain their bounded storage until delivery");
    r.on_cleanup(r.root(), "fresh", [&] { ++calls; });
    r.root().dispose();
    check(r.deliver_cleanups().empty() && calls == max_reactive_cleanups + 1,
          "Delivery releases registration capacity");
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

using Log = std::vector<std::string>;

void effects_and_publication() {
    ReactiveRuntime r;
    auto root = r.root();
    auto source = r.signal(root, "source", 1);
    Log log;
    int prepare_calculations = 0;
    auto view = r.owner(root, "view");
    r.effect<int>(view, "notify", EffectPhase::notify, [&] { return source.read(); }, [&](int value) {
        log.push_back("notify " + std::to_string(value));
        return [&log, value] { log.push_back("undo notify " + std::to_string(value)); };
    });
    r.effect<int>(view, "prepare", EffectPhase::prepare, [&] { ++prepare_calculations; return source.read() * 2; },
        [&](int value) { log.push_back("prepare " + std::to_string(value)); });
    r.on_cleanup(view, "after", [&] { log.push_back("cleanup after"); });
    auto doubled = r.computed<int>(root, "doubled", [&] { return source.read() * 2; });
    rejects([&] { r.publish(); }, "reactive_unsettled");
    r.flush();
    check(r.deliver_effects(EffectPhase::prepare).empty() && log.empty(), "Graph settlement alone delivers no effect");
    check(r.publish() == 2, "The first publication captures each initial effect value");
    check(r.deliver_effects(EffectPhase::notify).empty() && log == Log({"notify 1"}), "Delivery runs only the requested phase");
    check(r.deliver_effects(EffectPhase::prepare).empty() && log == Log({"notify 1", "prepare 2"}),
          "Prepare delivery is independent of notify delivery");
    check(r.publish() == 0 && r.deliver_effects(EffectPhase::notify).empty() && log.size() == 2,
          "Publishing unchanged values queues no delivery");

    log.clear();
    r.batch([&] {
        source.write(2);
        check(doubled.read() == 4, "Explicit reads inside a batch see the newest source");
        rejects([&] { r.publish(); }, "reactive_open_batch");
        rejects([&] { r.deliver_effects(EffectPhase::notify); }, "reactive_open_batch");
        source.write(3);
    });
    check(log.empty(), "Explicit reads during a batch deliver no partial effect");
    rejects([&] { r.publish(); }, "reactive_unsettled");
    r.flush();
    check(r.publish() == 2, "One successful batch publishes one candidate");
    r.deliver_effects(EffectPhase::prepare); r.deliver_effects(EffectPhase::notify);
    check(log == Log({"prepare 6", "undo notify 1", "notify 3"}),
          "Publication delivers final batch values; replacement runs prior cleanup once before applying");

    // A failing calculation registered after the effects lets them refresh
    // before flush fails; publication must still reject the whole candidate.
    bool reject_candidate = false;
    auto guard = r.owner(root, "guard");
    r.computed<int>(guard, "candidate", [&] {
        const int value = source.read();
        if (reject_candidate) throw std::runtime_error("Candidate rejected");
        return value;
    });
    r.flush(); r.publish(); log.clear();
    reject_candidate = true; source.write(7);
    rejects([&] { r.flush(); }, "reactive_calculation_failed");
    rejects([&] { r.publish(); }, "reactive_unsettled");
    check(r.deliver_effects(EffectPhase::prepare).empty() && r.deliver_effects(EffectPhase::notify).empty() && log.empty(),
          "No effect observes a rejected candidate");
    reject_candidate = false; source.write(8); r.flush();
    check(r.publish() == 2, "A later successful candidate publishes");
    r.deliver_effects(EffectPhase::prepare); r.deliver_effects(EffectPhase::notify);
    check(log == Log({"prepare 16", "undo notify 3", "notify 8"}), "Only the latest published value is delivered");

    // A host may publish again before delivering; deliveries coalesce.
    log.clear();
    source.write(9); r.flush(); r.publish();
    source.write(10); r.flush();
    check(r.publish() == 2, "Changed effects republish their latest value");
    r.deliver_effects(EffectPhase::notify); r.deliver_effects(EffectPhase::prepare);
    check(log == Log({"undo notify 8", "notify 10", "prepare 20"}), "An undelivered older value is replaced");

    int parity_applications = 0;
    auto parity = r.owner(root, "parity");
    r.effect<int>(parity, "parity", EffectPhase::notify, [&] { return source.read() % 2; },
        [&](int) { ++parity_applications; });
    r.flush(); r.publish(); r.deliver_effects(EffectPhase::notify);
    source.write(12); r.flush();
    check(r.publish() == 2 && parity_applications == 1, "Equal effect outputs are not redelivered");
    r.deliver_effects(EffectPhase::notify); r.deliver_effects(EffectPhase::prepare);

    log.clear();
    source.write(11); r.flush(); r.publish();
    const int before_disposal = prepare_calculations;
    view.dispose(); view.dispose();
    source.write(13); r.flush();
    check(r.deliver_effects(EffectPhase::notify).empty() && r.deliver_effects(EffectPhase::prepare).empty() &&
          log.empty() && prepare_calculations == before_disposal,
          "Disposed owners discard pending deliveries and evaluate no later effect calculations");
    check(r.deliver_cleanups().empty() && log == Log({"undo notify 12", "cleanup after"}),
          "Disposal queues the latest effect cleanup once in registration order");
    check(r.deliver_cleanups().empty() && log.size() == 2, "Effect cleanup is never repeated");
    parity.dispose(); guard.dispose(); r.deliver_cleanups();
}

void effect_failures_and_boundaries() {
    ReactiveRuntime r, other;
    auto root = r.root();
    auto source = r.signal(root, "source", 0);
    auto mirror = r.signal(root, "mirror", 0);
    auto derived = r.computed<int>(root, "derived", [&] { return source.read() + 1; });
    auto scope = r.owner(root, "effects/~");
    std::vector<int> calls;
    const auto read_source = [&] { return source.read(); };
    r.effect<int>(scope, "standard/~", EffectPhase::notify, read_source,
        [&](int) -> std::function<void()> { calls.push_back(1); throw std::runtime_error("apply failed"); });
    r.effect<int>(scope, "nonstandard", EffectPhase::notify, read_source, [&](int) { calls.push_back(2); throw 42; });
    r.effect<int>(scope, "guard", EffectPhase::notify, read_source, [&](int value) {
        check(source.read() == value && derived.read() == value + 1, "Effects may read settled values");
        rejects([&] { r.flush(); }, "reactive_effect_entry");
        rejects([&] { r.publish(); }, "reactive_effect_entry");
        rejects([&] { r.deliver_effects(EffectPhase::notify); }, "reactive_effect_entry");
        rejects([&] { r.deliver_cleanups(); }, "reactive_effect_entry");
        rejects([&] { r.begin_batch(); }, "reactive_effect_entry");
        rejects([&] { root.dispose(); }, "reactive_effect_entry");
        rejects([&] { r.owner(root, "late"); }, "reactive_effect_entry");
        rejects([&] { r.signal(root, "late", 0); }, "reactive_effect_entry");
        rejects([&] { r.on_cleanup(root, "late", [] {}); }, "reactive_effect_entry");
        rejects([&] { r.effect<int>(root, "late", EffectPhase::notify, [] { return 0; }, [](int) {}); },
                "reactive_effect_entry");
        calls.push_back(3);
        return [&calls] { calls.push_back(30); throw std::runtime_error("cleanup failed"); };
    });
    r.effect<int>(scope, "writer", EffectPhase::notify, read_source, [&](int value) {
        check(mirror.write(value + 100), "Effects may write sources for a later batch");
        calls.push_back(4);
    });
    r.flush();
    check(r.publish() == 4, "Each effect captures its initial value");
    auto diagnostics = r.deliver_effects(EffectPhase::notify);
    check(calls == std::vector<int>({1, 2, 3, 4}) && diagnostics.size() == 2,
          "Effect failures do not skip later deliveries");
    check(diagnostics[0].code == "reactive_effect_failed" && diagnostics[0].severity == Severity::error &&
          diagnostics[0].path == "/reactive/root/owners/effects~1~0/effects/standard~1~0" &&
          diagnostics[0].message.find("apply failed") != std::string::npos,
          "Effect diagnostics locate the escaped registration and retain the failure reason");
    check(diagnostics[1].code == "reactive_effect_failed" && diagnostics[1].path.ends_with("/effects/nonstandard"),
          "Nonstandard effect failures are located");
    check(mirror.read() == 100 && r.deliver_effects(EffectPhase::notify).empty() && calls.size() == 4,
          "Writes wait for a later flush, and failed effects are consumed rather than retried");
    r.flush();
    check(r.publish() == 0, "An effect-induced write that changes no effect queues nothing");

    calls.clear();
    source.write(1); r.flush(); r.publish();
    diagnostics = r.deliver_effects(EffectPhase::notify);
    check(calls == std::vector<int>({1, 2, 30, 3, 4}) && diagnostics.size() == 3 &&
          diagnostics[2].code == "reactive_cleanup_failed" && diagnostics[2].path.ends_with("/effects/guard"),
          "A failed replacement cleanup is reported once and the next application still runs");

    auto pure = r.owner(root, "pure");
    r.effect<int>(pure, "write", EffectPhase::notify, [&] { mirror.write(1); return 0; }, [](int) {});
    rejects([&] { r.flush(); }, "reactive_mutation");
    pure.dispose(); r.flush();

    calls.clear();
    auto allocation = r.owner(root, "allocation");
    r.effect<int>(allocation, "failure", EffectPhase::prepare, read_source, [](int) { throw std::bad_alloc{}; });
    r.effect<int>(allocation, "later", EffectPhase::prepare, read_source, [&](int) { calls.push_back(5); });
    r.flush(); r.publish();
    bool allocation_threw = false;
    try { r.deliver_effects(EffectPhase::prepare); }
    catch (const std::bad_alloc&) { allocation_threw = true; }
    check(allocation_threw && calls.empty(), "Allocation failure propagates from effect delivery");
    source.write(2); r.flush();
    check(r.deliver_effects(EffectPhase::prepare).empty() && calls == std::vector<int>({5}),
          "After allocation failure, guards restore and only undelivered effects remain queued");

    bool thread_rejected = false;
    std::thread worker([&] {
        try { r.deliver_effects(EffectPhase::notify); }
        catch (const ReactiveError& e) { thread_rejected = e.diagnostic().code == "reactive_thread"; }
    });
    worker.join();
    check(thread_rejected, "Effect delivery uses the creating UI thread");
    rejects([&] { r.effect<int>(other.root(), "foreign", EffectPhase::notify, [] { return 0; }, [](int) {}); },
            "foreign_reactive_owner");
    rejects([&] { r.effect<int>(root, "phase", static_cast<EffectPhase>(7), [] { return 0; }, [](int) {}); },
            "invalid_reactive_effect");
    r.effect<int>(root, "unique", EffectPhase::notify, [] { return 0; }, [](int) {});
    rejects([&] { r.effect<int>(root, "unique", EffectPhase::prepare, [] { return 0; }, [](int) {}); },
            "duplicate_reactive_name");
    // Effects, values, and cleanups have separate namespaces.
    r.signal(root, "unique", 0);
    r.on_cleanup(root, "unique", [] {});
}

void effect_feedback() {
    ReactiveRuntime r;
    auto root = r.root();
    auto count = r.signal(root, "count", 0);
    // Host drain loop: each round settles, rebuilds, publishes, and delivers.
    const auto drain = [&] {
        std::size_t publications = 0;
        for (;;) {
            r.flush();
            ++publications;
            if (!r.publish()) return publications;
            check(r.deliver_effects(EffectPhase::notify).empty(), "Feedback fixture effects succeed");
        }
    };
    auto settling = r.owner(root, "settling");
    r.effect<int>(settling, "clamp", EffectPhase::notify, [&] { return count.read(); },
        [&](int value) { if (value < 5) count.write(value + 1); });
    check(drain() == 7 && count.read() == 5, "Converging effect feedback drains through later publications");
    settling.dispose(); r.deliver_cleanups();

    auto runaway = r.owner(root, "runaway");
    r.effect<int>(runaway, "increment", EffectPhase::notify, [&] { return count.read(); },
        [&](int value) { count.write(value + 1); });
    const auto diagnostic = rejects([&] { drain(); }, "reactive_effect_feedback");
    check(diagnostic.path == "/reactive/root/owners/runaway/effects/increment" &&
          count.read() == 5 + 1 + static_cast<int>(max_reactive_effect_rounds),
          "Unsettled feedback is diagnosed at the writing effect after the bounded publication count");
    runaway.dispose(); r.flush();
    check(r.publish() == 0 && r.deliver_cleanups().empty(), "After breaking the feedback, publication resumes");
}

void effect_limits() {
    ReactiveRuntime r;
    auto scope = r.owner(r.root(), "bounded");
    for (std::size_t i = 0; i + 1 < max_reactive_cleanups; ++i) r.on_cleanup(scope, "c" + std::to_string(i), [] {});
    r.effect<int>(scope, "last", EffectPhase::notify, [] { return 0; }, [](int) {});
    rejects([&] { r.effect<int>(scope, "overflow", EffectPhase::notify, [] { return 0; }, [](int) {}); },
            "reactive_cleanup_limit");
    scope.dispose();
    rejects([&] { r.effect<int>(r.root(), "pending", EffectPhase::notify, [] { return 0; }, [](int) {}); },
            "reactive_cleanup_limit");
    check(r.deliver_cleanups().empty(), "Queued effect slots retain, then release, the shared cleanup budget");
    r.effect<int>(r.root(), "fresh", EffectPhase::notify, [] { return 1; }, [](int) {});
    r.flush();
    check(r.publish() == 1, "Released capacity admits a new effect");
}
}
int main() {
    try {
        diamond_and_equality(); branches_and_batches(); failures_and_recovery();
        ownership_and_boundaries(); stable_order_and_limits();
        cleanup_order_and_lifetimes(); cleanup_failures_and_boundaries(); cleanup_limits();
        effects_and_publication(); effect_failures_and_boundaries(); effect_feedback(); effect_limits();
        std::cout << "Reactive graph, equality, batches, ownership, cleanup, effect and failure checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
