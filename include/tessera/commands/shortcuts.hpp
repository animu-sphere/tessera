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
// Logical positions, independent of device labels. Hosts normalize physical buttons,
// trigger thresholds, press edges and repeat timing before routing.
enum class GamepadButton : std::uint8_t {
    south, east, west, north, dpad_up, dpad_down, dpad_left, dpad_right,
    left_shoulder, right_shoulder, left_trigger, right_trigger,
    left_stick, right_stick, select, start,
};
constexpr std::uint32_t gamepad_button_bit(GamepadButton button) noexcept {
    const auto index = static_cast<unsigned>(button);
    return index <= static_cast<unsigned>(GamepadButton::start) ? (1u << index) : 0;
}
struct GamepadShortcutChord {
    GamepadButton button = GamepadButton::south; // Newly pressed or repeated button.
    std::uint32_t held = 0; // Exact other held buttons, excluding button; one device/sample.
    bool operator==(const GamepadShortcutChord&) const = default;
};
struct GamepadShortcutBinding {
    GamepadShortcutChord chord;
    std::string command;
    std::optional<ActionRequest> owner;
    bool repeat = false;
    bool operator==(const GamepadShortcutBinding&) const = default;
};
struct ShortcutScope {
    std::optional<NodeHandle> root; // Absent = global; otherwise active on the focus ancestry.
    std::vector<ShortcutBinding> bindings;
    std::vector<GamepadShortcutBinding> gamepad_bindings;
    bool operator==(const ShortcutScope&) const = default;
};
struct ShortcutRequest {
    InputEvent event;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    CommandArguments arguments; // Host supplies arguments from the settled generation.
    bool consumed = false; // Host editing/IME or earlier routing already consumed this event.
};
// Separate in-process request, not an InputEvent or a serialized replay event.
struct GamepadShortcutRequest {
    GamepadShortcutChord chord;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    CommandArguments arguments;
    bool consumed = false;
    bool repeat = false;
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
    Result<ShortcutDispatch> dispatch(const CommandSnapshot&, const SemanticInput&,
        const GamepadShortcutRequest&) const;
private:
    explicit CommandShortcuts(std::shared_ptr<const detail::CommandShortcutData>);
    std::shared_ptr<const detail::CommandShortcutData> data_;
};

} // namespace tessera
