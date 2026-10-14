#include <tessera/ui/component_tree_mount.hpp>
#include "component_tree_detail.hpp"
#include "component_state_detail.hpp"
#include "json_detail.hpp"
#include <algorithm>
#include <limits>

namespace tessera {
struct ComponentTreeCollection::Impl {
    struct Group;
    struct Node {
        std::uint64_t local_lifetime;
        ComponentSetup context;
        std::vector<std::pair<std::string, std::unique_ptr<Group>>> slots;
        std::uint64_t next_slot = 0;
    };
    struct Group {
        ReactiveOwner scope;
        ComponentCollection collection;
        std::map<std::string, Node, std::less<>> nodes;
        explicit Group(ReactiveOwner owner) : scope(owner), collection(std::move(owner)) {}
    };
    Group roots;
    std::uint64_t next_lifetime = 1;
    bool reconciling = false;
    explicit Impl(ReactiveOwner scope) : roots(std::move(scope)) {}

    static Group* slot(Node& node, std::string_view name) {
        for (auto& [key, group] : node.slots) if (key == name) return group.get();
        return nullptr;
    }
    static void prune(Group& group, ReactiveRuntime& runtime) {
        group.collection.prune(runtime);
        std::erase_if(group.nodes, [&](const auto& entry) {
            return !group.collection.current(entry.first, entry.second.local_lifetime);
        });
        for (auto& [key, node] : group.nodes) {
            (void)key;
            for (auto& [name, children] : node.slots) {
                (void)name;
                prune(*children, runtime);
            }
            std::erase_if(node.slots, [](const auto& entry) {
                if (!entry.second->nodes.empty()) return false;
                entry.second->scope.dispose();
                return true;
            });
        }
    }
    static void locate(std::vector<Diagnostic>& diagnostics, std::span<const std::size_t> indices) {
        for (auto& diagnostic : diagnostics) {
            for (std::size_t j = 0; j < indices.size(); ++j) {
                const auto prefix = "/components/" + std::to_string(j);
                if (diagnostic.path == prefix || diagnostic.path.starts_with(prefix + "/")) {
                    diagnostic.path = "/invocations/" + std::to_string(indices[j]) + diagnostic.path.substr(prefix.size());
                    break;
                }
            }
        }
    }
    static void name(std::vector<Diagnostic>& errors, std::string_view value, std::string path) {
        const bool controls = std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
        if (value.empty() || value.size() > 256 || controls || !detail::valid_utf8(value))
            errors.push_back({"component_name", Severity::error, std::move(path),
                "Supply 1 to 256 UTF-8 bytes without control characters.", {}});
    }
    // Examine accepted schemas only along compatible parent/slot/key paths.
    // A replaced ancestor recreates its complete subtree instead.
    static void compatibility(Group* group, std::span<const std::size_t> indices,
        std::span<const ComponentInvocation> invocations, std::span<const ComponentInvocationMount> mounts,
        std::vector<Diagnostic>& errors) {
        std::set<std::string_view, std::less<>> keys;
        for (const auto i : indices) {
            const auto path = "/invocations/" + std::to_string(i);
            if (!keys.insert(mounts[i].key).second)
                errors.push_back({"duplicate_key", Severity::error, path + "/key", "Use each key once within its parent slot.", {}});
            Node* accepted = nullptr;
            if (group) {
                const auto found = group->nodes.find(mounts[i].key);
                if (found != group->nodes.end() && found->second.context.instance().kind == invocations[i].component.kind)
                    accepted = &found->second;
            }
            if (accepted) {
                const auto& schema = invocations[i].component.local_state;
                bool compatible = schema.size() == accepted->context.values_.size();
                for (const auto& state : schema) {
                    const auto found = accepted->context.values_.find(state.name);
                    if (found == accepted->context.values_.end() || found->second.index() != state.initial_value.index()) compatible = false;
                }
                if (!compatible) errors.push_back({"component_state_schema", Severity::error, path + "/local_state",
                    "Preserve named state types or change the component kind to recreate this subtree.", {}});
                const auto& declarations = invocations[i].component.cleanups;
                const std::set<std::string, std::less<>> names(declarations.begin(), declarations.end());
                if (names != group->collection.records_.at(mounts[i].key).cleanups)
                    errors.push_back({"component_cleanup_schema", Severity::error, path + "/cleanups",
                        "Preserve named cleanup declarations or change kind to recreate this subtree.", {}});
                std::map<std::string, std::pair<PropertyType, EffectPhase>, std::less<>> effects;
                for (const auto& effect : invocations[i].component.effects) effects.emplace(effect.name, std::pair{effect.type, effect.phase});
                if (effects != group->collection.records_.at(mounts[i].key).effects)
                    errors.push_back({"component_effect_schema", Severity::error, path + "/effects",
                        "Preserve named effect types and phases or change kind to recreate this subtree.", {}});
            }
            for (const auto& declaration : invocations[i].component.slots) {
                const auto found = invocations[i].invocation_slots.find(declaration.name);
                if (found != invocations[i].invocation_slots.end())
                    compatibility(accepted ? slot(*accepted, declaration.name) : nullptr, found->second, invocations, mounts, errors);
            }
        }
    }

    void mount(Group& group, std::span<const std::size_t> indices, ReactiveRuntime& runtime,
        std::span<const ComponentInvocation> invocations, std::span<const ComponentInvocationMount> mounts,
        std::vector<std::optional<ComponentSetup>>& contexts, std::vector<std::size_t>& created,
        std::vector<ReactiveOwner>& fresh) {
        std::vector<ComponentMount> candidate;
        candidate.reserve(indices.size());
        for (const auto i : indices) {
            ComponentEffectBindings effects;
            for (const auto& [name, factory] : mounts[i].effects) {
                (void)factory;
                effects.emplace(name, [&, i, name](const ComponentSetup&) {
                    // Give factories the same tree-issued lifetime as initial setup.
                    return mounts[i].effects.at(name)(group.nodes.at(mounts[i].key).context);
                });
            }
            candidate.push_back({mounts[i].key, &invocations[i].component,
            [&, i](ReactiveRuntime& r, const ComponentSetup& setup) {
                fresh.push_back(setup.instance().owner);
                auto identity = setup.instance();
                identity.lifetime = next_lifetime++;
                ComponentSetup context(std::move(identity), setup.values_);
                group.nodes.insert_or_assign(mounts[i].key, Node{setup.instance().lifetime, context, {}, 0});
                created.push_back(i);
                if (mounts[i].setup) {
                    try { mounts[i].setup(r, context); }
                    catch (const std::bad_alloc&) { throw; }
                    catch (const ReactiveError&) { throw; }
                    catch (const std::exception& e) {
                        throw ReactiveError({"component_setup_failure", Severity::error,
                            "/invocations/" + std::to_string(i) + "/setup", e.what(), {}});
                    } catch (...) {
                        throw ReactiveError({"component_setup_failure", Severity::error,
                            "/invocations/" + std::to_string(i) + "/setup", "Component setup threw an unknown exception.", {}});
                    }
                }
            }, mounts[i].cleanups, std::move(effects)});
        }
        Result<KeyedUpdate> update;
        try { update = group.collection.reconcile(runtime, candidate); }
        catch (const ReactiveError& e) {
            std::vector<Diagnostic> errors{e.diagnostic()};
            locate(errors, indices);
            throw ReactiveError(std::move(errors.front()));
        }
        if (!update) {
            auto errors = update.diagnostics;
            locate(errors, indices);
            throw ReactiveError(errors.front());
        }
        std::erase_if(group.nodes, [&](const auto& entry) {
            return !group.collection.current(entry.first, entry.second.local_lifetime);
        });
        for (const auto i : indices) {
            auto& node = group.nodes.at(mounts[i].key);
            contexts[i] = node.context;
            const auto& invocation = invocations[i];
            // Removed slots close in their previous declaration order.
            std::erase_if(node.slots, [&](const auto& entry) {
                const auto found = invocation.invocation_slots.find(entry.first);
                if (found != invocation.invocation_slots.end() && !found->second.empty()) return false;
                entry.second->scope.dispose();
                return true;
            });
            for (const auto& declaration : invocation.component.slots) {
                const auto found = invocation.invocation_slots.find(declaration.name);
                if (found == invocation.invocation_slots.end() || found->second.empty()) continue;
                auto* children = slot(node, declaration.name);
                if (!children) {
                    auto scope = runtime.owner(node.context.instance().owner, "__tessera_slot_" + std::to_string(node.next_slot++));
                    try {
                        node.slots.emplace_back(declaration.name, std::make_unique<Group>(scope));
                    } catch (...) { scope.dispose(); throw; }
                    children = node.slots.back().second.get();
                }
                mount(*children, found->second, runtime, invocations, mounts, contexts, created, fresh);
            }
            // Keep removal order aligned to the accepted slot declaration order.
            std::stable_sort(node.slots.begin(), node.slots.end(), [&](const auto& a, const auto& b) {
                const auto rank = [&](const std::string& key) {
                    return std::find_if(invocation.component.slots.begin(), invocation.component.slots.end(),
                        [&](const auto& s) { return s.name == key; }) - invocation.component.slots.begin();
                };
                return rank(a.first) < rank(b.first);
            });
        }
    }
};

ComponentTreeCollection::ComponentTreeCollection(ReactiveOwner scope) : impl_(std::make_unique<Impl>(std::move(scope))) {}
ComponentTreeCollection::~ComponentTreeCollection() = default;
ComponentTreeCollection::ComponentTreeCollection(ComponentTreeCollection&&) noexcept = default;
ComponentTreeCollection& ComponentTreeCollection::operator=(ComponentTreeCollection&&) noexcept = default;

Result<ComponentTreeUpdate> ComponentTreeCollection::reconcile(ReactiveRuntime& runtime,
    std::span<const ComponentInvocation> invocations, std::span<const ComponentInvocationMount> mounts, std::size_t root) {
    if (!impl_ || impl_->reconciling) return {{}, {{"component_setup_reentry", Severity::error, "/invocations",
        "Use a live collection after its setup operation returns.", {}}}};
    struct Guard { bool& active; ~Guard() { active = false; } } guard{impl_->reconciling};
    impl_->reconciling = true;
    if (invocations.size() != mounts.size()) return {{}, {{"component_mount_count", Severity::error, "/invocations",
        "Supply one key/setup record per invocation.", {}}}};
    if (!invocations.empty()) {
        auto order = detail::component_tree_order(invocations, root);
        if (!order) return {{}, std::move(order.diagnostics)};
    } else if (root != 0) return {{}, {{"component_invocation_index", Severity::error, "/root", "Use root zero for empty input.", {}}}};
    std::vector<Diagnostic> errors;
    std::size_t states = 0;
    std::size_t cleanups = 0;
    std::size_t effects = 0;
    for (std::size_t i = 0; i < invocations.size(); ++i) {
        const auto path = "/invocations/" + std::to_string(i);
        const auto& component = invocations[i].component;
        Impl::name(errors, mounts[i].key, path + "/key");
        Impl::name(errors, component.kind, path + "/kind");
        auto schema = detail::validate_component_local_state(component.local_state, path + "/local_state");
        errors.insert(errors.end(), schema.begin(), schema.end());
        auto operations = detail::validate_component_cleanup_bindings(component.cleanups, mounts[i].cleanups, path + "/cleanups");
        errors.insert(errors.end(), operations.begin(), operations.end());
        auto effect_errors = detail::validate_component_effect_bindings(component.effects, mounts[i].effects, path + "/effects");
        errors.insert(errors.end(), effect_errors.begin(), effect_errors.end());
        if (component.effects.size() <= max_component_effects) effects += component.effects.size();
        if (component.cleanups.size() <= max_component_cleanups) cleanups += component.cleanups.size();
        states += component.local_state.size();
        std::set<std::string_view, std::less<>> names;
        for (const auto& slot : component.slots) {
            Impl::name(errors, slot.name, path + "/slots/" + detail::pointer_token(slot.name));
            if (!names.insert(slot.name).second) errors.push_back({"component_duplicate_slot", Severity::error,
                path + "/slots/" + detail::pointer_token(slot.name), "Declare each slot once.", {}});
        }
    }
    if (states > max_reactive_nodes) errors.push_back({"component_state_limit", Severity::error, "/invocations",
        "Keep all invocation state slots within the runtime's 4096 value budget.", {}});
    if (cleanups > max_reactive_cleanups) errors.push_back({"component_cleanup_limit", Severity::error, "/invocations",
        "Keep all invocation cleanup operations within the runtime's 4096 live and pending budget.", {}});
    if (states + effects > max_reactive_nodes || cleanups + effects > max_reactive_cleanups)
        errors.push_back({"component_effect_limit", Severity::error, "/invocations",
            "State plus effects and cleanup plus effects must each fit the runtime's 4096-entry budgets.", {}});
    const std::span<const std::size_t> roots = invocations.empty() ? std::span<const std::size_t>{} : std::span<const std::size_t>{&root, 1};
    if (errors.empty()) Impl::compatibility(&impl_->roots, roots, invocations, mounts, errors);
    if (!errors.empty()) return {{}, std::move(errors)};
    if (impl_->next_lifetime - 1 > std::numeric_limits<std::uint64_t>::max() - invocations.size())
        return {{}, {{"keyed_lifetime_exhausted", Severity::error, "/invocations", "Create a new tree collection before lifetimes exhaust.", {}}}};

    ComponentTreeUpdate result;
    result.instances.reserve(invocations.size());
    result.created.reserve(invocations.size());
    std::vector<std::optional<ComponentSetup>> contexts(invocations.size());
    std::vector<ReactiveOwner> fresh;
    fresh.reserve(invocations.size());
    runtime.batch([&] {
        (void)runtime.faulted(impl_->roots.scope);
        try {
            impl_->mount(impl_->roots, roots, runtime, invocations, mounts, contexts, result.created, fresh);
            for (auto& context : contexts) result.instances.push_back(std::move(*context));
        } catch (...) {
            for (const auto& owner : fresh) owner.dispose();
            Impl::prune(impl_->roots, runtime);
            throw;
        }
    });
    return {std::move(result), {}};
}
} // namespace tessera
