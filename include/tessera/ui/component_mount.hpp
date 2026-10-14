#pragma once

#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/async.hpp>
#include <set>

namespace tessera {
using ComponentStateValue = std::variant<Signal<bool>, Signal<double>,
    Signal<std::string>, Signal<NodeReference>>;
using ComponentCleanupBindings = std::map<std::string, std::function<void()>, std::less<>>;
using ComponentAsyncBindings = std::map<std::string,
    std::function<void(const AsyncAttempt&)>, std::less<>>;

// Owned identity and weak state handles for one fresh lifetime. Copies may be
// retained by the host; they neither retain the owner nor refresh after reset.
class ComponentSetup final {
public:
    const KeyedInstance& instance() const noexcept { return instance_; }
    Result<AsyncValue> async(std::string_view name) const;
    template<class T>
    Result<Signal<T>> state(std::string_view name) const {
        static_assert(std::is_same_v<T, bool> || std::is_same_v<T, double> ||
                      std::is_same_v<T, std::string> || std::is_same_v<T, NodeReference>,
                      "Component state uses the four exact semantic property types.");
        const auto found = values_.find(name);
        if (found == values_.end()) return {{}, {error("component_unknown_state", name,
            "Use a declared local-state name.")}};
        if (const auto* typed = std::get_if<Signal<T>>(&found->second)) return {*typed, {}};
        return {{}, {error("component_state_type", name,
            "Request the declared exact state type; values are not coerced.")}};
    }
private:
    friend class ComponentCollection;
    friend class ComponentTreeCollection;
    ComponentSetup(KeyedInstance instance, const std::map<std::string, ComponentStateValue, std::less<>>& values,
        const std::map<std::string, AsyncValue, std::less<>>& async_values)
        : instance_(std::move(instance)), values_(values), async_values_(async_values) {}
    Diagnostic error(std::string code, std::string_view name, std::string message) const;
    KeyedInstance instance_;
    std::map<std::string, ComponentStateValue, std::less<>> values_;
    std::map<std::string, AsyncValue, std::less<>> async_values_;
};

using ComponentEffectCalculation = std::variant<std::function<bool()>, std::function<double()>,
    std::function<std::string()>, std::function<NodeReference()>>;
struct ComponentEffectBinding {
    ComponentEffectCalculation calculate;
    std::function<std::function<void()>(const Property&)> apply;
};
// Factories run once after fresh setup, inside the mounting batch. They return
// pure tracked calculations and external applications, never perform delivery.
using ComponentEffectBindings = std::map<std::string,
    std::function<ComponentEffectBinding(const ComponentSetup&)>, std::less<>>;

template<class T, class Calculate, class Apply>
ComponentEffectBinding component_effect(Calculate calculate, Apply apply) {
    static_assert(std::is_same_v<T, bool> || std::is_same_v<T, double> ||
                  std::is_same_v<T, std::string> || std::is_same_v<T, NodeReference>,
                  "Component effects use the four exact semantic property types.");
    static_assert(std::is_same_v<std::invoke_result_t<Calculate&>, T>,
                  "Effect calculations must return the declared exact type.");
    return {std::function<T()>(std::move(calculate)),
        [apply = std::move(apply)](const Property& value) mutable -> std::function<void()> {
            const auto& typed = std::get<T>(value);
            if constexpr (std::is_void_v<std::invoke_result_t<Apply&, const T&>>) {
                std::invoke(apply, typed);
                return {};
            } else return std::function<void()>(std::invoke(apply, typed));
        }};
}

// Borrows a description only during reconciliation; mounting registers its kind
// and state/cleanup/effect schemas. Lowering validates presentation separately.
struct ComponentMount {
    std::string key;
    const ComponentDescription* component = nullptr;
    // Runs once after fresh state registration, in candidate order inside the
    // mounting batch. Register work on setup.instance().owner; publication and
    // external work remain explicit host operations. Never retained by mounting.
    std::function<void(ReactiveRuntime&, const ComponentSetup&)> setup;
    // One nonempty operation for each declared cleanup. Fresh lifetimes retain
    // copies; compatible reuse keeps the original operations until disposal.
    ComponentCleanupBindings cleanups;
    ComponentEffectBindings effects;
    ComponentAsyncBindings async;
};

// One keyed sibling collection beneath a host scope. Compatible schemas match
// by name and exact type. Change kind to reset an incompatible component schema.
// Reconciliation controls child owner disposal; the host disposes the scope.
// Destruction releases weak records; dispose the host scope for external cleanup.
class ComponentCollection final {
public:
    explicit ComponentCollection(ReactiveOwner scope) : keyed_(std::move(scope)) {}
    ComponentCollection(const ComponentCollection&) = delete;
    ComponentCollection& operator=(const ComponentCollection&) = delete;
    ComponentCollection(ComponentCollection&&) = default;
    ComponentCollection& operator=(ComponentCollection&&) = default;

    // Invalid declarations/schema changes reject the entire candidate before
    // runtime entry. Runtime/setup failures throw and keep records truthful; they are
    // not reload transactions. Never settles, lowers or publishes automatically.
    Result<KeyedUpdate> reconcile(ReactiveRuntime&, std::span<const ComponentMount>);
    const std::vector<KeyedInstance>& instances() const noexcept { return keyed_.instances(); }
    const KeyedInstance* find(std::string_view key) const noexcept { return keyed_.find(key); }
    bool current(std::string_view key, std::uint64_t lifetime) const noexcept { return keyed_.current(key, lifetime); }
    Result<AsyncValue> async(std::string_view key, std::string_view name) const;

    // Returns a weak, exact-type source without reading it. Copied handles expire
    // on owner disposal, including when a key is subsequently recreated.
    template<class T>
    Result<Signal<T>> state(std::string_view key, std::string_view name) const {
        static_assert(std::is_same_v<T, bool> || std::is_same_v<T, double> ||
                      std::is_same_v<T, std::string> || std::is_same_v<T, NodeReference>,
                      "Component state uses the four exact semantic property types.");
        auto value = lookup(key, name);
        if (!value) return {{}, std::move(value.diagnostics)};
        if (const auto* typed = std::get_if<Signal<T>>(*value.value)) return {*typed, {}};
        return {{}, {state_error("component_state_type", key, name,
            "Request the declared exact state type; values are not coerced.")}};
    }
private:
    struct Record {
        std::uint64_t lifetime = 0;
        std::map<std::string, ComponentStateValue, std::less<>> values;
        std::set<std::string, std::less<>> cleanups;
        std::map<std::string, std::pair<PropertyType, EffectPhase>, std::less<>> effects;
        std::map<std::string, PropertyType, std::less<>> async_schema;
        std::map<std::string, AsyncValue, std::less<>> async_values;
    };
    Result<const ComponentStateValue*> lookup(std::string_view key, std::string_view name) const;
    static Diagnostic state_error(std::string code, std::string_view key,
                                  std::string_view name, std::string message);
    void prune(ReactiveRuntime&);
    friend class ComponentTreeCollection;
    KeyedCollection keyed_;
    std::map<std::string, Record, std::less<>> records_;
    bool reconciling_ = false;
};
} // namespace tessera
