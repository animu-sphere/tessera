#include <tessera/reactive/runtime.hpp>
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <limits>
#include <list>
#include <set>
#include <thread>

namespace tessera {
ReactiveError::ReactiveError(Diagnostic diagnostic)
    : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}
namespace detail {
namespace {
[[noreturn]] void fail(std::string code, std::string path, std::string message) {
    throw ReactiveError({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
std::string escaped(std::string_view name) {
    std::string result;
    for (const char c : name) {
        if (c == '~') result += "~0";
        else if (c == '/') result += "~1";
        else result += c;
    }
    return result;
}
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 256 || !valid_utf8(name))
        fail("invalid_reactive_name", "/reactive", "Supply a nonempty UTF-8 name of at most 256 bytes.");
}
struct Owner { std::uint64_t parent = 0; std::string path; std::size_t depth = 1; };
struct Node {
    std::uint64_t owner = 0;
    std::string path;
    std::any value;
    std::function<std::any()> calculate;
    ReactiveEqual equal;
    std::uint64_t revision = 0;
    bool dirty = true;
    bool failed = false;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dependencies;
    std::set<std::uint64_t> consumers;
};
struct Collection { std::uint64_t id; std::vector<std::pair<std::uint64_t, std::uint64_t>> dependencies; };
struct Cleanup {
    std::uint64_t owner;
    std::string path;
    std::function<void()> callback;
};
thread_local ReactiveState* active = nullptr;
}
struct ReactiveState {
    std::thread::id thread = std::this_thread::get_id();
    std::map<std::uint64_t, Owner> owners{{1, {0, "/reactive/root", 1}}};
    std::map<std::uint64_t, Node> nodes;
    std::list<Cleanup> cleanups, pending_cleanups;
    std::uint64_t next_owner = 2, next_node = 1;
    std::size_t batches = 0;
    bool flushing = false, comparing = false, delivering_cleanups = false;
    std::vector<Collection> stack;
    void check() const {
        if (thread != std::this_thread::get_id())
            fail("reactive_thread", "/reactive", "Use the runtime on its creating UI thread.");
        if (active && active->delivering_cleanups)
            fail("reactive_cleanup_entry", "/reactive", "Deliver external cleanup only; queue reactive updates for a later host batch.");
        if (active && active != this)
            fail("foreign_reactive_runtime", "/reactive", "A calculation may read only its own runtime.");
        if (comparing)
            fail("reactive_comparator", "/reactive", "Equality comparators must not enter the reactive runtime.");
    }
    void mutation(const std::string& path = "/reactive") const {
        check();
        if (!stack.empty())
            fail("reactive_mutation", path, "Calculations must not write values or mutate graph/owner/batch storage.");
    }
    Node& node(std::uint64_t id) {
        const auto it = nodes.find(id);
        if (it == nodes.end()) fail("disposed_reactive_value", "/reactive/values/" + std::to_string(id),
                                    "Use a value from a live owner lifetime.");
        return it->second;
    }
    Owner& owner(std::uint64_t id) {
        const auto it = owners.find(id);
        if (it == owners.end()) fail("disposed_reactive_owner", "/reactive/owners/" + std::to_string(id),
                                     "Use a live owner lifetime.");
        return it->second;
    }
    void invalidate(std::uint64_t id) {
        // Iterative fan-out, including consumers already stale from an earlier write.
        std::vector<std::uint64_t> pending{id};
        std::set<std::uint64_t> visited;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto current = pending[i];
            if (!visited.insert(current).second) continue;
            auto& n = node(current);
            if (current != id) n.dirty = true;
            pending.insert(pending.end(), n.consumers.begin(), n.consumers.end());
        }
    }
    bool same(Node& n, const std::any& value) {
        comparing = true;
        auto* previous = active;
        active = this;
        try {
            const bool result = n.equal(n.value, value);
            comparing = false; active = previous;
            return result;
        } catch (...) {
            comparing = false; active = previous;
            try { throw; }
            catch (const ReactiveError&) { throw; }
            catch (const std::bad_alloc&) { throw; }
            catch (const std::exception& e) {
                fail("reactive_equality_failed", n.path, "Equality comparison failed: " + std::string(e.what()));
            } catch (...) {
                fail("reactive_equality_failed", n.path, "Equality comparison failed with a nonstandard exception.");
            }
        }
    }
    void refresh(std::uint64_t id) {
        auto& n = node(id);
        if (!n.calculate || !n.dirty) return;
        const auto cycle = std::find_if(stack.begin(), stack.end(), [id](const auto& c) { return c.id == id; });
        if (cycle != stack.end()) {
            std::string path;
            for (auto it = cycle; it != stack.end(); ++it) path += node(it->id).path + " -> ";
            fail("reactive_cycle", n.path, "Break the synchronous dependency cycle: " + path + n.path);
        }
        if (stack.size() >= max_reactive_depth)
            fail("reactive_depth", n.path, "Reduce nested evaluation to at most 32 calculations.");
        stack.push_back({id, {}});
        auto* previous = active;
        active = this;
        try {
            bool changed = !n.value.has_value() || n.failed;
            if (!changed) for (const auto& [dependency, revision] : n.dependencies) {
                if (!nodes.contains(dependency) || node(dependency).revision != revision) {
                    changed = true;
                    break;
                }
                // An obsolete branch may contain a failed prerequisite. Re-run
                // the consumer to discover whether it actually still needs it.
                try { refresh(dependency); }
                catch (const ReactiveError&) { changed = true; break; }
                if (node(dependency).revision != revision) { changed = true; break; }
            }
            if (changed) {
                auto value = n.calculate();
                const bool accept = !n.value.has_value() || !same(n, value);
                if (accept && n.revision == std::numeric_limits<std::uint64_t>::max())
                    fail("reactive_revision_exhausted", n.path, "Create a new runtime before revisions exhaust.");
                auto dependencies = std::move(stack.back().dependencies);
                for (const auto& [dependency, revision] : n.dependencies) {
                    (void)revision;
                    if (auto it = nodes.find(dependency); it != nodes.end()) it->second.consumers.erase(id);
                }
                for (const auto& [dependency, revision] : dependencies) {
                    (void)revision;
                    node(dependency).consumers.insert(id);
                }
                n.dependencies = std::move(dependencies);
                if (accept) {
                    n.value = std::move(value);
                    ++n.revision;
                    invalidate(id);
                }
            }
            n.dirty = false; n.failed = false;
            stack.pop_back(); active = previous;
        } catch (...) {
            n.failed = true; n.dirty = true;
            stack.pop_back(); active = previous;
            try { throw; }
            catch (const ReactiveError&) { throw; }
            catch (const std::bad_alloc&) { throw; }
            catch (const std::exception& e) {
                fail("reactive_calculation_failed", n.path, "Calculation failed: " + std::string(e.what()));
            } catch (...) {
                fail("reactive_calculation_failed", n.path, "Calculation failed with a nonstandard exception.");
            }
        }
    }
};
namespace {
std::shared_ptr<ReactiveState> lock(const ReactiveRef& ref) {
    auto state = ref.state.lock();
    if (!state) fail("expired_reactive_runtime", "/reactive", "Use a reference to a live reactive runtime.");
    state->check();
    return state;
}
std::uint64_t identity(std::uint64_t& next) {
    if (next == std::numeric_limits<std::uint64_t>::max())
        fail("reactive_identity_exhausted", "/reactive", "Create a new runtime before identities exhaust.");
    return next++;
}
}
std::any reactive_read(const ReactiveRef& ref) {
    auto state = lock(ref);
    state->refresh(ref.id);
    auto& n = state->node(ref.id);
    auto value = n.value;
    if (!state->stack.empty()) {
        auto& dependencies = state->stack.back().dependencies;
        if (std::none_of(dependencies.begin(), dependencies.end(), [&](const auto& d) { return d.first == ref.id; }))
            dependencies.emplace_back(ref.id, n.revision);
    }
    return value;
}
std::uint64_t reactive_revision(const ReactiveRef& ref) {
    auto state = lock(ref);
    state->refresh(ref.id);
    return state->node(ref.id).revision;
}
bool reactive_write(const ReactiveRef& ref, std::any value) {
    auto state = lock(ref);
    auto& n = state->node(ref.id);
    state->mutation(n.path);
    if (state->same(n, value)) return false;
    if (n.revision == std::numeric_limits<std::uint64_t>::max())
        fail("reactive_revision_exhausted", n.path, "Create a new runtime before revisions exhaust.");
    n.value = std::move(value); ++n.revision;
    state->invalidate(ref.id);
    return true;
}
void reactive_dispose(const ReactiveRef& ref) {
    auto state = lock(ref);
    state->mutation();
    if (!state->owners.contains(ref.id)) return;
    std::vector<std::uint64_t> closing;
    std::function<void(std::uint64_t)> visit = [&](std::uint64_t parent) {
        for (const auto& [id, owner] : state->owners)
            if (owner.parent == parent) visit(id);
        closing.push_back(parent);
    };
    visit(ref.id); // Child-first postorder; siblings in creation order.
    const std::set<std::uint64_t> closed(closing.begin(), closing.end());
    // Splice registered callbacks without allocation. Keep their owned captures
    // alive until the explicit host delivery, in child-first registration order.
    for (const auto owner : closing) {
        for (auto it = state->cleanups.begin(); it != state->cleanups.end();) {
            auto current = it++;
            if (current->owner == owner)
                state->pending_cleanups.splice(state->pending_cleanups.end(), state->cleanups, current);
        }
    }
    // Close every scope before destroying stored values/callables.
    for (const auto id : closing) state->owners.erase(id);
    for (const auto& [id, n] : state->nodes)
        if (closed.contains(n.owner)) state->invalidate(id);
    for (const auto owner : closing) {
        for (auto it = state->nodes.begin(); it != state->nodes.end();) {
            if (it->second.owner != owner) { ++it; continue; }
            const auto id = it->first;
            for (const auto& [dep, revision] : it->second.dependencies) {
                (void)revision;
                if (auto d = state->nodes.find(dep); d != state->nodes.end()) d->second.consumers.erase(id);
            }
            // Surviving consumers retain dead identities and must recalculate.
            it = state->nodes.erase(it);
        }
    }
}
} // namespace detail

ReactiveRuntime::ReactiveRuntime() : state_(std::make_shared<detail::ReactiveState>()) {}
ReactiveRuntime::~ReactiveRuntime() = default;
ReactiveOwner ReactiveRuntime::root() const {
    state_->check(); state_->owner(1);
    return ReactiveOwner({state_, 1});
}
ReactiveOwner ReactiveRuntime::owner(const ReactiveOwner& parent, std::string name) {
    state_->mutation();
    if (detail::lock(parent.ref_) != state_)
        detail::fail("foreign_reactive_owner", "/reactive", "Use an owner from this runtime.");
    const auto& p = state_->owner(parent.ref_.id);
    detail::validate_name(name);
    if (p.depth >= max_reactive_depth || state_->owners.size() >= max_reactive_owners)
        detail::fail("reactive_owner_limit", p.path, "Reduce live owner count or nesting depth.");
    const auto path = p.path + "/owners/" + detail::escaped(name);
    for (const auto& [id, owner] : state_->owners) {
        (void)id;
        if (owner.path == path) detail::fail("duplicate_reactive_name", path, "Use a unique sibling owner name.");
    }
    const auto id = detail::identity(state_->next_owner);
    state_->owners.emplace(id, detail::Owner{parent.ref_.id, path, p.depth + 1});
    return ReactiveOwner({state_, id});
}
void ReactiveRuntime::on_cleanup(const ReactiveOwner& owner, std::string name, std::function<void()> callback) {
    state_->mutation();
    if (detail::lock(owner.ref_) != state_)
        detail::fail("foreign_reactive_owner", "/reactive", "Use an owner from this runtime.");
    const auto& scope = state_->owner(owner.ref_.id);
    detail::validate_name(name);
    const auto path = scope.path + "/cleanups/" + detail::escaped(name);
    if (!callback)
        detail::fail("invalid_reactive_cleanup", path, "Supply a callable external cleanup operation.");
    if (state_->cleanups.size() + state_->pending_cleanups.size() >= max_reactive_cleanups)
        detail::fail("reactive_cleanup_limit", path, "Keep live and pending cleanup registrations within 4096; deliver queued cleanups to release capacity.");
    for (const auto& cleanup : state_->cleanups)
        if (cleanup.path == path)
            detail::fail("duplicate_reactive_name", path, "Use a unique cleanup name within its owner.");
    state_->cleanups.push_back({owner.ref_.id, path, std::move(callback)});
}
std::vector<Diagnostic> ReactiveRuntime::deliver_cleanups() {
    auto state = state_;
    state->mutation();
    if (state->batches)
        detail::fail("reactive_open_batch", "/reactive", "Close the outer batch before delivering external cleanups.");
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(state->pending_cleanups.size());
    auto* previous = detail::active;
    detail::active = state.get();
    state->delivering_cleanups = true;
    try {
        while (!state->pending_cleanups.empty()) {
            auto cleanup = std::move(state->pending_cleanups.front());
            state->pending_cleanups.pop_front(); // Consume before invocation, even on failure.
            try { cleanup.callback(); }
            catch (const std::bad_alloc&) { throw; }
            catch (const std::exception& e) {
                diagnostics.push_back({"reactive_cleanup_failed", Severity::error, cleanup.path,
                    "Cleanup failed and will not be retried: " + std::string(e.what()), {}});
            } catch (...) {
                diagnostics.push_back({"reactive_cleanup_failed", Severity::error, cleanup.path,
                    "Cleanup failed with a nonstandard exception and will not be retried.", {}});
            }
        }
        state->delivering_cleanups = false;
        detail::active = previous;
    } catch (...) {
        state->delivering_cleanups = false;
        detail::active = previous;
        throw;
    }
    return diagnostics;
}
detail::ReactiveRef ReactiveRuntime::add(const ReactiveOwner& owner, std::string name, std::any value,
    std::function<std::any()> calculate, detail::ReactiveEqual equal) {
    state_->mutation();
    if (detail::lock(owner.ref_) != state_)
        detail::fail("foreign_reactive_owner", "/reactive", "Use an owner from this runtime.");
    const auto& scope = state_->owner(owner.ref_.id);
    detail::validate_name(name);
    const auto path = scope.path + "/values/" + detail::escaped(name);
    if (state_->nodes.size() >= max_reactive_nodes)
        detail::fail("reactive_node_limit", path, "Reduce the live value count to at most 4096.");
    for (const auto& [id, n] : state_->nodes) {
        (void)id;
        if (n.path == path) detail::fail("duplicate_reactive_name", path, "Use a unique value name within its owner.");
    }
    const auto id = detail::identity(state_->next_node);
    detail::Node node;
    node.owner = owner.ref_.id; node.path = path; node.value = std::move(value);
    node.revision = calculate ? 0 : 1;
    node.calculate = std::move(calculate); node.equal = std::move(equal);
    state_->nodes.emplace(id, std::move(node));
    return {state_, id};
}
void ReactiveRuntime::begin_batch() {
    state_->mutation();
    if (state_->batches >= max_reactive_depth)
        detail::fail("reactive_batch_limit", "/reactive", "Reduce nested batches to at most 32.");
    ++state_->batches;
}
void ReactiveRuntime::end_batch() {
    state_->mutation();
    if (state_->batches == 0)
        detail::fail("reactive_batch_unbalanced", "/reactive", "Close only a matching open batch.");
    --state_->batches;
}
void ReactiveRuntime::flush() {
    state_->check();
    if (state_->flushing || !state_->stack.empty())
        detail::fail("reactive_reentrant_flush", "/reactive", "Call flush after calculation and the prior flush return.");
    if (state_->batches)
        detail::fail("reactive_open_batch", "/reactive", "Close the outer batch before flushing scheduled calculations.");
    state_->flushing = true;
    try {
        for (const auto& [id, node] : state_->nodes) {
            (void)node;
            state_->refresh(id);
        }
        state_->flushing = false;
    } catch (...) { state_->flushing = false; throw; }
}
} // namespace tessera
