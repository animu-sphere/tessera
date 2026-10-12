#pragma once

#include <tessera/reactive/runtime.hpp>
#include <tessera/ui/property_metadata.hpp>

namespace tessera {
inline constexpr std::size_t max_async_text_bytes = 4096;

enum class AsyncStatus { idle, loading, ready, failed, cancelled };
struct AsyncFailure {
    std::string code;
    std::string message;
    bool retryable = false;
    bool operator==(const AsyncFailure&) const = default;
};
struct AsyncReadyValue {
    Property value;
    std::string request;
    std::uint64_t attempt = 0;
    std::uint64_t revision = 0; // Accepted completion count, including cache hits.
    bool operator==(const AsyncReadyValue&) const = default;
};
struct AsyncObservation {
    AsyncStatus status = AsyncStatus::idle;
    std::string request;
    std::uint64_t attempt = 0;
    std::optional<double> progress;
    std::optional<AsyncFailure> failure;
    std::optional<AsyncReadyValue> value; // Last complete value; never a partial payload.
    bool stale() const noexcept { return value.has_value() && status != AsyncStatus::ready; }
    bool operator==(const AsyncObservation&) const = default;
};

// An opaque, call-independent delivery address for one observation lifetime.
// Holding a token neither owns the reactive owner nor keeps its values alive.
class AsyncAttempt final {
public:
    AsyncAttempt() = default;
    const std::string& request() const noexcept { return request_; }
    std::uint64_t sequence() const noexcept { return sequence_; }
private:
    std::weak_ptr<const char> identity_;
    std::string request_;
    std::uint64_t sequence_ = 0;
    friend class AsyncValue;
};
struct AsyncStart {
    AsyncAttempt attempt;
    // The host cancels/detaches this work after return, outside traversal.
    std::optional<AsyncAttempt> detached;
};

// Typed local view observation. The host owns execution and calls transitions
// on the runtime's UI thread at update points, optionally inside update batches.
// Copies alias one weak reactive source; disposal belongs to the supplied owner.
class AsyncValue final {
public:
    AsyncValue(ReactiveRuntime&, const ReactiveOwner&, std::string name, PropertyType);
    AsyncObservation read() const { return state_.read(); } // Tracked in calculations.
    AsyncObservation snapshot(const ReactiveRuntime& runtime) const { return runtime.snapshot(state_); }
    Result<AsyncStart> begin(std::string request) const;
    // Explicit synchronous cache declaration, permitted only from idle.
    Result<AsyncAttempt> ready_cached(std::string request, Property value) const;
    Result<bool> progress(const AsyncAttempt&, double) const;
    Result<bool> complete(const AsyncAttempt&, Property) const;
    Result<bool> fail(const AsyncAttempt&, AsyncFailure) const;
    Result<bool> cancel(const AsyncAttempt&) const;
private:
    AsyncAttempt token(const AsyncObservation&) const;
    Result<AsyncObservation> current(const AsyncAttempt&) const;
    Result<AsyncObservation> live() const;
    std::optional<Diagnostic> payload(const Property&) const;
    Result<bool> write(AsyncObservation) const;
    Diagnostic error(std::string code, std::string field, std::string message) const;
    Diagnostic located(const ReactiveError&) const;
    std::string path_;
    PropertyType type_;
    std::shared_ptr<const char> identity_;
    Signal<AsyncObservation> state_;
};
} // namespace tessera
