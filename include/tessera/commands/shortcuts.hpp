#pragma once

#include <tessera/commands/commands.hpp>

namespace tessera {

inline constexpr std::size_t max_shortcut_scopes = 64;
inline constexpr std::size_t max_shortcut_bindings = 1024;

struct ShortcutChord {
    Key key = Key::enter;
    Modifiers modifiers; // Exact logical key/modifier match; no platform convention.
    bool operator==(const ShortcutChord&) const = default;
};
struct ShortcutBinding {
    ShortcutChord chord;
    std::string command;
    std::optional<ActionRequest> owner; // Ordinary activate owner; required for subtree scopes.
    bool repeat = false;
    bool operator==(const ShortcutBinding&) const = default;
};
struct ShortcutScope {
    std::optional<NodeHandle> root; // Absent = global; otherwise active on the focus ancestry.
    std::vector<ShortcutBinding> bindings;
    bool operator==(const ShortcutScope&) const = default;
};
struct ShortcutRequest {
    InputEvent event;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    CommandArguments arguments; // Host supplies arguments from the settled generation.
    bool consumed = false; // Host editing/IME or earlier routing already consumed this event.
};
struct ShortcutDispatch {
    bool consumed = false;
    std::optional<CommandInvocation> invocation;
};

namespace detail { struct CommandShortcutData; }

// Immutable, owned map for one registry revision/tree identity. No callbacks or stream state.
class CommandShortcuts final {
public:
    // Validate bounds, exact commands/action mappings, live owners, containment and conflicts
    // before copying. Commands must be available in the supplied snapshot's mode, even if disabled.
    static Result<CommandShortcuts> create(const CommandSnapshot&, const UiTree&,
        std::span<const ShortcutScope>);
    // Borrow coherent command/UI observations. Nearest matching scope shadows outer/global ones,
    // including when disabled, unavailable or repeat-suppressed. Errors stop event routing.
    Result<ShortcutDispatch> dispatch(const CommandSnapshot&, const SemanticInput&,
        const ShortcutRequest&) const;
private:
    explicit CommandShortcuts(std::shared_ptr<const detail::CommandShortcutData>);
    std::shared_ptr<const detail::CommandShortcutData> data_;
};

} // namespace tessera
