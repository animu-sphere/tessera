# Checkbox and Switch

`authoring::Checkbox` and `authoring::Switch` compose one controlled Boolean from ordinary Boxes and Text. They share a contract and differ only in presentation. The [Inspector](inspector-hierarchy.md#shadows-switch) uses a Checkbox for Enabled and a Switch for Cast shadows.

## Compose a controlled Boolean

```cpp
namespace ui = tessera::authoring;
auto enabled = ui::Checkbox("Enabled", item.enabled, "item.enabled", {.node = {.id = "enabled"}});
auto shadows = ui::Switch("Cast shadows", item.shadows, "item.shadows", {.node = {.id = "shadows"}});
```

The label is a `Value<std::string>`. The checked value and `CheckboxProps::disabled` (`SwitchProps` is the same type) are `Value<bool>`, so literals, state and computed values all work. The host owns the value and its command. Register the action in the validation context and command registry. Activation requests the command and never writes the value; the host inverts its observation and recomposes, or the next lowering reads the updated state.

Both controls require a stable nonempty ID; lowering without one fails with `checkbox_id` or `switch_id`. Generated IDs are `<id>-label` for both, plus `<id>-track` and `<id>-thumb` for a Switch. The owner is a Row with an 8-unit gap unless `Props::style` sets one; a Switch also centers its parts vertically unless `align` is set. Other styles, classes and source use `Props`.

- **Checkbox.** A `[x]`/`[ ]` marker Text followed by the label Text.
- **Switch.** A 32×18 track with 3-unit padding and a 9-unit corner radius, then the label. The track's 12-unit circular white thumb sits at the start while off and at the end while on; the track is gray while off and blue while on. These are instance styles, so a style sheet rule targeting the generated IDs can restyle the parts but cannot move the thumb against the value.

A checked read from a foreign or expired reference fails at the owner's `selected` property, with the caller's source when supplied.

## Semantics and document vocabulary

A Checkbox projects the `checkbox` role and a Switch the `switch` role. Each is named only by its label through `labelled_by` and carries `selected` as its checked/on state. While enabled, it exposes one ordinary `activate` action and focus. The marker, track and thumb are presentational. A disabled control stays exposed with its state but without actions or focus.

In UI JSON v1, the Box-only string `toggle_role` is `checkbox` or `switch`. A toggle requires a `selected` boolean and an activate action, and cannot combine with a collection role, `modal`, `separator` or `scrollbar`. `selected` on any other Box still requires a list/tree item or radio. Both roles round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the additions; existing documents keep their encoding.

This replaces the earlier Checkbox projection, in which the control was a button whose content name included the marker, such as `[x] Enabled`. Observe the label name and checked state instead.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. As for other controls, the host focuses the pressed control; a press and release on any part, including the track, thumb or label, requests its action once.
- Each control is one ordinary Tab stop. `Activate` (map Enter/Space and gamepad A) requests the focused control's action. Arrows do not toggle.
- Execute returned requests with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose with the new value. A rejected command leaves the accepted value unchanged.

Replay plays normalized activation and semantic action steps through the same dispatchers; host-built replacement views carry the accepted value.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_toggle_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(toggle|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [owner fixture](../../tests/input/toggle_tests.cpp) covers composition, reactive and literal values, Switch thumb geometry and track color, semantics, disabled controls, keyboard and pointer activation on every Switch part, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2 and lowering/document rejection.

A mixed/indeterminate state, checkbox groups, automatic toggling without a host command, thumb drag or animation, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
