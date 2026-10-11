#include <tessera/commands/gestures.hpp>
#include <utility>

namespace tessera {
namespace {
std::vector<Diagnostic> error(std::string code, std::string path, std::string message) {
    return {{std::move(code), Severity::error, std::move(path), std::move(message), {}}};
}
struct Guard {
    bool& entering;
    explicit Guard(bool& value) : entering(value) { entering = true; }
    ~Guard() { entering = false; }
};
} // namespace

CommandGestures::CommandGestures(CommandTransactions& transactions)
    : transactions_(transactions), thread_(std::this_thread::get_id()) {}
std::optional<std::uint64_t> CommandGestures::active() const noexcept {
    return token_ && token_->active() ? std::optional{gesture_->id} : std::nullopt;
}
void CommandGestures::discard_closed() {
    if (token_ && !token_->active()) { token_.reset(); gesture_.reset(); }
}
std::vector<Diagnostic> CommandGestures::entry() const {
    if (std::this_thread::get_id() != thread_)
        return error("wrong_thread", "/gesture", "Use the gesture adapter's creating UI thread.");
    if (entering_)
        return error("reentrant_gesture", "/gesture", "Queue gesture operations until the host boundary returns.");
    return {};
}
std::vector<Diagnostic> CommandGestures::identity(std::uint64_t id) const {
    if (!active() || *active() != id)
        return error("stale_gesture", "/gesture/id", "Use the active gesture identity; closed gestures cannot be reopened.");
    return {};
}
Result<CommandExecution> CommandGestures::edit(const EditGesture& gesture, const CommandSnapshot& snapshot,
    const CommandInvocation& invocation, TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    discard_closed();
    if (gesture.id == 0)
        return {std::nullopt, error("invalid_gesture", "/gesture/id", "Supply a nonzero host gesture identity.")};
    if (static_cast<unsigned>(gesture.button) > static_cast<unsigned>(PointerButton::middle))
        return {std::nullopt, error("unknown_value", "/gesture/button", "Use a declared pointer button.")};
    if (active()) {
        if (gesture.id != gesture_->id)
            return {std::nullopt, error("nested_gesture", "/gesture/id", "Finish the active gesture before starting another.")};
        if (gesture != *gesture_)
            return {std::nullopt, error("gesture_mismatch", "/gesture", "Keep the gesture's pointer, button and opening metadata unchanged.")};
    } else if (gesture.id <= last_id_) {
        return {std::nullopt, error("stale_gesture", "/gesture/id", "Use a new increasing gesture identity.")};
    }
    Guard guard(entering_);
    if (!active()) {
        // Begin validates bounded metadata before any copy. Prepare adapter
        // storage only after it succeeds; retain the token if copying throws.
        last_id_ = gesture.id;
        auto started = transactions_.begin(snapshot, invocation, gesture.metadata, host, ui);
        if (!started) return {std::nullopt, std::move(started.diagnostics)};
        token_ = *started.value;
        // Move a bounded copy after begin while keeping recovery identity valid.
        gesture_.emplace();
        gesture_->id = gesture.id;
        gesture_->pointer = gesture.pointer;
        gesture_->button = gesture.button;
        gesture_->metadata = gesture.metadata;
    }
    auto result = transactions_.execute_edit(snapshot, invocation, *token_, host, ui);
    discard_closed();
    return result;
}
Result<CommandOutcome> CommandGestures::complete(std::uint64_t id, const CommandSnapshot& snapshot,
    TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (errors.empty()) errors = identity(id);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    Guard guard(entering_);
    auto result = transactions_.commit(snapshot, *token_, host, ui);
    discard_closed();
    return result;
}
Result<CommandOutcome> CommandGestures::cancel(std::uint64_t id, TransactionCancel reason, TransactionHost& host) {
    auto errors = entry();
    if (errors.empty()) errors = identity(id);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    Guard guard(entering_);
    auto result = transactions_.rollback(*token_, reason, host);
    discard_closed();
    return result;
}
Result<bool> CommandGestures::refresh(const CommandSnapshot& snapshot, TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    discard_closed();
    if (!active()) return {false, {}};
    Guard guard(entering_);
    auto result = transactions_.recheck(snapshot, *token_, host, ui);
    discard_closed();
    return result;
}
Result<GestureInputResult> CommandGestures::handle_input(const CommandSnapshot& snapshot, const InputEvent& event,
    TransactionHost& host, const SemanticInput* ui) {
    auto errors = entry();
    if (errors.empty()) errors = validate(event);
    if (errors.empty() && timestamp_ && event.timestamp < *timestamp_)
        errors = error("event_order", "/timestamp", "Deliver non-decreasing gesture input timestamps.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto checked = refresh(snapshot, host, ui);
    if (!checked) return {std::nullopt, checked.diagnostics};
    timestamp_ = event.timestamp;
    if (!*checked.value) return {GestureInputResult{}, {}};
    const auto key = std::get_if<KeyDown>(&event.data);
    const auto up = std::get_if<PointerUp>(&event.data);
    const auto abandoned = std::get_if<PointerCancel>(&event.data);
    const bool user_cancel = std::holds_alternative<Cancel>(event.data) || (key && key->key == Key::escape);
    const bool pointer_cancel = abandoned && gesture_->pointer && abandoned->pointer == *gesture_->pointer;
    const bool released = up && gesture_->pointer && up->pointer == *gesture_->pointer && up->button == gesture_->button;
    if (!user_cancel && !pointer_cancel && !released) return {GestureInputResult{}, {}};
    auto closed = released ? complete(gesture_->id, snapshot, host, ui) :
        cancel(gesture_->id, pointer_cancel ? TransactionCancel::capture_lost : TransactionCancel::user, host);
    if (!closed) return {std::nullopt, std::move(closed.diagnostics)};
    return {GestureInputResult{true, std::move(closed.value)}, {}};
}
} // namespace tessera
