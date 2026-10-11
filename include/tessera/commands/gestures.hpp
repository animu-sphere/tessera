#pragma once

#include <tessera/commands/transactions.hpp>

namespace tessera {

struct EditGesture {
    std::uint64_t id = 0; // Host-issued, increasing and never reused in this adapter.
    std::optional<PointerId> pointer; // Absent for keyboard/gamepad/semantic edits.
    PointerButton button = PointerButton::primary;
    TransactionMetadata metadata;
    bool operator==(const EditGesture&) const = default;
};

struct GestureInputResult {
    bool consumed = false;
    std::optional<CommandOutcome> closure;
};

// Call after ordinary input traversal, before publishing the next generation.
// Borrows a session that outlives this adapter; use that session for non-editing
// commands, but do not directly open/close its edit while a gesture is active.
// No host callbacks/pointers are retained and destruction invokes no callbacks.
class CommandGestures final {
public:
    explicit CommandGestures(CommandTransactions&);
    CommandGestures(const CommandGestures&) = delete;
    CommandGestures& operator=(const CommandGestures&) = delete;
    CommandGestures(CommandGestures&&) = delete;
    CommandGestures& operator=(CommandGestures&&) = delete;

    // First accepted update begins and executes; later updates join the same edit.
    Result<CommandExecution> edit(const EditGesture&, const CommandSnapshot&,
        const CommandInvocation&, TransactionHost&, const SemanticInput* current_ui = nullptr);
    Result<CommandOutcome> complete(std::uint64_t id, const CommandSnapshot&,
        TransactionHost&, const SemanticInput* current_ui = nullptr);
    Result<CommandOutcome> cancel(std::uint64_t id, TransactionCancel, TransactionHost&);
    // Recheck at every update point, including updates without input or edits.
    Result<bool> refresh(const CommandSnapshot&, TransactionHost&, const SemanticInput* current_ui = nullptr);
    // Matching pointer release commits; matching PointerCancel rolls back for
    // capture loss; Escape/Cancel rolls back for the user. Other input is ignored.
    // consumed=true suppresses shortcut/navigation/activation for that event.
    Result<GestureInputResult> handle_input(const CommandSnapshot&, const InputEvent&,
        TransactionHost&, const SemanticInput* current_ui = nullptr);
    std::optional<std::uint64_t> active() const noexcept;
private:
    std::vector<Diagnostic> entry() const;
    std::vector<Diagnostic> identity(std::uint64_t) const;
    void discard_closed();
    CommandTransactions& transactions_;
    const std::thread::id thread_;
    std::uint64_t last_id_ = 0;
    std::optional<EditGesture> gesture_;
    std::optional<TransactionToken> token_;
    std::optional<std::chrono::microseconds> timestamp_;
    bool entering_ = false;
};

} // namespace tessera
