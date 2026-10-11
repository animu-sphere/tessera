#include <tessera/commands/shortcuts.hpp>
#include "../detail/checks.hpp"
#include "../detail/targeting.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace tessera {
namespace detail {
struct CommandShortcutData {
    std::uint64_t registry_revision, tree;
    std::vector<std::uint32_t> parents;
    std::vector<ShortcutScope> scopes;
};
} // namespace detail
namespace {
constexpr auto no_node = std::numeric_limits<std::uint32_t>::max();
bool contains(const std::vector<std::uint32_t>& parents, NodeHandle root, NodeHandle node) {
    if (root.tree != node.tree) return false;
    for (auto index = node.index; index != no_node; index = parents[index])
        if (index == root.index) return true;
    return false;
}
unsigned chord_id(const ShortcutChord& chord) {
    const auto& m = chord.modifiers;
    return static_cast<unsigned>(chord.key) * 16 + m.shift + 2 * m.control + 4 * m.alt + 8 * m.super;
}
unsigned chord_id(const GamepadShortcutChord& chord) {
    return static_cast<unsigned>(chord.button) * 65536 + chord.held;
}
void validate_chord(detail::Checker& check, const ShortcutChord& chord, const std::string& path) {
    check.enumeration(chord.key, Key::f12, path + "/key");
}
void validate_chord(detail::Checker& check, const GamepadShortcutChord& chord, const std::string& path) {
    check.enumeration(chord.button, GamepadButton::start, path + "/button");
    if (chord.held & ~std::uint32_t{0xffff})
        check.error("unknown_value", path + "/held", "Use only declared logical gamepad button bits.");
    if (chord.held & gamepad_button_bit(chord.button))
        check.error("invalid_chord", path + "/held", "Exclude the pressed button from the other held buttons.");
}
} // namespace

CommandShortcuts::CommandShortcuts(std::shared_ptr<const detail::CommandShortcutData> data)
    : data_(std::move(data)) {}

Result<CommandShortcuts> CommandShortcuts::create(const CommandSnapshot& commands, const UiTree& tree,
    std::span<const ShortcutScope> scopes) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (scopes.size() > max_shortcut_scopes) {
        check.error("out_of_range", "/scopes", "Declare at most 64 shortcut scopes.");
        return {std::nullopt, std::move(errors)};
    }
    std::size_t count = 0;
    for (const auto& scope : scopes) {
        for (const auto size : {scope.bindings.size(), scope.gamepad_bindings.size()}) {
            if (size > max_shortcut_bindings - count) {
                check.error("out_of_range", "/scopes", "Declare at most 1024 keyboard/gamepad shortcut bindings in total.");
                return {std::nullopt, std::move(errors)};
            }
            count += size;
        }
    }
    std::vector<std::uint32_t> parents(tree.size(), no_node);
    const auto visit = [&](auto&& self, NodeHandle parent) -> void {
        for (const auto child : tree.children(parent)) {
            parents[child.index] = parent.index;
            self(self, child);
        }
    };
    visit(visit, tree.root());
    std::set<std::uint32_t> roots;
    for (std::size_t i = 0; i < scopes.size(); ++i) {
        const auto& scope = scopes[i];
        const auto path = "/scopes/" + std::to_string(i);
        const bool live_root = !scope.root || tree.get(*scope.root);
        if (!live_root) check.error("stale_target", path + "/root", "Resolve the scope root in this tree.");
        if (live_root && !roots.insert(scope.root ? scope.root->index : no_node).second)
            check.error("duplicate_scope", path + "/root", "Declare each scope root, including global, once.");
        const auto validate_bindings = [&](const auto& bindings, const std::string& prefix) {
            std::map<unsigned, std::size_t> chords;
            for (std::size_t j = 0; j < bindings.size(); ++j) {
                const auto& binding = bindings[j];
                const auto bp = prefix + "/" + std::to_string(j);
                validate_chord(check, binding.chord, bp + "/chord");
                const auto [previous, unique] = chords.emplace(chord_id(binding.chord), j);
                if (!unique) check.error("shortcut_conflict", bp + "/chord",
                    "This chord conflicts with " + prefix + "/" + std::to_string(previous->second) + "/chord; keep one binding per scope.");
                if (binding.command.size() > 128) {
                    check.error("out_of_range", bp + "/command", "Use a command ID within 128 bytes.");
                    continue;
                }
                const auto info = commands.lookup(binding.command);
                if (!info) check.error("unknown_command", bp + "/command", "Use an exact command ID available in this mode.");
                if (scope.root && !binding.owner)
                    check.error("missing_owner", bp + "/owner", "Subtree shortcuts require an ordinary activation owner inside the scope.");
                if (!binding.owner) continue;
                const auto& owner = *binding.owner;
                const auto* node = tree.get(owner.target);
                if (!node) {
                    check.error("stale_target", bp + "/owner/target", "Resolve the activation owner in this tree.");
                    continue;
                }
                if (scope.root && live_root && !contains(parents, *scope.root, owner.target))
                    check.error("outside_scope", bp + "/owner/target", "Place the activation owner inside the scope subtree.");
                const auto action = node->events.find("activate");
                if (owner.binding != "activate" || action == node->events.end() || action->second != owner.action) {
                    check.error("stale_binding", bp + "/owner/action", "Supply this node's exact activate binding.");
                    continue;
                }
                const auto mapped = commands.lookup_action(owner.action);
                if (!mapped || mapped.value->descriptor.id != binding.command)
                    check.error("unmapped_action", bp + "/owner/action", "Map this activation action to the shortcut's exact command ID.");
            }
        };
        validate_bindings(scope.bindings, path + "/bindings");
        validate_bindings(scope.gamepad_bindings, path + "/gamepad_bindings");
    }
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto data = std::make_shared<detail::CommandShortcutData>(detail::CommandShortcutData{
        commands.registry_revision(), tree.root().tree, std::move(parents), {scopes.begin(), scopes.end()}});
    return {CommandShortcuts{std::move(data)}, {}};
}

namespace {
template<class Request, class Binding, class Chord>
Result<ShortcutDispatch> route(const detail::CommandShortcutData& data, const CommandSnapshot& commands,
    const SemanticInput& ui, const Request& request, const Chord* chord, bool repeat, CommandSource source,
    std::vector<Binding> ShortcutScope::* bindings, std::vector<Diagnostic> errors) {
    auto snapshot_errors = validate(ui);
    errors.insert(errors.end(), snapshot_errors.begin(), snapshot_errors.end());
    detail::Checker check(errors);
    if (commands.registry_revision() != data.registry_revision)
        check.error("stale_registry", "/registry_revision", "Rebuild shortcuts after registry replacement.");
    if (request.generation != commands.generation())
        check.error("stale_generation", "/generation", "Route against the current settled command/UI generation.");
    if (!request.sequence) check.error("invalid_sequence", "/sequence", "Supply a nonzero host request sequence.");
    if (ui.tree && ui.tree->root().tree != data.tree)
        check.error("stale_target", "/scopes", "Rebuild shortcut scopes and owners after tree replacement.");
    if (request.arguments.size() > max_command_parameters)
        check.error("out_of_range", "/arguments", "Supply at most 64 command arguments.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (request.consumed) return {ShortcutDispatch{true, {}}, {}};
    if (!chord) return {ShortcutDispatch{}, {}};
    const HitTestInput hit{ui.tree, ui.styles, ui.layout};
    const detail::Targeting targeting(hit);
    std::optional<NodeHandle> focused = ui.focused;
    if (focused) {
        const auto box = targeting.box(*focused);
        if (box == no_layout_parent || !targeting.focusable(box)) focused.reset();
    }
    const Binding* selected = nullptr;
    auto best = std::numeric_limits<std::size_t>::max();
    for (const auto& scope : data.scopes) {
        auto distance = std::numeric_limits<std::size_t>::max() - 1;
        if (scope.root) {
            if (!focused) continue;
            distance = 0;
            auto index = focused->index;
            while (index != no_node && index != scope.root->index) {
                index = data.parents[index];
                ++distance;
            }
            if (index == no_node) continue;
        }
        if (distance >= best) continue;
        const auto& candidates = scope.*bindings;
        const auto binding = std::find_if(candidates.begin(), candidates.end(),
            [&](const auto& candidate) { return candidate.chord == *chord; });
        if (binding != candidates.end()) { selected = &*binding; best = distance; }
    }
    if (!selected) return {ShortcutDispatch{}, {}};
    if (repeat && !selected->repeat) return {ShortcutDispatch{true, {}}, {}};
    auto invocation = selected->owner ? commands.resolve_action(ui, *selected->owner, request.generation,
        request.arguments, source, request.sequence) :
        commands.resolve({request.generation, selected->command, request.arguments, source, request.sequence});
    if (!invocation) return {std::nullopt, std::move(invocation.diagnostics)};
    return {ShortcutDispatch{true, std::move(*invocation.value)}, {}};
}
} // namespace

Result<ShortcutDispatch> CommandShortcuts::dispatch(const CommandSnapshot& commands, const SemanticInput& ui,
    const ShortcutRequest& request) const {
    const auto* key = std::get_if<KeyDown>(&request.event.data);
    const ShortcutChord chord{key ? key->key : Key::enter, key ? key->modifiers : Modifiers{}};
    return route(*data_, commands, ui, request, key ? &chord : nullptr, key && key->repeat,
        CommandSource::keyboard, &ShortcutScope::bindings, validate(request.event));
}

Result<ShortcutDispatch> CommandShortcuts::dispatch(const CommandSnapshot& commands, const SemanticInput& ui,
    const GamepadShortcutRequest& request) const {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    validate_chord(check, request.chord, "/chord");
    return route(*data_, commands, ui, request, &request.chord, request.repeat,
        CommandSource::gamepad, &ShortcutScope::gamepad_bindings, std::move(errors));
}

} // namespace tessera
