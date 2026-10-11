#pragma once

#include <tessera/reactive/runtime.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tessera {
inline constexpr std::size_t max_keyed_children = max_reactive_owners;

// One requested child of a keyed sibling collection, in presentation order.
struct KeyedChild {
    std::string key;  // Unique within the collection; names the instance owner.
    std::string kind; // An instance is reused only for an equal kind.
    bool operator==(const KeyedChild&) const = default;
};

// A live logical instance. Its owner holds the instance's local state,
// bindings, effects, and cleanup registrations.
struct KeyedInstance {
    std::string key;
    std::string kind;
    std::uint64_t lifetime = 0; // Allocated by the collection; never reused.
    ReactiveOwner owner;
};

struct KeyedUpdate {
    std::vector<KeyedInstance> instances; // Candidate order.
    std::vector<std::size_t> created;     // Indices into instances, ascending.
    std::vector<KeyedInstance> disposed;  // Removed or replaced, in previous order.
};

// Reconciles one sibling collection against owners created beneath `scope`.
// Use one collection per scope; the collection never owns the scope itself.
class KeyedCollection final {
public:
    KeyedCollection() = default;
    explicit KeyedCollection(ReactiveOwner scope) : scope_(std::move(scope)) {}
    KeyedCollection(const KeyedCollection&) = delete;
    KeyedCollection& operator=(const KeyedCollection&) = delete;
    KeyedCollection(KeyedCollection&&) = default;
    KeyedCollection& operator=(KeyedCollection&&) = default;

    // Invalid candidates return diagnostics and change nothing. Runtime entry
    // failures throw ReactiveError; see the UI model contract for their effect.
    Result<KeyedUpdate> reconcile(ReactiveRuntime&, const std::vector<KeyedChild>&);
    const std::vector<KeyedInstance>& instances() const noexcept { return instances_; }
    const KeyedInstance* find(std::string_view key) const noexcept;
    // Whether `lifetime` still names the live instance for `key`; results
    // addressed to an earlier lifetime are late and must be dropped.
    bool current(std::string_view key, std::uint64_t lifetime) const noexcept;

private:
    ReactiveOwner scope_;
    std::vector<KeyedInstance> instances_;
    std::uint64_t next_lifetime_ = 1;
};
} // namespace tessera
