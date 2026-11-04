# Current

## v0.7.0 candidate: Application Experience

Shift from widget coverage to making an ordinary desktop application comfortable to build, operate, inspect and verify.

### Selected operation: select and edit an Inspector item

Build a small Inspector / Hierarchy reference application relevant to [Path-finder](../integrations/path-finder.md), without an engine or OpenUSD dependency:

- Select and expand/collapse items with [TreeView](../guides/tree-list.md).
- Edit the selected item's display name, enabled flag and numeric property through TextInput / Checkbox / Slider in a [Grid Inspector](../guides/grid.md).
- Adjust the hierarchy/detail split with [SplitPane](../guides/split-pane.md); retain selection and editing state through scrolling, resizing and keyboard/pointer interaction.
- Route item actions through existing Menu / ContextMenu commands, using Dialog only where confirmation is needed.
- Observe the selected item and edited values through ordinary [inspection](../architecture/inspection.md), [semantics](../architecture/semantics.md) and [replay](../architecture/replay.md).

Start with existing widgets and public C++ APIs. Add widget, host or runtime changes where this operation exposes a concrete blocker, rather than building a general editor shell first.

### Done when

- The select -> edit -> select another item -> return scenario runs through an ordinary host connection; accepted edits remain visible in the Inspector and the display name agrees with the hierarchy.
- Scrolling, pane/window resizing and keyboard/pointer operation preserve the selected item and its editing state.
- The scenario's selection and property outcomes are reproducible through the shared observation paths. Changed contracts have owner tests and the operation has one consumer smoke under [testing](../guides/testing.md#verification-ownership).
- Actual blockers found in the scenario are resolved, with remaining capability and validation limits recorded in the support matrix.

Clipboard, basic Windows IME and development tools follow demonstrated editing/observation needs; completing all of them is not a v0.7.0 release gate. Further candidates: [backlog](backlog.md).

Implemented capabilities and limits: [support matrix](../reference/support-matrix.md).
Delivered work: [changelog](../../CHANGELOG.md).
