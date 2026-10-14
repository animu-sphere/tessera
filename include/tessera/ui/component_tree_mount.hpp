#pragma once

#include <tessera/ui/component_mount.hpp>

namespace tessera {
// Parallel to ComponentInvocation input; keys are scoped to the parent and slot.
// Registration callbacks are borrowed and run once for each fresh lifetime.
struct ComponentInvocationMount {
    std::string key;
    std::function<void(ReactiveRuntime&, const ComponentSetup&)> setup;
    ComponentCleanupBindings cleanups;
    ComponentEffectBindings effects;
};
struct ComponentTreeUpdate {
    std::vector<ComponentSetup> instances; // Input index order; owned weak contexts.
    std::vector<std::size_t> created; // Setup order: parent, declared slots, children.
};

// A keyed invocation tree beneath a host-owned scope. The host disposes that
// scope and explicitly settles, lowers, accepts, publishes and delivers cleanup.
// Slot scopes use reserved child owner names beginning with __tessera_slot_.
class ComponentTreeCollection final {
public:
    explicit ComponentTreeCollection(ReactiveOwner scope);
    ~ComponentTreeCollection();
    ComponentTreeCollection(const ComponentTreeCollection&) = delete;
    ComponentTreeCollection& operator=(const ComponentTreeCollection&) = delete;
    ComponentTreeCollection(ComponentTreeCollection&&) noexcept;
    ComponentTreeCollection& operator=(ComponentTreeCollection&&) noexcept;

    // Validates complete topology, geometry, keys and retained state schemas
    // before mutation. Runtime/setup failures repair records but do not roll
    // back removals or host writes. Empty input unmounts the accepted root.
    Result<ComponentTreeUpdate> reconcile(ReactiveRuntime&, std::span<const ComponentInvocation>,
        std::span<const ComponentInvocationMount>, std::size_t root = 0);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace tessera
