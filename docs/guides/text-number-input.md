# TextInput and NumberInput

`authoring::TextInput` and `authoring::NumberInput` present a host-owned `TextEditState` as a labelled single-line input. TextInput edits a string directly; NumberInput edits a text draft that the host parses into its own accepted number. The [Inspector](inspector-hierarchy.md#scale-number-input) uses a TextInput for the item name and a NumberInput for Scale.

## Compose an input

```cpp
namespace ui = tessera::authoring;
auto name = ui::TextInput("Name", item.name, "item.name", {.node = {.id = "name"}});
auto scale = ui::NumberInput("Scale", item.scale_draft, "item.scale", {.node = {.id = "scale"}});
```

The label is a `Value<std::string>`. The edit state and `TextInputProps::disabled` (`NumberInputProps` is the same type) are a `Value<TextEditState>` and a `Value<bool>`, so literals, state and computed values all work. Register the action in the validation context and command registry. Lowering validates the state with the [logical editor](../architecture/input.md#implemented-logical-string-editing) and never advances its revision. An open [composition](../architecture/input.md#implemented-ime-composition-state) presents its preedit.

Both inputs require a stable nonempty ID; lowering without one fails with `text_input_id` or `number_input_id`. Reserve the generated IDs:

- **TextInput.** A Column of `<id>-label`, `<id>-value` (presented text) and an unnamed `Selection <anchor>:<caret>` Text. During composition that Text reads `Composition <begin>:<end> caret <caret>` instead.
- **NumberInput.** A Row (default gap 6, centered) of `<id>-label`, an `<id>-field` Row that grows and holds the `<id>-value` draft Text, then two 20-unit-wide presentational step parts, `<id>-decrease` (`-`) and `<id>-increase` (`+`). It shows no selection or caret; the host owns any caret presentation.

Other styles, classes and source use `Props`. A malformed or foreign edit state fails at the generated value Text's `text` property, with the caller's source when supplied.

## Semantics and document vocabulary

A TextInput projects the `text_input` role and a NumberInput the `number_input` role. Each is named only by its label through `labelled_by` and carries `value`: the presented text, including any preedit. For a NumberInput this is the draft text, not a parsed or accepted number. While enabled it exposes focus and one ordinary `activate` action. The label, value, selection and step Text are presentational. A disabled input stays exposed with its value but without actions or focus.

In UI JSON v1, the Box-only string `edit_role` is `text_input` or `number_input`, paired with the Box-only string `edit_value`. Each requires the other and an activate action, and an edit role cannot combine with a toggle or collection role, `modal`, `separator` or `scrollbar`. Both roles and `value` round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the additions; existing documents keep their encoding.

This replaces the earlier TextInput projection, in which the field was a button whose content name joined label, value and selection, such as `Username Ada Selection 3:3`. Observe the label name and `value` instead, and the selection Text in the document.

## Parse and step numbers

NumberInput does not own a number. The host keeps the accepted value next to its draft and uses three helpers:

- `parse_number(range, text)` accepts the whole string as locale-independent ASCII `-?(0|[1-9][0-9]*)(\.[0-9]+)?` within the inclusive `NumberRange` bounds. Whitespace, `+`, exponents, leading zeros, NaN/infinity and empty text fail with `number_syntax`; finite values outside the bounds fail with `number_out_of_range`. Negative zero becomes zero.
- `format_number(value)` returns the shortest fixed decimal that round-trips the value, with zero as `0`.
- `step_number(range, value, steps)` adds `steps * step`, clamps to the bounds and keeps the decimal places of the value and step, so `0.2 + 0.1` yields `0.3`. It does not snap direct entry to the step grid.

An invalid range (nonfinite bounds, minimum above maximum or a nonpositive step) fails with `number_range`.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. Each input is one Tab stop. A press on any part presses the input owner; activating the field supplies no edit payload, so the host treats it as focus only.
- While an input is focused, map committed `TextInput` events and editing keys (Left/Right/Home/End with optional Shift, Backspace/Delete, Control+A) to revision-addressed `TextEditRequest`s before navigation, and execute them through the input's command.
- For a NumberInput, map unmodified Enter to apply (parse the draft, accept it, replace the draft with `format_number` text) and Escape to revert the draft to the accepted text. Up/Down and presses on the step parts parse the draft, call `step_number` and accept the result. An invalid draft rejects apply and step and stays as typed. The host hit-tests `<id>-decrease`/`<id>-increase` on primary press; the parts have no action of their own.
- Execute each request with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose. A rejected command leaves the accepted value and draft unchanged.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_edit_input_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(edit_input|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [owner fixture](../../tests/input/edit_input_tests.cpp) covers composition and generated IDs, NumberInput geometry, reactive draft and preedit values, semantics, disabled inputs, Tab and pointer targeting, the number grammar, canonical text and steps, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2 and lowering/document/snapshot rejection.

Multiline text, a generated caret for NumberInput, increment/decrement or numeric range semantics, a slider-like drag on the input, locale-aware number formats, native editable accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
