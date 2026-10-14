#include <tessera/ui/async.hpp>
#include "../check.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
// One-shot allocation failure, enabled only around the tested registration or
// transition. Recovery, observations and cleanup delivery allocate normally.
thread_local int fail_after = -1;
}
void* operator new(std::size_t size) {
    // MSVC Debug strings allocate tiny iterator proxies in noexcept moves.
    // Throwing there terminates inside the STL rather than reaching the API.
    // Exclude tiny bookkeeping allocations in that configuration; source and
    // invalidation storage remain subject to injection.
#if defined(_MSC_VER) && defined(_DEBUG)
    const bool eligible = size > 2 * sizeof(void*);
#else
    const bool eligible = true;
#endif
    if (eligible && fail_after == 0) { fail_after = -1; throw std::bad_alloc(); }
    if (eligible && fail_after > 0) --fail_after;
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

using namespace tessera;
using tessera::test::check;
namespace {
enum class Operation { registration, begin, progress, complete, fail, cancel };
void allocation_failures(Operation operation) {
    bool reached_success = false;
    bool saw_accepted_failure = false;
    for (int point = 0; point < 128 && !reached_success; ++point) {
        ReactiveRuntime r;
        const auto owner = r.owner(r.root(), "row");
        AsyncValue value(r, owner, "name", PropertyType::string);
        const auto initial = value.begin(std::string(160, 'a'));
        check(bool(initial), "Allocation fixture starts host work");
        const auto attempt = initial.value->attempt;
        std::optional<AsyncAttempt> detached;
        int deliveries = 0;
        const auto callback = [&](const AsyncAttempt& address) { detached = address; ++deliveries; };
        if (operation != Operation::registration) value.on_detach(r, callback);
        r.computed<std::uint64_t>(owner, "consumer", [value] { return value.read().attempt; });
        r.flush();
        bool threw = false;
        bool accepted = false;
        fail_after = point;
        try {
            switch (operation) {
            case Operation::registration: value.on_detach(r, callback); accepted = true; break;
            case Operation::begin: accepted = bool(value.begin(std::string(160, 'b'))); break;
            case Operation::progress: accepted = bool(value.progress(attempt, 0.5)); break;
            case Operation::complete: accepted = bool(value.complete(attempt, std::string(160, 'c'))); break;
            case Operation::fail: accepted = bool(value.fail(attempt, {"missing", std::string(160, 'd')})); break;
            case Operation::cancel: accepted = bool(value.cancel(attempt)); break;
            }
        } catch (const std::bad_alloc&) { threw = true; }
        fail_after = -1;
        check(threw || accepted, "Valid operation succeeds unless an allocation fails");
        if (threw && operation == Operation::registration) value.on_detach(r, callback);
        const auto observed = value.read();
        if (threw && (observed.attempt != 1 || observed.status != AsyncStatus::loading || observed.progress)) saw_accepted_failure = true;
        owner.dispose();
        check(deliveries == 0 && r.deliver_cleanups().empty(), "Allocation recovery preserves explicit cleanup delivery");
        if (observed.status == AsyncStatus::loading) {
            check(detached && deliveries == 1 && detached->request() == observed.request && detached->sequence() == observed.attempt,
                  "Detachment matches accepted source state after every allocation failure");
        } else check(!detached && deliveries == 0, "Accepted terminal state clears detachment even when invalidation throws");
        check(r.deliver_cleanups().empty() && deliveries <= 1, "Injected failures never duplicate host work");
        reached_success = !threw;
    }
    check(reached_success, "Fixture covers every eligible allocation until an uninterrupted operation succeeds");
    if (operation != Operation::registration) check(saw_accepted_failure, "Fixture exercises allocation failure after source acceptance");
}
}
int main() {
    try {
        for (const auto operation : {Operation::registration, Operation::begin, Operation::progress,
                                    Operation::complete, Operation::fail, Operation::cancel}) allocation_failures(operation);
    } catch (const std::exception& failure) { fail_after = -1; std::cerr << failure.what() << '\n'; return 1; }
    std::cout << "Async detachment remains consistent through allocation failures.\n";
}
