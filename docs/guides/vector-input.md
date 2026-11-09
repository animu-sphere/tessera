# VectorInput

`authoring::VectorInput` presents a labelled vec2, vec3 or vec4 of numbers. Each component is a compact number input with its own host-owned draft and command, so a host edits X, Y and Z exactly as it edits a [NumberInput](text-number-input.md). The [Inspector](inspector-hierarchy.md#position-vector-input) uses one for each item's Position.

## Compose a vector

```cpp
namespace ui = tessera::authoring;
auto position = ui::VectorInput("Position",
                                {{"x", "X", item.position_draft[0], "item.position.x"},
                                 {"y", "Y", item.position_draft[1], "item.position.y"},
                                 {"z", "Z", item.position_draft[2], "item.position.z"}},
                                {.node = {.id = "position"}});
```

Supply two to four `VectorComponent` records. Each has a `key`, unique and nonempty within the vector, a `Value<std::string>` label, a `Value<TextEditState>` draft and an action. Register every component action in the validation context and command registry. `VectorInputProps` (the same type as `NumberInputProps`) supplies node props and a `Value<bool>` disabled flag that disables every component. Lowering validates each draft with the [logical editor](../architecture/input.md#implemented-logical-string-editing) and never advances its revision.

The vector requires a stable nonempty ID. Lowering rejects a missing ID with `vector_input_id`, fewer than two or more than four components with `vector_input_components`, and empty or duplicate keys with `vector_input_keys`. A malformed draft fails at its component's generated value Text. Reserve the generated IDs:

- `<id>`: a Row (default gap 6, centered) holding `<id>-label` and `<id>-fields`.
- `<id>-fields`: a growing Row with a 4-unit gap holding the components in declared order.
- `<id>-<key>`: a component Row (gap 4, centered) of `<id>-<key>-label` and an `<id>-<key>-field` Row that grows, clips its content and holds the `<id>-<key>-value` draft Text.

Components have a zero width basis and equal grow, so they share the fields row equally whatever their drafts' lengths. Give the vector an assigned or stretched width, such as a Grid column; its natural width does not include the components. There are no step parts. Style the parts by ID; for example, hide `<id>-label` when a neighboring label already names the vector, as the Inspector does. A hidden label still names the group.

## Semantics and document vocabulary

The owner projects the additive `group` role from the Box-only `collection_role: group`, named by `<id>-label` and without actions, value or selection. Each component projects a `number_input` under it, named by its own label (X, Y, Z) and carrying its draft text as `value`; while enabled it exposes focus and one ordinary `activate` action. Component labels and drafts are presentational. A disabled vector keeps its components exposed with their values but without actions or focus.

In UI JSON v1 a group carries no activate action, and no collection role, including a group, may appear inside a toolbar. Components use the ordinary `edit_role: number_input` vocabulary. The group role round trips in Semantic JSON v1 and Replay v1/v2 targets and is accepted by inspection/invariants; older readers reject it.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. Each component is its own Tab stop in declared order. A press on any part of a component presses that component; the group label is not a press target. Activating a component supplies no edit payload, so the host treats it as focus only.
- While a component is focused, route committed text, editing keys, Enter, Escape and Up/Down to that component's draft and command exactly as for a [NumberInput](text-number-input.md#wire-input), using `parse_number`, `format_number` and `step_number` with the component's range.
- Execute each request with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose. A rejected command leaves every accepted value and draft unchanged.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_vector_input_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(vector_input|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [owner fixture](../../tests/input/vector_input_tests.cpp) covers two, three and four components, generated IDs and sources, equal component geometry and clipped fields, group and component semantics, reactive drafts, disabled vectors, Tab order and pointer targeting, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2 and lowering/document/snapshot rejection.

Drag-to-scrub labels, linked or uniform editing across components, per-component step parts, a generated caret and native editable accessibility are not included. [Support](../reference/support-matrix.md) owns validation scope.
