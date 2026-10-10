#pragma once

#include <tessera/ui/document.hpp>
#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tessera {
inline constexpr std::size_t max_reactive_nodes = 4096;
inline constexpr std::size_t max_reactive_owners = 4096;
inline constexpr std::size_t max_reactive_depth = 32;
inline constexpr std::size_t max_reactive_cleanups = 4096;
inline constexpr std::size_t max_reactive_effect_rounds = 32;

// Host delivery points for values captured by a successful publication.
enum class EffectPhase {
    prepare, // After publication, before the host submits that generation.
    notify,  // After publication; implies no GPU completion.
};

class ReactiveError final : public std::runtime_error {
public:
    explicit ReactiveError(Diagnostic);
    const Diagnostic& diagnostic() const noexcept { return diagnostic_; }
private:
    Diagnostic diagnostic_;
};
namespace detail {
struct ReactiveState;
struct ReactiveRef {
    std::weak_ptr<ReactiveState> state;
    std::uint64_t id = 0;
};
using ReactiveEqual = std::function<bool(const std::any&, const std::any&)>;
using ReactiveApply = std::function<std::function<void()>(const std::any&)>;
std::any reactive_read(const ReactiveRef&);
std::uint64_t reactive_revision(const ReactiveRef&);
bool reactive_write(const ReactiveRef&, std::any);
void reactive_dispose(const ReactiveRef&);
template<class T, class Equal>
ReactiveEqual reactive_equal(Equal equal) {
    return [equal = std::move(equal)](const std::any& a, const std::any& b) mutable {
        return std::invoke(equal, std::any_cast<const T&>(a), std::any_cast<const T&>(b));
    };
}
}

// References neither prolong the runtime nor own the referenced scope/value.
class ReactiveOwner final {
public:
    ReactiveOwner() = default;
    void dispose() const { detail::reactive_dispose(ref_); }
private:
    explicit ReactiveOwner(detail::ReactiveRef ref) : ref_(std::move(ref)) {}
    detail::ReactiveRef ref_;
    friend class ReactiveRuntime;
};
template<class T>
class Signal final {
public:
    Signal() = default;
    T read() const { return std::any_cast<T>(detail::reactive_read(ref_)); }
    bool write(T value) const { return detail::reactive_write(ref_, std::any(std::move(value))); }
    std::uint64_t revision() const { return detail::reactive_revision(ref_); }
private:
    explicit Signal(detail::ReactiveRef ref) : ref_(std::move(ref)) {}
    detail::ReactiveRef ref_;
    friend class ReactiveRuntime;
};
template<class T>
class Computed final {
public:
    Computed() = default;
    T read() const { return std::any_cast<T>(detail::reactive_read(ref_)); }
    // Refreshes the value without adding a dependency.
    std::uint64_t revision() const { return detail::reactive_revision(ref_); }
private:
    explicit Computed(detail::ReactiveRef ref) : ref_(std::move(ref)) {}
    detail::ReactiveRef ref_;
    friend class ReactiveRuntime;
};

// One host UI thread; explicit graph flush, publication, and external delivery.
class ReactiveRuntime final {
public:
    ReactiveRuntime();
    ~ReactiveRuntime();
    ReactiveRuntime(const ReactiveRuntime&) = delete;
    ReactiveRuntime& operator=(const ReactiveRuntime&) = delete;
    ReactiveOwner root() const;
    ReactiveOwner owner(const ReactiveOwner& parent, std::string name);
    // Owner disposal queues this callback; it never invokes external operations.
    // Cleanup callbacks must not enter any reactive runtime.
    void on_cleanup(const ReactiveOwner&, std::string name, std::function<void()>);
    // Host boundary, outside batches/calculations. Consumes each queued callback
    // once, continues after callable failures, and returns located diagnostics.
    // Runtime destruction releases captures without invoking pending callbacks.
    std::vector<Diagnostic> deliver_cleanups();
    template<class T, class Equal = std::equal_to<T>>
    Signal<T> signal(const ReactiveOwner& owner, std::string name, T value, Equal equal = {}) {
        static_assert(std::is_copy_constructible_v<T>, "Reactive values must be copyable owned values.");
        return Signal<T>(add(owner, std::move(name), std::any(std::move(value)), {},
                             detail::reactive_equal<T>(std::move(equal))));
    }
    template<class T, class Calculate, class Equal = std::equal_to<T>>
    Computed<T> computed(const ReactiveOwner& owner, std::string name, Calculate calculate, Equal equal = {}) {
        static_assert(std::is_copy_constructible_v<T>, "Reactive values must be copyable owned values.");
        return Computed<T>(add(owner, std::move(name), {},
            [calculate = std::move(calculate)]() mutable -> std::any { return T(std::invoke(calculate)); },
            detail::reactive_equal<T>(std::move(equal))));
    }
    // The pure calculation is tracked like a computed value. Each published
    // change applies on its phase after running the prior returned cleanup once;
    // owner disposal queues the latest cleanup for deliver_cleanups.
    template<class T, class Calculate, class Apply, class Equal = std::equal_to<T>>
    void effect(const ReactiveOwner& owner, std::string name, EffectPhase phase,
                Calculate calculate, Apply apply, Equal equal = {}) {
        static_assert(std::is_copy_constructible_v<T>, "Reactive values must be copyable owned values.");
        add_effect(owner, std::move(name), phase,
            [calculate = std::move(calculate)]() mutable -> std::any { return T(std::invoke(calculate)); },
            detail::reactive_equal<T>(std::move(equal)),
            [apply = std::move(apply)](const std::any& value) mutable -> std::function<void()> {
                const auto& typed = std::any_cast<const T&>(value);
                if constexpr (std::is_void_v<std::invoke_result_t<Apply&, const T&>>) {
                    std::invoke(apply, typed);
                    return {};
                } else {
                    return std::function<void()>(std::invoke(apply, typed));
                }
            });
    }
    void begin_batch();
    void end_batch(); // Closes only; the host calls flush after the outer batch.
    template<class Update>
    void batch(Update update) {
        begin_batch();
        try { std::invoke(update); }
        catch (...) { end_batch(); throw; }
        end_batch();
    }
    // Demand reads are allowed within batches; flush is not. First failure stops
    // draining; accepted caches survive and failed/pending calculations can retry.
    void flush();
    // Accepts the settled graph as the published candidate after the host has
    // built its presentation. Captures changed effect values and returns how
    // many deliveries were queued; failure queues nothing.
    std::size_t publish();
    // Host boundary, outside batches/calculations. Effects may read and write
    // sources; writes wait for a later flush/publication.
    std::vector<Diagnostic> deliver_effects(EffectPhase);
private:
    detail::ReactiveRef add(const ReactiveOwner&, std::string, std::any,
                            std::function<std::any()>, detail::ReactiveEqual);
    void add_effect(const ReactiveOwner&, std::string, EffectPhase, std::function<std::any()>,
                    detail::ReactiveEqual, detail::ReactiveApply);
    std::shared_ptr<detail::ReactiveState> state_;
};
} // namespace tessera
