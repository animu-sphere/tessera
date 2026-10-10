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

### Descriptor and eligibility rules

- Command IDs use dot-separated segments matching `[a-z][a-z0-9_]*`, with at least two segments. The application reserves its leading namespace and registers library namespaces explicitly; duplicate IDs fail before publishing the registry. Display labels never resolve commands.
- A descriptor is an owned immutable value in one registry revision. Availability is checked before discovery or resolution; production requests cannot discover or invoke a development-only descriptor. The host publishes eligibility queries together with settled UI state, without callbacks during traversal. A query yields enabled, checked, and an optional disabled reason, and errors fail closed.
- Arguments are a named object of declared required/optional booleans, finite numbers with inclusive bounds, UTF-8 strings with byte bounds, string enumerations, or host object IDs. Defaults are descriptor values validated on registration. Unknown fields, missing required values, type mismatches, and invalid references fail at the argument field; there is no coercion. Reuse the scalar vocabulary of [property metadata](ui-model.md#implemented-property-metadata), with command schemas owned here rather than added to UI JSON.
- An action binding resolves by exact command ID only when the host explicitly registers that action-to-command mapping. Existing action names retain the [input action](input.md#action-registration-and-lifetime) contract. The mapping, descriptor revision, and eligibility observation belong to one generation. A request carries that generation, command ID, arguments, invocation source, and logical request sequence; stale generations fail before any host implementation runs.
- Effective enabled state is the conjunction of ordinary node/ancestor eligibility and command eligibility. The host applies the same eligibility observation to pointer/focus dispatch, style, and semantic projection when settling the generation. It does not merely hide an action in the semantic adapter. Requests without a node use the same command query and argument validation.
- Dispatch returns an owned validated invocation. Immediately before executing it, the host rechecks generation, registry revision, and eligibility; changes reject the invocation instead of activating a replacement. Execution happens after traversal. Results are success with a declared serializable value, cancellation, or a structured error with command/request identity and located diagnostics. Host exceptions are converted at the execution adapter, never inside core.

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

### Transaction rules

An application adapter owns a transaction token and every mutation. A discrete editing command requests one explicit begin/commit pair; non-editing commands request none. Continuous gestures begin once on the first accepted edit and commit on completion. Escape, capture loss, owner removal/reload, or command failure requests rollback once, even after multiple updates. Undo/redo implementations consume application history and do not implicitly open another transaction. A commit/rollback result settles before the next published UI generation.

Transactions are single-level: nested begin requests fail with a located diagnostic. A command invoked within a gesture may join its explicitly supplied token but cannot implicitly begin or commit it. Each token belongs to one host session, command/gesture owner, and logical request sequence; a closed or foreign token rejects later edits and a late asynchronous completion cannot reopen it. Merge hints never merge in core; the application decides history coalescing after a successful commit. Rollback failure is reported as a host error, without claiming application state was restored.

Long-running commands either perform no mutation until ready and then request a discrete transaction, or explicitly keep one gesture-owned token whose cancellation requests rollback. They use [async request identities](ui-model.md#proposed-async-state) and publish transitions only at update points. Pending execution grants no exemption from command eligibility, stale-owner rejection, or the transaction token rules.

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

- Concrete C++ registration/query/execution adapter types and ownership of descriptor revision storage.
- Host object-ID validation and the serialized invocation/result schema, including size limits.
- Shortcut scopes, conflict diagnostics, and platform key conventions.

## Verification

First implementation fixtures: one menu command requested by pointer, keyboard, scripted gamepad, semantic identity, and replay with identical validated arguments; eligibility changing between dispatch and execution; a removed/replaced owner with a reused author ID; malformed and unknown arguments; a development-only command in production discovery; and a multi-update edit with one commit, cancellation with one rollback, nested begin rejection, and a late completion after token closure. Record descriptor revisions and eligibility as fixture inputs before extending [Replay](replay.md); no callback-only check establishes generation or semantic agreement.

Check that every input source produces the same command invocation under the same eligibility, that disabled commands and nodes agree in semantics, that invalid arguments fail with located diagnostics, that stale targets fail after replacement, and that a continuous gesture produces exactly one committed or rolled-back transaction. Use replayed fixtures rather than callback mocks; follow the [testing strategy](../guides/testing.md).
