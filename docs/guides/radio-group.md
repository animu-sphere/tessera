# RadioGroup

`authoring::RadioGroup` composes one controlled single choice from ordinary Boxes and Text. The [Inspector](inspector-hierarchy.md#visibility-choice) uses it for each item's Visibility.

## Compose a controlled choice

```cpp
namespace ui = tessera::authoring;
auto visibility = ui::RadioGroup("Visibility", item.visibility, {
    {"inherited", "Inherited", "visibility.inherited"},
    {"visible", "Visible", "visibility.visible"},
    {"hidden", "Hidden", "visibility.hidden"}
}, {.node = {.id = "visibility"}});
```

`RadioOption` is the same `{key, label, action, disabled}` description as `ListItem`. The host owns the selected key and every option's command; register each action in the validation context and command registry. Lowering and navigation never change the selection. `std::nullopt` checks no option; an unknown key fails lowering instead of silently clearing.

The group requires a stable nonempty ID and label. Keys must be nonempty and unique, and every option needs a nonempty label. `CollectionProps::disabled` disables every option; `RadioOption::disabled` disables one. Invalid keys, labels, selection and document limits return `radio_group_*` diagnostics through `Session::lower`.

Generated IDs are `<id>-title`, `<id>-option-<key>` and `<id>-label-<key>`. The group is a Column with a 4-unit gap unless `Props::style` sets a direction or gap. Each option is a Row Box with a `(o)`/`( )` marker Text followed by its label Text. Style the parts by ID; for example, hide the title when a neighboring label already names the group, as the Inspector does. A hidden title still names the group.

## Semantics and document vocabulary

The group projects the `radio_group` role, named by its title, without an action of its own. Each option projects `radio`, named only by its label through `labelled_by`, with `selected` as its checked state and one ordinary `activate` action while enabled. The marker is presentational. Disabled and modal-blocked options remain exposed without actions or focus.

In UI JSON v1, Box-only `collection_role` adds `radio_group` and `radio`. Radios are direct children of a `radio_group`, require an activate action and may carry `selected`; at most one is checked per group. The group cannot have an activate action. Both roles round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the additions; existing documents keep their encoding.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. As for other controls, the host focuses the pressed option; release requests its action.
- Translate arrows and gamepad directions to `Navigate`. On a focused radio, every direction moves through the group's eligible options in preorder: Up/Left to the previous, Down/Right to the next, wrapping at either end. Unmodified Home/End reach the first/last. Movement to a different option also returns that option's `activate` request, so selection follows focus through one ordinary command; staying on the same option requests nothing. Disabled options are skipped.
- `Activate` requests the focused option's action.
- `FocusNext`/`FocusPrevious` enter each group once: at its checked option when that option is eligible, otherwise at the first eligible option. From any option in a group, either direction leaves that group. A group with no eligible options has no stop.
- Execute returned requests with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose with the new selected key. A rejected command leaves focus on the new option while the accepted selection stays unchanged.

Replay plays normalized `Navigate` and semantic action steps through the same dispatcher; host-built replacement views carry the accepted selection.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_radio_group_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(radio_group|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [owner fixture](../../tests/input/radio_group_tests.cpp) covers composition, direction override, semantics, disabled options and groups, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2, Tab entry/exit, wrapping arrows with selection requests, Home/End and lowering/document rejection.

A standalone radio outside a group, nested groups, a mixed/indeterminate state, automatic checking without a host command, type-ahead, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
