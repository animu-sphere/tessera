#pragma once

#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>

namespace tessera {
// Borrows a description only during reconciliation; mounting registers its kind
// and local-state schema. Ordinary lowering validates the presentation separately.
struct ComponentMount {
    std::string key;
    const ComponentDescription* component = nullptr;
};
using ComponentStateValue = std::variant<Signal<bool>, Signal<double>,
    Signal<std::string>, Signal<NodeReference>>;

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
    // runtime entry. Runtime failures throw and keep records truthful; they are
    // not reload transactions. Never settles, lowers or publishes automatically.
    Result<KeyedUpdate> reconcile(ReactiveRuntime&, std::span<const ComponentMount>);
    const std::vector<KeyedInstance>& instances() const noexcept { return keyed_.instances(); }
    const KeyedInstance* find(std::string_view key) const noexcept { return keyed_.find(key); }
    bool current(std::string_view key, std::uint64_t lifetime) const noexcept { return keyed_.current(key, lifetime); }

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
    };
    Result<const ComponentStateValue*> lookup(std::string_view key, std::string_view name) const;
    static Diagnostic state_error(std::string code, std::string_view key,
                                  std::string_view name, std::string message);
    void prune();
    KeyedCollection keyed_;
    std::map<std::string, Record, std::less<>> records_;
};
} // namespace tessera
