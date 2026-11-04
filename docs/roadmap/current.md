# Current

Inactive candidates live in [backlog](backlog.md). [Support](../reference/support-matrix.md) owns capability/validation evidence and the [changelog](../../CHANGELOG.md) owns delivery history.

## SplitPane

The Settings/Inspector resize operation is delivered in the [changelog](../../CHANGELOG.md#unreleased), with capability and validation evidence in [support](../reference/support-matrix.md#splitpane-evidence--2026-11-03). The [public-API guide](../guides/split-pane.md) and [Settings starter](../guides/settings-starter.md#resize-the-sidebar) own usage and host procedures. No SplitPane work items remain.

## Next operation

The in-parent Stack/Overlay operation is delivered: a [viewport toolbar](../guides/stack.md) accepts pointer, keyboard/gamepad commands and stays aligned on resize. [Support](../reference/support-matrix.md#stack-evidence--2026-11-04) owns validation evidence and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No Stack work items remain.

## Inspector Grid

The [Grid Inspector operation](../guides/grid.md) is delivered: shared label/value columns, narrow-width wrapped rows and controlled input preserve ordinary commands, edits and focus on resize. [Support](../reference/support-matrix.md#grid-evidence--2026-11-04) owns validation evidence and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No Inspector Grid work items remain.

## Popup and Tooltip

The [dedicated theme-selection example](../guides/popup-tooltip.md) delivers a root-sibling Popup and hover/focus Tooltip: anchor placement follows scrolling and resize, with outside-press/Cancel dismissal, focus recovery and pointer-transparent help. [Support](../reference/support-matrix.md#popup-and-tooltip-evidence--2026-11-04) owns validation evidence and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No Popup/Tooltip work items remain.

## Modal and Dialog

The dedicated [settings-reset confirmation](../guides/modal-dialog.md) delivers a single root Modal and title-labelled Dialog: background input/semantic actions are blocked, keyboard/gamepad focus stays inside, Cancel/Escape closes without resetting, confirmation uses an ordinary command, and closure restores a surviving eligible opener. [Support](../reference/support-matrix.md#modal-and-dialog-evidence--2026-11-04) owns validation evidence and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No Modal/Dialog work items remain.

## TreeView and ListView

The [folder/asset browser](../guides/tree-list.md) delivers controlled single selection, branch expansion/collapse and dependent detail text through ordinary commands. Pointer and keyboard/gamepad input share item actions; collapsed descendant selection survives, focus recovers to its logical ancestor, and scrolling/resize retain state. [Support](../reference/support-matrix.md) owns validation scope and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No single-selection TreeView/ListView work items remain.

## Menu and ContextMenu

The [asset-actions operation](../guides/menu-context.md) delivers controlled single-level Menu and ContextMenu: button/keyboard/gamepad opening, right-click/Shift+F10 context triggers, bounded point placement, ordered/wrapped item focus, disabled-command skipping, ordinary command execution, outside/Cancel/Escape/Tab dismissal and eligible anchor recovery. [Support](../reference/support-matrix.md) owns validation scope and the [changelog](../../CHANGELOG.md#unreleased) owns delivery history. No single-level Menu/ContextMenu work items remain.

Choose another application operation from the [backlog](backlog.md) before promoting further scope. Submenus, menu bars, richer menu item kinds, multiple selection, virtualization, absolute positioning, advanced Grid placement, generalized Portal and nested/stacked modal lifetimes remain inactive.
