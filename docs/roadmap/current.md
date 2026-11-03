# Current

Inactive candidates live in [backlog](backlog.md). [Support](../reference/support-matrix.md) owns capability/validation evidence and the [changelog](../../CHANGELOG.md) owns delivery history.

## SplitPane

Extend the public C++ [Settings starter](../../examples/settings-starter/main.cpp) into a resizable Settings/Inspector tool. The completed v0.5.0 application flow is recorded in the [changelog](../../CHANGELOG.md#v050--2026-11-03); the [starter guide](../guides/settings-starter.md) owns its copy/build/run procedure.

### Work items

- Add public horizontal and vertical two-pane components with a splitter handle, an initial split, minimum pane sizes and a logical handle size; compose multi-pane screens by nesting them.
- Compose pane sizing with existing Flex constraints and route handle input through the ordinary pointer, keyboard and command APIs. Choose a canonical split ratio or logical size owned by the host/component.
- Implement press/move/release/cancel, keyboard adjustment and clamping after parent-size changes. Retain the split through presentation replacements and handle capture loss or handle removal during a drag.
- Map drag positions into the parent's logical coordinates and use the resolved handle geometry for drawing and hit testing. Connect resize cursors and separator/adjustable semantics through the available host and semantic APIs.
- Add a draggable sidebar/editor boundary to the Settings/Inspector example, preserving category selection, pending edits, scrolling and Apply/Cancel state.
- Cover both orientations, drag/resize clamping, cancellation, state retention and keyboard adjustment in the affected owner tests and consumer smoke; add a short public-API usage guide.

The next layout tasks are [Stack/Overlay, Absolute and Grid](backlog.md#layout-follow-ups).
