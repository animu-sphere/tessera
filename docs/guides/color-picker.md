# ColorPicker

`authoring::ColorPicker` edits one opaque color, such as a display color property. A compact trigger shows a swatch and the `#RRGGBB` value; opening it shows a popup panel with a saturation/value area and a hue strip. Every part is an ordinary Box or Text. The [Inspector](inspector-hierarchy.md#display-color-picker) uses it for each item's Display color.

## Compose a controlled ColorPicker

```cpp
namespace ui = tessera::authoring;
auto color = ui::ColorPicker("Display color", item.color,
                             {.toggle = "color.toggle", .area = "color.area", .hue = "color.hue"},
                             {.node = {.id = "display-color"}, .panel = {.classes = {"popup"}},
                              .open = state.color_open});
auto form = ui::Column({
    ui::Text("Color"),
    std::move(color.field),   // In place, like any control.
    /* ordinary content */
    std::move(color.panel)    // Directly under the root, after ordinary content.
});
```

The value is a `tessera::HsvColor` from [color.hpp](../../include/tessera/style/color.hpp): hue in degrees [0, 360] and saturation/value in [0, 1] of the sRGB components, so it matches hexadecimal sRGB colors. Hue 360 names the same color as 0. Keeping the HSV value, rather than a `Color`, retains the hue while saturation or value is zero. `to_color` converts a valid value to an opaque sRGB `Color` and `format_hex_color` formats one as `#RRGGBB`, rounding halves upward; `valid` checks the ranges.

`ColorPicker` returns `ColorPickerParts{field, panel}`. Compose `field` where the control belongs and `panel` as a direct root child after ordinary content, like a [ComboBox](combobox.md) list. The host owns the color, the open flag and the three commands of `ColorPickerActions`: `toggle` opens and closes the panel, while `area` and `hue` carry the colors the dispatcher requests. Register each in the validation context and command registry. Lowering and input never change the color themselves.

The ColorPicker requires a stable nonempty `node.id`, a nonempty label, a valid value and a finite positive `area` size (160 by 120 by default). `disabled` disables the trigger and both panel controls. `panel` supplies classes, styles and source for the popup; its ID is always generated, so supplying one is an error. Invalid input returns `color_picker_*` diagnostics through `Session::lower`; `color_picker_panel_id` is located at the panel, the others at the trigger.

The trigger is a Row with centered alignment and an 8-unit gap unless `node.style` sets them. Generated IDs are `<id>-title` (display-none, names the trigger), `<id>-swatch` (a 16-unit bordered square filled with the color) and `<id>-value` (the hex text, growing to fill the row). The panel is `<id>-panel`: an anchored [Popup](popup-tooltip.md) Column with `side` and `gap` from the props, an 8-unit gap unless `panel.style` sets one, and a display-none `<id>-panel-title`. It holds:

- `<id>-area`, sized by `area` and filled with the pure hue. Absolute `<id>-area-white` and `<id>-area-black` overlays desaturate toward the left and darken toward the bottom; the `<id>-area-thumb` ring centers on the saturation (x) and value (top is 1) position.
- `<id>-hue`, as wide as the area and 12 units tall, with a left-to-right 0–360 degree gradient and an `<id>-hue-thumb` bar centered on the hue.

Overlays and thumbs have `pointer_events: false`, so presses reach the area and strip. Thumbs extend up to 7 units beyond their control, so give the panel padding, as the Inspector's `popup` class does. Both controls are labelled by display-none `<id>-area-label` / `<id>-hue-label` Text stating their values, such as `Saturation 50%, value 80%` and `Hue 210 degrees`.

The swatch, hex value and thumbs are exact. Gradients interpolate in linear light, so the panel's fills approximate the sRGB model: the hue strip interpolates between 49 exact sRGB samples, and the area's overlay alpha follows the decoded sRGB ramps at 17 stops. On the CPU reference the [owner fixture](../../tests/input/color_picker_tests.cpp) measures at most 24/255 channel deviation inside the area, at most 8/255 for each color's largest and smallest components and at most 5/255 on the strip; the middle component of intermediate hues, such as orange, deviates most.

## Semantics and document vocabulary

The trigger projects the additive `color_picker` role, named by its title, with `value` set to the hex text, `expanded` set to the open flag and, while enabled, one ordinary `activate` action that requests the toggle. Its swatch and Text are presentational. An open panel projects a `group` named by its title, without an action. The area and strip are buttons named by their labels with their own activate actions; they carry no numeric range or value semantics, like [Slider](settings-starter.md). A closed panel is display-none and has no entries.

In UI JSON v1, Box-only `collection_role` adds `color_picker`. A color picker requires an activate action, `expanded` and the presented `edit_value`, and its children are noninteractive Text or Boxes without a collection role; it may sit inside a toolbar. The panel uses the existing `group` role and `popup_anchor`. Violations report `color_picker_action`, `color_picker_state`, `color_picker_child`, `edit_value` and `collection_state`. The role, `value` and `expanded` round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the addition; existing documents keep their encoding.

## Wire input

Include [popup.hpp](../../include/tessera/input/popup.hpp) and retain a `ColorPickerDispatcher` constructed with the trigger's ID and optional `ColorPickerSteps` (5 degrees of hue, 0.05 saturation and 0.05 value per press by default), plus ordinary `PointerDispatcher`/`FocusDispatcher` instances.

1. Refresh the dispatcher on every accepted view, including closed ones and views without the ColorPicker, and apply its `focus`. Opening focuses the area; closing returns focus to an eligible trigger. A `dismiss` from refresh means the trigger disappeared or became ineligible while open: close in host state and publish.
2. Dispatch before ordinary input, passing current-snapshot focus and the accepted color the view presents. Keep at most one menu, list or panel open. A closed ColorPicker only reacts to Alt+Down.
3. The trigger is an ordinary Tab stop. Enter/Space, gamepad `Activate` or a pointer press and release request its toggle action. Non-repeating Alt+Down on the focused trigger returns `open` and `consumed`: run the same toggle command.
4. When a result carries `color` and `action`, execute that area or hue command with the color, accept it and publish the open panel. The values clamp to their ranges and are rounded to 0.0001 (saturation, value) or 0.01 degrees (hue); an unchanged value is consumed without a request. A rejected command keeps the previous color.
5. A primary press on the area or strip focuses it, captures that pointer and requests the color under it; captured moves, including beyond the control, and the release do the same over the control's content box. `PointerCancel` ends the capture. On the focused area, Left/Right change saturation and Up/Down value; on the strip, Left/Down and Right/Up change hue and Home/End select 0 and 360. Gamepad `Navigate` follows the arrows.
6. Tab/`FocusNext` moves from the area to the strip and Shift+Tab/`FocusPrevious` back. Past either end they return **unconsumed** dismissal: close, publish and refresh first, then forward the sequential command from the trigger. Enter, `Activate`, Escape, `Cancel`, Alt+Up and presses outside the panel, including on the trigger, return consumed dismissal with trigger focus. Reset ordinary pointer presses so later releases cannot click through.

Changes apply as they are requested; closing does not revert them. Replay plays normalized input and semantic action steps through the ordinary dispatchers; host-built replacement views carry the accepted color and open state, not the dispatcher's capture.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_color_picker_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(color_picker|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [ColorPicker fixture](../../tests/input/color_picker_tests.cpp) covers the HSV/hex model, composition, panel placement, thumb geometry and input transparency, semantics of the closed, open and disabled parts, CPU swatch/area/strip pixels against the sRGB model, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2, Alt+Down opening only on the focused trigger, area and strip keyboard steps with clamping and rounding, Tab movement and exit, pointer capture, drag clamping, release and cancellation, every dismissal path, trigger focus on close and lowering/document/semantic/dispatcher rejection.

Hex or numeric channel entry, alpha, color spaces other than sRGB, reverting on Escape, eyedropping, swatch palettes, numeric range semantics, GPU image comparison, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
