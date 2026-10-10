# Commands and transactions

## Proposed command registry

Commands are the semantic operations an application exposes, such as `file.open`, `edit.undo`, `view.frame_selection`, or `node.delete`. They decouple widgets from application functions: a menu item, toolbar button, shortcut, gamepad binding, accessibility client, agent, or replay step requests a command instead of calling the application directly. This page proposes a design; it does not change [JSON v1](../../formats/tessera-ui/README.md) or the implemented [action request](input.md#action-registration-and-lifetime) contract.

A command descriptor carries:

| Field | Meaning |
| --- | --- |
| ID | Stable, namespaced identifier; never a display string |
| Label, description | Localizable presentation text supplied by the application |
| Category | Semantic grouping for palettes, menus, and discovery |
| Shortcut | Optional declared key or gamepad chord; conflicts are diagnosed, not silently resolved |
| Enabled, checked | Query results the application derives from its own state, with an optional disabled reason |
| Argument schema | Typed parameters validated before invocation; no coercion |
| Result/error contract | Typed result or structured error returned to the requester |
| Availability | Whether the command exists in development builds, production builds, or both |

The application owns the registry contents and every implementation. Tessera stores descriptors and evaluates eligibility; it holds no callbacks inside core traversal, consistent with [action lifetime](input.md#action-registration-and-lifetime). The registry reaches a subtree through explicit [context](ui-model.md#proposed-context) rather than a global locator.

## Proposed invocation path

```text
pointer / keyboard / gamepad / accessibility / agent / replay
        -> normalized event or semantic operation (input, semantics, inspection)
        -> ActionRequest after eligibility checks
        -> command resolution and argument validation (this page)
        -> application implementation after dispatch returns
```

A document binding keeps naming an action. A host may declare that action name as a command ID, so bound widgets, shortcuts, and external requests converge on one descriptor without new document fields. Command enabled state composes with node eligibility: a disabled node cannot request a command, and a disabled command makes its bound nodes report the disabled state through [semantics](semantics.md) in the same generation. Each invocation records its source (pointer, keyboard, gamepad, accessibility, agent, or replay) for diagnostics, transactions, and [replay](replay.md).

Commands evaluated or invoked from outside the UI tree, such as a palette or an agent, use the same eligibility query and argument validation. No source receives a private path or bypasses a disabled state. External discovery is exposed through the [agent capability manifest](inspection.md#proposed-agent-capability-manifest).

## Proposed command palette

The command palette is the reference consumer of the registry and is composed from ordinary components, list virtualization, and text input; it is not a special runtime case. It provides fuzzy search over labels, categories, and descriptions; category grouping; shortcut display; recently used commands and command history; disabled reasons; and argument prompts generated from the argument schema. Search ranking and history storage are application- or library-owned policies. A [graph editor's](graph-editor.md#proposed-interaction-and-editing) node-creation search can reuse the same component with a filtered command set.

## Proposed transaction boundary

Tessera does not own application data history. It provides a common boundary so that UI-driven edits group consistently:

```text
begin_transaction(metadata) -> application mutations -> commit_transaction()
                                                     -> rollback_transaction()
```

Transaction metadata includes an ID, a label, a logical sequence (and host timestamp when supplied), the source (pointer, keyboard, agent, or replay), a merge/coalesce hint, and affected object IDs. A continuous gesture such as a drag, a transform handle, or slider scrubbing forms one transaction, so a 100-frame drag is one history entry. Cancellation of a gesture rolls back. Inspectors, property editors, node editors, drag and drop, transform UI, text editing, and multi-selection edits share this boundary.

The application applies mutations, keeps the undo stack, and decides merge policy. Tessera reports boundaries at defined update points and never mutates application data itself. Undo and redo are ordinary commands (`edit.undo`, `edit.redo`) whose enabled state comes from the application's history.

## Ownership

[Input](input.md#event-to-action-boundary) owns event-to-action translation and eligibility, and [semantics](semantics.md) owns which actions a node exposes. This page owns action-to-command resolution, descriptors, command eligibility, invocation records, and transaction boundaries. [Inspection](inspection.md) exposes commands to tooling. Applications own implementations, data, and history.

## Open decisions

- Command ID grammar and namespace ownership across libraries.
- Argument schema vocabulary and its relation to [property metadata](ui-model.md#implemented-property-metadata).
- Nested transactions and whether a command can open a transaction implicitly.
- Long-running commands and their [async state](ui-model.md#proposed-async-state).
- Shortcut scopes, conflict diagnostics, and platform key conventions.

## Verification

Check that every input source produces the same command invocation under the same eligibility, that disabled commands and nodes agree in semantics, that invalid arguments fail with located diagnostics, that stale targets fail after replacement, and that a continuous gesture produces exactly one committed or rolled-back transaction. Use replayed fixtures rather than callback mocks; follow the [testing strategy](../guides/testing.md).
