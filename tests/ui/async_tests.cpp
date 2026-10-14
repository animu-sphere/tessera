#include <tessera/ui/async.hpp>
#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <thread>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
namespace {
std::string text(const AsyncObservation& state) {
    return state.value ? std::get<std::string>(state.value->value) : "Loading";
}
void transitions() {
    ReactiveRuntime r;
    AsyncValue value(r, r.root(), "asset", PropertyType::string);
    check(value.read().status == AsyncStatus::idle && !value.read().stale(), "Observations start idle without a value");
    auto first = value.begin("request"); check(bool(first), "Idle can start a loading attempt");
    check(first.value->attempt.sequence() == 1 && first.value->attempt.request() == "request" && !first.value->detached,
          "Host request identity and first attempt are observable");
    const auto attempt = first.value->attempt;
    check(value.progress(attempt, 0.25).value == true && value.progress(attempt, 0.25).value == false,
          "Equal progress is suppressed by ordinary reactive equality");
    check(bool(value.fail(attempt, {"unavailable", "Try again", true})), "Loading can fail");
    auto state = value.read();
    check(state.status == AsyncStatus::failed && state.failure->retryable && !state.progress && !state.value,
          "Failure records retry policy without accepting a partial value");
    auto retry = value.begin("request"); check(bool(retry), "Failure retries with a new attempt, even for an equal request ID");
    check(retry.value->attempt.sequence() == 2 && !retry.value->detached && !value.read().failure,
          "Retry clears failure and never reuses an attempt");
    check(has(value.complete(attempt, std::string("old")).diagnostics, "async_stale_attempt", "/async/asset/attempt"),
          "An old completion after retry cannot replace state");
    check(bool(value.complete(retry.value->attempt, std::string("Map"))), "Retry accepts one complete value");
    const auto ready = value.read();
    check(ready.status == AsyncStatus::ready && !ready.stale() && ready.value->revision == 1 && ready.value->attempt == 2,
          "Ready values identify their own accepted request, attempt and revision");
    auto replacement = value.begin("replacement"); check(bool(replacement), "Ready can start replacement");
    state = value.read();
    check(state.stale() && text(state) == "Map" && state.value == ready.value && !state.progress,
          "Replacement keeps the original ready identity as stale");
    auto superseding = value.begin("replacement"); check(bool(superseding), "Loading can be superseded");
    check(superseding.value->detached && superseding.value->detached->sequence() == replacement.value->attempt.sequence(),
          "Superseding returns the old host work address for detachment");
    check(!value.complete(replacement.value->attempt, std::string("too late")), "Superseded completion is dropped");
    check(bool(value.fail(superseding.value->attempt, {"missing", "Missing replacement", false})) && value.read().stale(),
          "Replacement failure preserves the complete stale value");
    auto cancelling = value.begin("cancelled"); check(bool(cancelling), "Failed replacement may start another attempt");
    check(bool(value.cancel(cancelling.value->attempt)) && value.read().status == AsyncStatus::cancelled && value.read().stale(),
          "Cancellation is terminal and retains accepted values");
    check(has(value.complete(cancelling.value->attempt, std::string("late")).diagnostics, "async_transition", "/async/asset/status"),
          "Cancellation cannot be completed afterwards");
    auto last = value.begin("new"); check(bool(last), "Cancellation can restart");
    check(bool(value.complete(last.value->attempt, std::string("Rope"))), "Replacement succeeds");
    check(value.read().value->revision == 2 && text(ready) == "Map" && !value.read().stale(),
          "An owned earlier observation stays immutable through replacement");
    check(!value.complete(last.value->attempt, std::string("duplicate")), "A terminal delivery is consumed once");
    AsyncValue cache(r, r.root(), "cached", PropertyType::number);
    check(bool(cache.ready_cached("cache", 7.0)) && cache.read().status == AsyncStatus::ready && cache.read().attempt == 1,
          "Explicit cache hit moves directly from idle to ready");
    check(!cache.ready_cached("cache", 8.0) && std::get<double>(cache.read().value->value) == 7,
          "Cache declaration cannot overwrite a terminal value");
}
void rejection() {
    ReactiveRuntime r;
    AsyncValue value(r, r.root(), "a/b~c", PropertyType::string);
    check(has(value.begin("").diagnostics, "async_name", "/async/a~1b~0c/request") && value.read().attempt == 0,
          "Invalid requests are located and allocate no attempt");
    check(!value.begin(std::string(257, 'a')) && !value.begin("line\n") && !value.begin(std::string("\xc0\xaf")),
          "Request IDs obey UTF-8, length and control bounds");
    check(!value.ready_cached("cache", true) && value.read().status == AsyncStatus::idle,
          "Cache payload validation is atomic");
    const auto start = value.begin("request"); check(bool(start), "Valid request starts after rejections");
    const auto attempt = start.value->attempt;
    check(bool(value.progress(attempt, 0.5)), "Progress accepts an initial finite fraction");
    const auto before = value.read();
    for (const double progress : {-0.1, 1.1, 0.4, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        check(has(value.progress(attempt, progress).diagnostics, "async_progress", "/async/a~1b~0c/progress") && value.read() == before,
              "Invalid or regressing progress changes nothing");
    check(!value.complete(attempt, false) && !value.complete(attempt, std::string(max_async_text_bytes + 1, 'x')) &&
          !value.complete(attempt, std::string("\xed\xa0\x80")) && value.read() == before,
          "Wrong type, oversized and malformed UTF-8 payloads leave loading unchanged");
    check(!value.fail(attempt, {"", "error"}) && !value.fail(attempt, {"error", ""}) &&
          !value.fail(attempt, {"error", std::string(max_async_text_bytes + 1, 'x')}) &&
          !value.fail(attempt, {"error", std::string("\xff")}) && value.read() == before,
          "Failures also enforce bounded valid payloads before mutation");
    check(has(value.cancel({}).diagnostics, "async_stale_attempt", "/async/a~1b~0c/attempt"), "Default tokens address no attempt");
    AsyncValue other(r, r.root(), "other", PropertyType::string);
    const auto other_start = other.begin("request"); check(bool(other_start), "Second observation has an independent lifetime");
    check(!value.complete(other_start.value->attempt, std::string("foreign")) && value.read() == before,
          "Equal request IDs and sequences cannot alias another observation");
    const auto alias = value;
    check(bool(alias.complete(attempt, std::string(max_async_text_bytes, 'x'))) && value.read().status == AsyncStatus::ready,
          "Copies alias one source and the exact payload size bound is accepted");
    AsyncValue number(r, r.root(), "number", PropertyType::number);
    check(!number.ready_cached("cache", std::numeric_limits<double>::quiet_NaN()), "Numeric payloads must be finite");
    AsyncValue reference(r, r.root(), "reference", PropertyType::reference);
    check(!reference.ready_cached("cache", NodeReference{""}) && bool(reference.ready_cached("cache", NodeReference{"target"})),
          "Reference payloads validate author-ID syntax without owning a tree");
    AsyncValue boolean(r, r.root(), "boolean", PropertyType::boolean);
    check(bool(boolean.ready_cached("cache", true)), "Boolean payloads retain exact type");
    try { AsyncValue invalid(r, r.root(), "invalid", static_cast<PropertyType>(99)); check(false, "Invalid type must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "async_type", "Invalid schema is diagnosed"); }
    AsyncValue valid(r, r.root(), "invalid", PropertyType::boolean); // Rejected constructor reserved no source name.
    try { AsyncValue invalid(r, r.root(), "bad\n", PropertyType::boolean); check(false, "Invalid name must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "async_name", "Invalid observation names are diagnosed"); }
}
void lifetimes_and_entry() {
    ReactiveRuntime r;
    KeyedCollection rows(r.owner(r.root(), "rows"));
    auto update = rows.reconcile(r, {{"item", "row"}}); check(bool(update), "Row is mounted");
    const auto owner = update.value->instances[0].owner;
    AsyncValue value(r, owner, "resource", PropertyType::string);
    const auto start = value.begin("load"); check(bool(start), "Owner starts host work");
    int detached = 0;
    r.on_cleanup(owner, "host-work", [&] { ++detached; });
    auto replacement = rows.reconcile(r, {{"item", "replacement-row"}}); check(bool(replacement), "Same author key may replace its owner");
    AsyncValue next(r, replacement.value->instances[0].owner, "resource", PropertyType::string);
    const auto next_start = next.begin("load"); check(bool(next_start), "Replacement can reuse request and source names");
    check(has(value.complete(start.value->attempt, std::string("late")).diagnostics, "disposed_reactive_value", "/async/resource") &&
          !next.complete(start.value->attempt, std::string("late")), "Old owner/token cannot address a reused author key");
    check(detached == 0 && r.deliver_cleanups().empty() && detached == 1 && r.deliver_cleanups().empty() && detached == 1,
          "Owner removal detaches host work exactly once at explicit cleanup delivery");
    Result<AsyncStart> cross_thread;
    std::thread thread([&] { cross_thread = next.begin("thread"); }); thread.join();
    check(has(cross_thread.diagnostics, "reactive_thread", "/async/resource") && next.read().attempt == 1,
          "Background deliveries cannot mutate reactive state directly");
    const auto boundary = r.boundary(r.root(), "calculation");
    r.computed<bool>(boundary, "illegal", [&] {
        const auto result = next.begin("illegal");
        if (!result) throw ReactiveError(result.diagnostics.front());
        return true;
    });
    const auto faults = r.flush();
    check(has(faults, "reactive_mutation", "/async/resource") && next.read().attempt == 1,
          "Calculation-time transitions obey the kernel's mutation guard");
    boundary.dispose(); r.flush();
    check(bool(next.complete(next_start.value->attempt, std::string("fresh"))), "Valid completion recovers after rejected entries");
    ReactiveRuntime foreign;
    try { next.snapshot(foreign); check(false, "Foreign snapshots must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "foreign_reactive_runtime", "Settled snapshots enforce runtime ownership"); }
    const auto survived = next.read();
    r.root().dispose();
    check(!next.begin("disposed") && text(survived) == "fresh", "Owned snapshots survive logical disposal without reviving work");
}
void owner_detachment() {
    ReactiveRuntime r;
    const auto owner = r.owner(r.root(), "row");
    AsyncValue value(r, owner, "a/b~c", PropertyType::string);
    auto first = value.begin("old"); check(bool(first), "Work may begin before detachment registration");
    std::vector<AsyncAttempt> delivered;
    value.on_detach(r, [&](const AsyncAttempt& attempt) { delivered.push_back(attempt); });
    const auto copy = value;
    try { copy.on_detach(r, [](const AsyncAttempt&) {}); check(false, "Aliased registration must reject"); }
    catch (const ReactiveError& e) { check(has({e.diagnostic()}, "async_detach_registered", "/async/a~1b~0c/detach"), "Copies share one detachment registration"); }
    const auto second = copy.begin("new"); check(bool(second), "Copied handle supersedes current work");
    check(second.value->detached && second.value->detached->request() == "old", "Supersession retains the explicit host detachment address");
    check(!value.complete(first.value->attempt, std::string("late")) && !value.complete(second.value->attempt, true),
          "Rejected attempts and payloads do not clear active detachment");
    check(bool(value.progress(second.value->attempt, 0.25)), "Progress preserves active work");
    r.batch([&] { owner.dispose(); check(delivered.empty(), "Disposal inside a batch invokes no external work"); });
    check(!value.complete(second.value->attempt, std::string("late")), "Disposed completion cannot change the queued address");
    check(r.deliver_cleanups().empty() && delivered.size() == 1 && delivered[0].request() == "new" &&
          delivered[0].sequence() == second.value->attempt.sequence(), "Cleanup delivers only the latest loading attempt");
    check(r.deliver_cleanups().empty() && delivered.size() == 1, "Detachment delivers once");
    try { value.on_detach(r, [](const AsyncAttempt&) {}); check(false, "Disposed registration must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "disposed_reactive_value", "Disposed handles cannot attach cleanup"); }

    // Terminal observations have no live host work; retained stale payloads are
    // presentation data and must not cause a second cancellation.
    for (const auto terminal : {AsyncStatus::idle, AsyncStatus::ready, AsyncStatus::failed, AsyncStatus::cancelled}) {
        const auto scope = r.owner(r.root(), "terminal");
        AsyncValue item(r, scope, "item", PropertyType::string);
        item.on_detach(r, [&](const AsyncAttempt&) { check(false, "Terminal work must not detach on disposal"); });
        if (terminal == AsyncStatus::ready) check(bool(item.ready_cached("cached", std::string("value"))), "Cache is terminal without work");
        if (terminal == AsyncStatus::failed || terminal == AsyncStatus::cancelled) {
            check(bool(item.ready_cached("cached", std::string("value"))), "Cache initializes stale-value fixture");
            const auto loading = item.begin("work"); check(bool(loading), "Terminal fixture begins replacement");
            if (terminal == AsyncStatus::failed) check(bool(item.fail(loading.value->attempt, {"error", "Failed"})), "Failure clears active detachment");
            else check(bool(item.cancel(loading.value->attempt)), "Explicit cancellation clears active detachment");
        }
        scope.dispose(); check(r.deliver_cleanups().empty(), "Terminal cleanup consumes an empty registration");
    }
    const auto completed_owner = r.owner(r.root(), "completed");
    AsyncValue completed(r, completed_owner, "item", PropertyType::string);
    completed.on_detach(r, [&](const AsyncAttempt&) { check(false, "Completed work must not detach"); });
    const auto complete_start = completed.begin("work"); check(bool(complete_start), "Completion fixture begins work");
    check(bool(completed.complete(complete_start.value->attempt, std::string("ready"))), "Completion clears active detachment");
    completed_owner.dispose(); check(r.deliver_cleanups().empty(), "Ready completion has no active work");

    // Failed registration leaves the source and shared registration state intact.
    const auto guarded_owner = r.owner(r.root(), "guarded");
    AsyncValue guarded(r, guarded_owner, "item", PropertyType::boolean);
    try { guarded.on_detach(r, {}); check(false, "Empty detachment must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "async_detach_callback", "Empty callback has an actionable diagnostic"); }
    ReactiveRuntime foreign;
    try { guarded.on_detach(foreign, [](const AsyncAttempt&) {}); check(false, "Foreign registration must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "foreign_reactive_owner", "Registration keeps its original owner"); }
    std::string thread_error;
    std::thread thread([&] {
        try { guarded.on_detach(r, [](const AsyncAttempt&) {}); }
        catch (const ReactiveError& e) { thread_error = e.diagnostic().code; }
    }); thread.join();
    check(thread_error == "reactive_thread", "Registration checks thread before shared state");
    int closed = 0;
    guarded.on_detach(r, [&](const AsyncAttempt&) { ++closed; });
    check(bool(guarded.begin("work")), "Registration recovers after invalid entries");
    guarded_owner.dispose(); check(r.deliver_cleanups().empty() && closed == 1, "Recovered registration closes once");

    const auto failing = r.owner(r.root(), "failing");
    AsyncValue failure(r, failing, "resource", PropertyType::boolean);
    failure.on_detach(r, [&](const AsyncAttempt&) { ++closed; throw std::runtime_error("Host failure"); });
    check(bool(failure.begin("work")), "Failing host fixture starts");
    r.on_cleanup(failing, "following", [&] { ++closed; });
    failing.dispose();
    check(has(r.deliver_cleanups(), "reactive_cleanup_failed", "/reactive/root/owners/failing/cleanups/resource") && closed == 3,
          "Host failures are located and do not stop following cleanup");
    check(r.deliver_cleanups().empty() && closed == 3, "Failed detachment cannot retry");
}
void detachment_registration_boundaries() {
    ReactiveRuntime r;
    const auto scope = r.owner(r.root(), "resource");
    AsyncValue value(r, scope, "item", PropertyType::boolean);
    const auto boundary = r.boundary(r.root(), "registration");
    r.computed<bool>(boundary, "illegal", [&] { value.on_detach(r, [](const AsyncAttempt&) {}); return true; });
    check(has(r.flush(), "reactive_mutation", "/async/item"), "Calculation-time registration rejects without attaching cleanup");
    boundary.dispose();
    const auto occupied = r.owner(r.root(), "occupied");
    for (std::size_t i = 0; i < max_reactive_cleanups; ++i) r.on_cleanup(occupied, std::to_string(i), [] {});
    try { value.on_detach(r, [](const AsyncAttempt&) {}); check(false, "Full cleanup budget must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "reactive_cleanup_limit", "Detachment shares the bounded cleanup queue"); }
    occupied.dispose(); check(r.deliver_cleanups().empty(), "Delivery releases the occupied budget");
    auto capture = std::make_shared<int>(0);
    std::weak_ptr<int> weak = capture;
    int closed = 0;
    value.on_detach(r, [&, capture](const AsyncAttempt&) {
        ++closed;
        try { value.read(); check(false, "Detachment callback cannot enter graph"); }
        catch (const ReactiveError& e) { check(e.diagnostic().code == "reactive_cleanup_entry", "Detachment uses ordinary cleanup guards"); }
    });
    capture.reset();
    check(!weak.expired() && bool(value.begin("work")), "Failed registration permits later registration and owns its callback");
    scope.dispose();
    check(!weak.expired() && closed == 0, "Queued callback survives disposal without executing");
    check(r.deliver_cleanups().empty() && closed == 1 && weak.expired(), "Delivery releases host captures despite retained async handles");
    const auto collision = r.owner(r.root(), "collision");
    AsyncValue colliding(r, collision, "item", PropertyType::boolean);
    r.on_cleanup(collision, "item", [&] { ++closed; });
    try { colliding.on_detach(r, [&](const AsyncAttempt&) { check(false, "Rejected callback cannot be retained"); }); check(false, "Cleanup name collision must reject"); }
    catch (const ReactiveError& e) { check(e.diagnostic().code == "duplicate_reactive_name", "Observation name uses the cleanup namespace"); }
    check(bool(colliding.begin("work")), "Rejected registration leaves the observation available");
    collision.dispose(); check(r.deliver_cleanups().empty() && closed == 2, "Only the accepted colliding cleanup runs");
    const auto unreferenced = r.owner(r.root(), "unreferenced");
    {
        AsyncValue temporary(r, unreferenced, "item", PropertyType::boolean);
        temporary.on_detach(r, [&](const AsyncAttempt&) { ++closed; });
        check(bool(temporary.begin("work")), "Transient handles can start work");
    }
    unreferenced.dispose(); check(r.deliver_cleanups().empty() && closed == 3, "Owner retains detachment after all observation handles are released");
    {
        ReactiveRuntime destroyed;
        AsyncValue abandoned(destroyed, destroyed.root(), "item", PropertyType::boolean);
        abandoned.on_detach(destroyed, [&](const AsyncAttempt&) { ++closed; });
        check(bool(abandoned.begin("work")), "Shutdown fixture starts work");
    }
    check(closed == 3, "Runtime destruction releases registrations without implicit host execution");
}
void presentation() {
    ReactiveRuntime r;
    AsyncValue value(r, r.root(), "asset", PropertyType::string);
    int evaluations = 0, notifications = 0;
    auto label = r.computed<std::string>(r.root(), "label", [&] { ++evaluations; return text(value.read()); });
    r.effect<std::string>(r.root(), "status", EffectPhase::notify, [label] { return label.read(); },
        [&](const std::string&) { ++notifications; });
    ComponentDescription component; component.kind = "async-caption";
    component.document.root.kind = NodeKind::text; component.document.root.id = "caption";
    component.document.root.properties["text"] = std::string{};
    component.props = {{"caption", PropertyType::string, {}}};
    component.properties = {{"caption", "caption", "text"}};
    ComponentArguments args; args.props["caption"] = label;
    auto present = [&] {
        check(r.flush().empty(), "Host explicitly settles resource transitions");
        const auto bound = lower_component(r, component, args); check(bool(bound), "Settled async caption lowers through ordinary components");
        auto authored = component.document; authored.root.properties["text"] = text(value.snapshot(r));
        check(bound.value->document == authored, "Caption equals the full-tree authored reference");
        auto tree = UiTree::create(bound.value->document); check(bool(tree), "Caption creates an ordinary tree");
        auto reference = UiTree::create(authored); check(bool(reference), "Authored reference creates an independent tree");
        const StyleSheet sheet; const auto styles = resolve_styles({tree.value->get(), &sheet}); check(bool(styles), "Style is independent of async state");
        const auto reference_styles = resolve_styles({reference.value->get(), &sheet});
        check(reference_styles && *reference_styles.value == *styles.value, "Resolved style equals the authored reference");
        PlaceholderTextShaper shaper;
        const auto layout = compute_layout({tree.value->get(), *styles.value, {240, 80}, &shaper}); check(bool(layout), "Async caption has ordinary geometry");
        const auto reference_layout = compute_layout({reference.value->get(), *reference_styles.value, {240, 80}, &shaper});
        check(bool(reference_layout), "Authored reference lays out successfully");
        auto reference_boxes = reference_layout.value->boxes;
        // Independent trees have distinct lifetime identities; compare geometry
        // and preorder indices while keeping each original layout for traversal.
        for (auto& box : reference_boxes) box.node.tree = (*tree.value)->root().tree;
        check(reference_boxes == layout.value->boxes, "Geometry equals the authored reference");
        const auto semantics = capture_semantics({tree.value->get(), *styles.value, &*layout.value}, 1);
        const auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &shaper});
        const auto reference_semantics = capture_semantics({reference.value->get(), *reference_styles.value, &*reference_layout.value}, 1);
        const auto reference_paint = build_paint_list({reference.value->get(), *reference_styles.value, &*reference_layout.value, &shaper});
        check(semantics && paint && reference_semantics && reference_paint && *semantics.value == *reference_semantics.value &&
              *paint.value == *reference_paint.value, "One settled document supplies matching semantic names and paint");
        check(r.publish() <= 1 && r.deliver_effects(EffectPhase::notify).empty(), "Effects deliver only after host presentation acceptance");
        return bound.value->document;
    };
    const auto idle = present();
    const auto cached = value.ready_cached("cache", std::string("Map")); check(bool(cached), "Host accepts cached asset");
    const auto ready = present(); const int prior_notifications = notifications;
    AsyncAttempt attempt;
    r.batch([&] {
        auto start = value.begin("replacement"); check(bool(start), "Replacement starts inside host batch"); attempt = start.value->attempt;
        check(bool(value.progress(attempt, 0.2)) && bool(value.progress(attempt, 0.8)), "Progress coalesces inside batch");
        check(label.read() == "Map" && notifications == prior_notifications, "Demand reads in a batch see stale content without delivering effects");
        check(!lower_component(r, component, args), "Batch cannot publish a component candidate");
    });
    check(present() == ready && notifications == prior_notifications, "Stale content suppresses caption effects despite observation changes");
    const auto before = value.read();
    check(!value.complete(attempt, true) && value.read() == before && present() == ready, "Invalid completion retains the accepted presentation");
    r.batch([&] { check(bool(value.complete(attempt, std::string("Rope"))), "Host installs complete replacement"); });
    const auto replaced = present();
    check(replaced != ready && idle != ready && notifications == prior_notifications + 1 && evaluations == 4,
          "Accepted replacement produces one full-tree update and one changed caption effect");
}
}
int main() {
    try { transitions(); rejection(); lifetimes_and_entry(); owner_detachment(); detachment_registration_boundaries(); presentation(); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
    std::cout << "Async view observations validated.\n";
}
