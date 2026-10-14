#include <tessera/ui/component_mount.hpp>
#include "component_state_detail.hpp"
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>
#include <new>

namespace tessera {
namespace {
void error(std::vector<Diagnostic>& errors, std::string code, std::string path, std::string message) {
    errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
void name(std::vector<Diagnostic>& errors, std::string_view value, const std::string& path) {
    const bool controls = std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    if (value.empty() || value.size() > 256 || controls || !detail::valid_utf8(value))
        error(errors, "component_name", path, "Supply a name of 1 to 256 UTF-8 bytes without control characters.");
}
ComponentStateValue placeholder(const Property& initial) {
    return std::visit([](const auto& value) -> ComponentStateValue {
        return Signal<std::decay_t<decltype(value)>>{};
    }, initial);
}
ComponentStateValue create(ReactiveRuntime& runtime, const ReactiveOwner& owner, const ComponentLocalState& slot) {
    return std::visit([&](const auto& value) -> ComponentStateValue {
        return runtime.signal(owner, slot.name, value);
    }, slot.initial_value);
}
} // namespace

std::vector<Diagnostic> detail::validate_component_local_state(std::span<const ComponentLocalState> slots,
                                                             const std::string& path) {
    std::vector<Diagnostic> errors;
    if (slots.size() > max_component_local_state) {
        error(errors, "component_state_limit", path, "Declare at most 256 local-state slots per component.");
        return errors;
    }
    std::set<std::string_view, std::less<>> names;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto& slot = slots[i];
        const auto location = path + "/" + std::to_string(i);
        name(errors, slot.name, location + "/name");
        if (!names.insert(slot.name).second)
            error(errors, "component_duplicate_state", location + "/name", "Declare each local-state name once.");
        if (slot.type != property_type(slot.initial_value))
            error(errors, "component_state_type", location + "/initial", "Use the declared exact state type; values are not coerced.");
        if (const auto* text = std::get_if<std::string>(&slot.initial_value)) {
            if (text->size() > max_component_state_text_bytes)
                error(errors, "component_state_limit", location + "/initial", "Use an initial string of at most 4096 UTF-8 bytes.");
            else if (!valid_utf8(*text))
                error(errors, "invalid_utf8", location + "/initial", "Use valid UTF-8 text.");
        }
        if (const auto* number = std::get_if<double>(&slot.initial_value); number && !std::isfinite(*number))
            error(errors, "invalid_number", location + "/initial", "Use a finite initial number.");
        if (const auto* ref = std::get_if<NodeReference>(&slot.initial_value)) name(errors, ref->id, location + "/initial/ref");
    }
    return errors;
}

std::vector<Diagnostic> detail::validate_component_cleanups(std::span<const std::string> declarations,
                                                          const std::string& path) {
    std::vector<Diagnostic> errors;
    if (declarations.size() > max_component_cleanups) {
        error(errors, "component_cleanup_limit", path, "Declare at most 256 cleanup operations per component.");
        return errors;
    }
    std::set<std::string_view, std::less<>> names;
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        const auto location = path + "/" + std::to_string(i);
        name(errors, declarations[i], location);
        if (!names.insert(declarations[i]).second)
            error(errors, "component_duplicate_cleanup", location, "Declare each cleanup name once.");
    }
    return errors;
}

std::vector<Diagnostic> detail::validate_component_cleanup_bindings(std::span<const std::string> declarations,
    const ComponentCleanupBindings& bindings, const std::string& path) {
    auto errors = validate_component_cleanups(declarations, path);
    if (bindings.size() > max_component_cleanups) {
        error(errors, "component_cleanup_limit", path, "Bind at most 256 cleanup operations per component.");
        return errors;
    }
    if (!errors.empty()) return errors;
    std::size_t index = 0;
    for (const auto& [key, callback] : bindings) {
        (void)callback;
        name(errors, key, path + "/bindings/" + std::to_string(index++));
    }
    if (!errors.empty()) return errors;
    const std::set<std::string_view, std::less<>> names(declarations.begin(), declarations.end());
    for (const auto& declaration : declarations) {
        const auto found = bindings.find(declaration);
        if (found == bindings.end() || !found->second)
            error(errors, "component_cleanup_binding", path + "/" + pointer_token(declaration),
                "Supply a nonempty host operation for each declared cleanup.");
    }
    for (const auto& [key, callback] : bindings) {
        (void)callback;
        if (!names.contains(key))
            error(errors, "component_unknown_cleanup", path + "/" + pointer_token(key),
                "Bind only declared cleanup names.");
    }
    return errors;
}

std::vector<Diagnostic> detail::validate_component_effects(std::span<const ComponentEffect> declarations,
    const std::string& path) {
    std::vector<Diagnostic> errors;
    if (declarations.size() > max_component_effects) {
        error(errors, "component_effect_limit", path, "Declare at most 256 effects per component.");
        return errors;
    }
    std::set<std::string_view, std::less<>> names;
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        const auto& declaration = declarations[i];
        const auto location = path + "/" + std::to_string(i);
        name(errors, declaration.name, location + "/name");
        if (!names.insert(declaration.name).second)
            error(errors, "component_duplicate_effect", location + "/name", "Declare each effect name once.");
        if (declaration.type != PropertyType::boolean && declaration.type != PropertyType::number &&
            declaration.type != PropertyType::string && declaration.type != PropertyType::reference)
            error(errors, "component_effect_type", location + "/type", "Declare one of the four exact semantic property types.");
        if (declaration.phase != EffectPhase::prepare && declaration.phase != EffectPhase::notify)
            error(errors, "component_effect_phase", location + "/phase", "Use prepare or notify delivery.");
    }
    return errors;
}

std::vector<Diagnostic> detail::validate_component_effect_bindings(std::span<const ComponentEffect> declarations,
    const ComponentEffectBindings& bindings, const std::string& path) {
    auto errors = validate_component_effects(declarations, path);
    if (bindings.size() > max_component_effects) {
        error(errors, "component_effect_limit", path, "Bind at most 256 effect factories per component.");
        return errors;
    }
    if (!errors.empty()) return errors;
    std::set<std::string_view, std::less<>> names;
    for (const auto& declaration : declarations) {
        names.insert(declaration.name);
        const auto found = bindings.find(declaration.name);
        if (found == bindings.end() || !found->second)
            error(errors, "component_effect_binding", path + "/" + pointer_token(declaration.name),
                "Supply a nonempty host factory for each declared effect.");
    }
    std::size_t index = 0;
    for (const auto& [key, factory] : bindings) {
        (void)factory;
        name(errors, key, path + "/bindings/" + std::to_string(index++));
        if (!names.contains(key))
            error(errors, "component_unknown_effect", path + "/" + pointer_token(key), "Bind only declared effect names.");
    }
    return errors;
}

void ComponentCollection::prune(ReactiveRuntime& runtime) {
    // Keyed reconciliation can fail after committing owner replacements (for
    // example, while allocating its returned records). Do not retain an owner
    // whose local state was never registered by this collection.
    std::erase_if(keyed_.instances_, [&](const KeyedInstance& instance) {
        try { (void)runtime.faulted(instance.owner); }
        catch (const ReactiveError& e) {
            if (e.diagnostic().code == "disposed_reactive_owner") return true;
            throw;
        }
        const auto record = records_.find(instance.key);
        if (record != records_.end() && record->second.lifetime == instance.lifetime) return false;
        instance.owner.dispose();
        return true;
    });
    for (auto it = records_.begin(); it != records_.end();) {
        if (!keyed_.current(it->first, it->second.lifetime)) it = records_.erase(it);
        else ++it;
    }
}

Result<KeyedUpdate> ComponentCollection::reconcile(ReactiveRuntime& runtime, std::span<const ComponentMount> mounts) {
    if (reconciling_) return {{}, {{"component_setup_reentry", Severity::error, "/components",
        "Reconcile this collection after its setup operation returns.", {}}}};
    struct Guard {
        bool& active;
        explicit Guard(bool& value) : active(value) { active = true; }
        ~Guard() { active = false; }
    } guard(reconciling_);
    std::vector<Diagnostic> errors;
    if (mounts.size() > max_keyed_children) {
        error(errors, "keyed_child_limit", "/components", "Supply at most 4096 keyed components in one collection.");
        return {{}, std::move(errors)};
    }
    std::vector<KeyedChild> children;
    std::size_t slots = 0;
    std::size_t cleanups = 0;
    std::size_t effects = 0;
    std::set<std::string_view, std::less<>> seen;
    // Validate the complete candidate and its aggregate state budget before
    // constructing copied schemas or entering any runtime.
    for (std::size_t i = 0; i < mounts.size(); ++i) {
        const auto& mount = mounts[i];
        const auto path = "/components/" + std::to_string(i);
        name(errors, mount.key, path + "/key");
        if (!seen.insert(mount.key).second)
            error(errors, "duplicate_key", path + "/key", "Use each key once within the component collection.");
        if (!mount.component) {
            error(errors, "component_description", path + "/component", "Supply a component description for mounting.");
            continue;
        }
        const auto& description = *mount.component;
        name(errors, description.kind, path + "/kind");
        auto schema_errors = detail::validate_component_local_state(description.local_state, path + "/local_state");
        errors.insert(errors.end(), schema_errors.begin(), schema_errors.end());
        auto cleanup_errors = detail::validate_component_cleanup_bindings(description.cleanups, mount.cleanups, path + "/cleanups");
        errors.insert(errors.end(), cleanup_errors.begin(), cleanup_errors.end());
        auto effect_errors = detail::validate_component_effect_bindings(description.effects, mount.effects, path + "/effects");
        errors.insert(errors.end(), effect_errors.begin(), effect_errors.end());
        if (description.effects.size() <= max_component_effects) effects += description.effects.size();
        if (description.cleanups.size() <= max_component_cleanups) cleanups += description.cleanups.size();
        if (description.local_state.size() <= max_component_local_state) slots += description.local_state.size();
    }
    if (slots > max_reactive_nodes)
        error(errors, "component_state_limit", "/components", "Keep the collection's declared local-state slots within 4096; runtime values share that budget.");
    if (cleanups > max_reactive_cleanups)
        error(errors, "component_cleanup_limit", "/components", "Keep the collection's declared cleanup operations within 4096; live and pending operations share that budget.");
    if (slots + effects > max_reactive_nodes || cleanups + effects > max_reactive_cleanups)
        error(errors, "component_effect_limit", "/components", "State plus effects and cleanup plus effects must each fit the runtime's 4096-entry budgets.");
    if (!errors.empty()) return {{}, std::move(errors)};
    children.reserve(mounts.size());
    std::map<std::string, Record, std::less<>> next;
    for (std::size_t i = 0; i < mounts.size(); ++i) {
        const auto& mount = mounts[i];
        const auto& description = *mount.component;
        const auto path = "/components/" + std::to_string(i);
        children.push_back({mount.key, description.kind});
        Record record;
        for (const auto& slot : description.local_state) record.values.emplace(slot.name, placeholder(slot.initial_value));
        record.cleanups.insert(description.cleanups.begin(), description.cleanups.end());
        for (const auto& effect : description.effects) record.effects.emplace(effect.name, std::pair{effect.type, effect.phase});
        const auto* previous = keyed_.find(mount.key);
        if (previous && previous->kind == description.kind) {
            const auto& accepted = records_.at(mount.key);
            bool compatible = record.values.size() == accepted.values.size();
            for (const auto& [slot, value] : record.values) {
                const auto found = accepted.values.find(slot);
                if (found == accepted.values.end() || found->second.index() != value.index()) compatible = false;
            }
            if (!compatible)
                error(errors, "component_state_schema", path + "/local_state",
                      "Preserve the named state types for this kind, or change kind to recreate the component.");
            if (record.cleanups != accepted.cleanups)
                error(errors, "component_cleanup_schema", path + "/cleanups",
                    "Preserve named cleanup declarations or change kind to recreate the component.");
            if (record.effects != accepted.effects)
                error(errors, "component_effect_schema", path + "/effects",
                    "Preserve named effect types and phases or change kind to recreate the component.");
            if (compatible && record.cleanups == accepted.cleanups && record.effects == accepted.effects) record = accepted;
        }
        next.emplace(mount.key, std::move(record));
    }
    if (!errors.empty()) return {{}, std::move(errors)};

    Result<KeyedUpdate> update;
    // Even literal-only compatible reorders enter the ordinary structural guard.
    // Nested host batches remain open; no flush or publication is implicit.
    runtime.batch([&] {
        (void)runtime.faulted(keyed_.scope_); // Validates runtime, thread and scope lifetime.
        try { update = keyed_.reconcile(runtime, children); }
        catch (...) { prune(runtime); throw; }
        if (!update) return;
        try {
            for (const auto i : update.value->created) {
                const auto& instance = update.value->instances[i];
                auto& record = next.at(instance.key);
                for (const auto& slot : mounts[i].component->local_state)
                    record.values.at(slot.name) = create(runtime, instance.owner, slot);
            }
            // Every fresh schema is registered before host setup starts. Records
            // stay provisional until all callbacks succeed; the context carries
            // its own weak handles instead of observing the previous collection.
            for (const auto i : update.value->created) {
                const auto& instance = update.value->instances[i];
                for (const auto& cleanup : mounts[i].component->cleanups)
                    runtime.on_cleanup(instance.owner, cleanup, mounts[i].cleanups.at(cleanup));
            }
            for (const auto i : update.value->created) {
                if (!mounts[i].setup) continue;
                const auto& instance = update.value->instances[i];
                const ComponentSetup setup(instance, next.at(instance.key).values);
                try { mounts[i].setup(runtime, setup); }
                catch (const std::bad_alloc&) { throw; }
                catch (const ReactiveError&) { throw; }
                catch (const std::exception& e) {
                    throw ReactiveError({"component_setup_failure", Severity::error,
                        "/components/" + std::to_string(i) + "/setup", e.what(), {}});
                } catch (...) {
                    throw ReactiveError({"component_setup_failure", Severity::error,
                        "/components/" + std::to_string(i) + "/setup", "Component setup threw an unknown exception.", {}});
                }
                (void)runtime.faulted(instance.owner); // Setup must leave its owner live.
            }
            // Factories can capture fresh state or calculations created by setup.
            // Registration never invokes their calculation or external application.
            for (const auto i : update.value->created) {
                const auto& instance = update.value->instances[i];
                const ComponentSetup setup(instance, next.at(instance.key).values);
                for (const auto& effect : mounts[i].component->effects) {
                    const auto path = "/components/" + std::to_string(i) + "/effects/" + detail::pointer_token(effect.name);
                    ComponentEffectBinding binding;
                    try { binding = mounts[i].effects.at(effect.name)(setup); }
                    catch (const std::bad_alloc&) { throw; }
                    catch (const ReactiveError&) { throw; }
                    catch (const std::exception& e) {
                        throw ReactiveError({"component_effect_factory_failure", Severity::error, path, e.what(), {}});
                    } catch (...) {
                        throw ReactiveError({"component_effect_factory_failure", Severity::error, path, "Effect factory threw an unknown exception.", {}});
                    }
                    if (binding.calculate.index() != static_cast<std::size_t>(effect.type))
                        throw ReactiveError({"component_effect_type", Severity::error, path, "Return the declared exact calculation type.", {}});
                    if (!binding.apply || !std::visit([](const auto& calculate) { return bool(calculate); }, binding.calculate))
                        throw ReactiveError({"component_effect_binding", Severity::error, path, "Return nonempty calculation and application operations.", {}});
                    std::visit([&](auto calculate) {
                        using T = std::invoke_result_t<decltype(calculate)&>;
                        runtime.effect<T>(instance.owner, effect.name, effect.phase, std::move(calculate),
                            [apply = std::move(binding.apply)](const T& value) mutable { return apply(Property{value}); });
                    }, std::move(binding.calculate));
                }
            }
            for (const auto& instance : update.value->instances) (void)runtime.faulted(instance.owner);
        } catch (...) {
            // State registration and callbacks form one setup operation: release every owner
            // created in this call, including fully initialized siblings.
            // Repair the weak records after ordinary owner disposal.
            for (const auto i : update.value->created) update.value->instances[i].owner.dispose();
            std::erase_if(keyed_.instances_, [&](const KeyedInstance& instance) {
                return std::any_of(update.value->created.begin(), update.value->created.end(), [&](std::size_t i) {
                    return update.value->instances[i].lifetime == instance.lifetime;
                });
            });
            prune(runtime);
            throw;
        }
        for (const auto& instance : update.value->instances) next.at(instance.key).lifetime = instance.lifetime;
        records_.swap(next);
    });
    return update;
}

Diagnostic ComponentSetup::error(std::string code, std::string_view slot, std::string message) const {
    return {std::move(code), Severity::error,
        "/components/" + detail::pointer_token(instance_.key) + "/local_state/" + detail::pointer_token(slot),
        std::move(message), {}};
}

Diagnostic ComponentCollection::state_error(std::string code, std::string_view key,
                                           std::string_view slot, std::string message) {
    return {std::move(code), Severity::error,
        "/components/" + detail::pointer_token(key) + "/local_state/" + detail::pointer_token(slot),
        std::move(message), {}};
}
Result<const ComponentStateValue*> ComponentCollection::lookup(std::string_view key, std::string_view slot) const {
    const auto record = records_.find(key);
    if (record == records_.end()) return {{}, {state_error("component_instance", key, slot, "Use a mounted component key.")}};
    const auto value = record->second.values.find(slot);
    if (value == record->second.values.end()) return {{}, {state_error("component_unknown_state", key, slot, "Use a declared local-state name.")}};
    return {&value->second, {}};
}
} // namespace tessera
