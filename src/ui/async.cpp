#include <tessera/ui/async.hpp>
#include "json_detail.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {
namespace {
bool name(std::string_view text) {
    return !text.empty() && text.size() <= 256 && detail::valid_utf8(text) &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
template<class T> Result<T> rejected(Diagnostic diagnostic) {
    return {{}, {std::move(diagnostic)}};
}
}
AsyncValue::AsyncValue(ReactiveRuntime& runtime, const ReactiveOwner& owner, std::string value_name, PropertyType type)
    : path_("/async/" + detail::pointer_token(value_name)), type_(type), identity_(std::make_shared<const char>('\0')) {
    if (!name(value_name)) throw ReactiveError(error("async_name", "", "Supply a name of 1 to 256 UTF-8 bytes without control characters."));
    if (type != PropertyType::boolean && type != PropertyType::number && type != PropertyType::string && type != PropertyType::reference)
        throw ReactiveError(error("async_type", "/type", "Use boolean, number, string or reference."));
    state_ = runtime.signal(owner, std::move(value_name), AsyncObservation{});
}
Diagnostic AsyncValue::error(std::string code, std::string field, std::string message) const {
    return {std::move(code), Severity::error, path_ + field, std::move(message), {}};
}
Diagnostic AsyncValue::located(const ReactiveError& failure) const {
    auto diagnostic = failure.diagnostic();
    diagnostic.message += " (" + diagnostic.path + ")";
    diagnostic.path = path_;
    return diagnostic;
}
Result<AsyncObservation> AsyncValue::live() const {
    try { return {state_.read(), {}}; }
    catch (const ReactiveError& failure) { return rejected<AsyncObservation>(located(failure)); }
}
AsyncAttempt AsyncValue::token(const AsyncObservation& state) const {
    AsyncAttempt result;
    result.identity_ = identity_;
    result.request_ = state.request;
    result.sequence_ = state.attempt;
    return result;
}
Result<AsyncObservation> AsyncValue::current(const AsyncAttempt& attempt) const {
    auto state = live();
    if (!state) return state;
    if (attempt.identity_.lock() != identity_ || attempt.sequence_ != state.value->attempt ||
        attempt.request_ != state.value->request)
        return rejected<AsyncObservation>(error("async_stale_attempt", "/attempt", "Drop this delivery; it does not address the current observation and attempt."));
    if (state.value->status != AsyncStatus::loading)
        return rejected<AsyncObservation>(error("async_transition", "/status", "Deliver progress or a terminal result only to a loading attempt."));
    return state;
}
std::optional<Diagnostic> AsyncValue::payload(const Property& value) const {
    if (property_type(value) != type_) return error("async_payload_type", "/value", "Supply the observation's exact declared property type; values are not coerced.");
    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        return error("invalid_number", "/value", "Supply a finite number.");
    if (const auto* text = std::get_if<std::string>(&value); text && (text->size() > max_async_text_bytes || !detail::valid_utf8(*text)))
        return error("async_text", "/value", "Supply at most 4096 bytes of valid UTF-8 text.");
    if (const auto* ref = std::get_if<NodeReference>(&value); ref && !name(ref->id))
        return error("async_name", "/value/ref", "Supply an author ID of 1 to 256 UTF-8 bytes without control characters.");
    return {};
}
Result<bool> AsyncValue::write(AsyncObservation state) const {
    try { return {state_.write(std::move(state)), {}}; }
    catch (const ReactiveError& failure) { return rejected<bool>(located(failure)); }
}
Result<AsyncStart> AsyncValue::begin(std::string request) const {
    auto state = live();
    if (!state) return {{}, std::move(state.diagnostics)};
    if (!name(request)) return rejected<AsyncStart>(error("async_name", "/request", "Supply a request ID of 1 to 256 UTF-8 bytes without control characters."));
    if (state.value->attempt == std::numeric_limits<std::uint64_t>::max())
        return rejected<AsyncStart>(error("async_attempt_limit", "/attempt", "Create a new observation lifetime before the attempt sequence is exhausted."));
    AsyncStart start;
    if (state.value->status == AsyncStatus::loading) start.detached = token(*state.value);
    state.value->status = AsyncStatus::loading;
    state.value->request = std::move(request);
    ++state.value->attempt;
    state.value->progress.reset();
    state.value->failure.reset();
    start.attempt = token(*state.value);
    const auto changed = write(std::move(*state.value));
    if (!changed) return {{}, changed.diagnostics};
    return {std::move(start), {}};
}
Result<AsyncAttempt> AsyncValue::ready_cached(std::string request, Property value) const {
    auto state = live();
    if (!state) return {{}, std::move(state.diagnostics)};
    if (state.value->status != AsyncStatus::idle)
        return rejected<AsyncAttempt>(error("async_transition", "/status", "Declare an immediate cache hit only from idle; use a new attempt for replacement."));
    if (!name(request)) return rejected<AsyncAttempt>(error("async_name", "/request", "Supply a request ID of 1 to 256 UTF-8 bytes without control characters."));
    if (const auto diagnostic = payload(value)) return rejected<AsyncAttempt>(*diagnostic);
    state.value->status = AsyncStatus::ready;
    state.value->request = std::move(request);
    state.value->attempt = 1;
    state.value->value = AsyncReadyValue{std::move(value), state.value->request, 1, 1};
    const auto attempt = token(*state.value);
    const auto changed = write(std::move(*state.value));
    if (!changed) return {{}, changed.diagnostics};
    return {attempt, {}};
}
Result<bool> AsyncValue::progress(const AsyncAttempt& attempt, double value) const {
    auto state = current(attempt);
    if (!state) return {{}, std::move(state.diagnostics)};
    if (!std::isfinite(value) || value < 0 || value > 1 || (state.value->progress && value < *state.value->progress))
        return rejected<bool>(error("async_progress", "/progress", "Supply finite progress in [0, 1] that does not decrease within the attempt."));
    state.value->progress = value;
    return write(std::move(*state.value));
}
Result<bool> AsyncValue::complete(const AsyncAttempt& attempt, Property value) const {
    auto state = current(attempt);
    if (!state) return {{}, std::move(state.diagnostics)};
    if (const auto diagnostic = payload(value)) return rejected<bool>(*diagnostic);
    const auto revision = state.value->value ? state.value->value->revision : 0;
    if (revision == std::numeric_limits<std::uint64_t>::max())
        return rejected<bool>(error("async_revision_limit", "/value/revision", "Create a new observation lifetime before the accepted-value revision is exhausted."));
    state.value->status = AsyncStatus::ready;
    state.value->progress.reset();
    state.value->value = AsyncReadyValue{std::move(value), state.value->request, state.value->attempt, revision + 1};
    return write(std::move(*state.value));
}
Result<bool> AsyncValue::fail(const AsyncAttempt& attempt, AsyncFailure failure) const {
    auto state = current(attempt);
    if (!state) return {{}, std::move(state.diagnostics)};
    if (!name(failure.code)) return rejected<bool>(error("async_name", "/failure/code", "Supply an error code of 1 to 256 UTF-8 bytes without control characters."));
    if (failure.message.empty() || failure.message.size() > max_async_text_bytes || !detail::valid_utf8(failure.message))
        return rejected<bool>(error("async_text", "/failure/message", "Supply a nonempty error message of at most 4096 UTF-8 bytes."));
    state.value->status = AsyncStatus::failed;
    state.value->progress.reset();
    state.value->failure = std::move(failure);
    return write(std::move(*state.value));
}
Result<bool> AsyncValue::cancel(const AsyncAttempt& attempt) const {
    auto state = current(attempt);
    if (!state) return {{}, std::move(state.diagnostics)};
    state.value->status = AsyncStatus::cancelled;
    state.value->progress.reset();
    return write(std::move(*state.value));
}
} // namespace tessera
