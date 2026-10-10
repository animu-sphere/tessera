# Commands and transactions

## Implemented in-process command boundary

[commands.hpp](../../include/tessera/commands/commands.hpp) defines immutable owned `CommandRegistry`, `CommandSnapshot`, and `CommandInvocation` values. Registry copies share descriptor storage and a process-unique revision; replacement registries receive a distinct revision, even for equal descriptors. There is no global registry or callback. The host reserves command namespaces and passes the registry explicitly; subtree context follows the [context proposal](ui-model.md#proposed-context).

`CommandRegistry::create` validates a candidate atomically. IDs have at least two dot-separated `[a-z][a-z0-9_]*` segments and at most 128 bytes. Parameter names use one such segment. A descriptor owns its ID, nonempty label, description, category, availability (`both`, `development`, or `production`), and parameter schema. Explicit action-to-command mappings resolve exact registered IDs; a label or an unmapped action cannot implicitly resolve a command. Legacy actions keep their [input contract](input.md#action-registration-and-lifetime).

Parameters accept distinct boolean, binary64 number, UTF-8 string, string enumeration, and `CommandObjectId` alternatives. Numbers must be finite and meet optional inclusive bounds. Strings and object IDs meet a descriptor byte bound; object IDs are nonempty and belong to the host's permitted set for the settled generation. Enumeration choices are unique. Defaults are type/range checked on registration and materialized on resolution; object defaults additionally require the current permitted set. A default satisfies an omitted required parameter. Optional parameters without defaults remain absent. Unknown fields, missing required arguments, mismatched types, invalid references, and invalid defaults fail with located diagnostics and no partial value; no coercion occurs.

Bounds are 1,024 descriptors and action mappings, 64 parameters/arguments per command, 64 choices per enumeration, 4,096 bytes per presentation string or parameter string, and 10,000 permitted host object IDs. Descriptor string bounds can be smaller; an object-ID bound is nonzero. Registry revision exhaustion throws, as do allocation failures.

The host publishes `CommandSnapshot` at an update point with a nonzero logical generation, execution mode, owned query observations, and permitted object IDs. Generations and request sequences are host-issued and never reused within a session; they are independent of tree identities and Replay generation indices. Missing observations disable commands. A `query_error` disables the command and clears checked state, even if the supplied flags are true. Unknown observation IDs and invalid modes or metadata reject publication. `discover` returns owned metadata/observations ordered by ID; `lookup` uses exact IDs. Unavailable descriptors cannot be discovered or resolved; their lookup produces the same diagnostic as an unknown command.

`apply_eligibility` validates and copies the authored document before tree/style/layout/semantic construction. It composes unavailable or disabled command state into the `disabled` property of explicitly mapped `activate` owners, preserving authored disabled state and ordinary ancestor eligibility. Always lower from the authored document to restore eligibility on a later update. Input targeting, pseudo-state resolution, focus, and semantic projection then read the same disabled property. Unmapped actions retain their existing behavior. This boundary supports activation bindings; cancellation bindings and shortcut routing follow the invocation proposal below.

`resolve` checks generation, nonzero request sequence, invocation source, availability, eligibility, and arguments for a node-free request. `resolve_action` additionally borrows a coherent `SemanticInput` and verifies the live activation owner and exact binding through ordinary semantic action eligibility. All sources (pointer, keyboard, gamepad, accessibility, agent, and replay) use these checks. The resulting invocation owns normalized arguments, source/sequence, generation/revision, and an optional action owner handle; it retains its command observation without borrowing descriptor or tree memory. It exposes a const record and cannot be constructed by the requester.

Immediately before executing after dispatch returns, the host calls `recheck` against its current command snapshot and, for node invocations, current coherent UI snapshot. A changed registry revision or observation identity rejects the invocation, including accidental reuse of a generation number. A removed/replaced, hidden, disabled, or differently bound node fails ordinary owner validation. A copied snapshot shares its identity. The host is responsible for pairing UI and command observations from one update, rejecting reentrant changes, executing each request once, converting host failures, and publishing later changes as a new generation. `recheck` performs no execution or sequence deduplication. Invocation handles are values, and a destroyed tree is never dereferenced; an author ID reused in a replacement tree does not retain ownership.

This API does not encode command schemas in UI JSON or Replay JSON. [Command checks](../../tests/commands/commands_tests.cpp) declare descriptors, queries, arguments, and object IDs as host fixture inputs, then replay the ordinary UI recording and resolve its action observations through that fixture. [The tool-panel example](../../examples/tool-panel/main.cpp) demonstrates disabled/enabled generations and host execution of a non-editing inspection command. Serialized invocation/results, transaction tokens, shortcuts, palette composition, and execution adapters follow the proposals below.

## Proposed command registry

Commands are the semantic operations an application exposes, such as `file.open`, `edit.undo`, `view.frame_selection`, or `node.delete`. They decouple widgets from application functions: a menu item, toolbar button, shortcut, gamepad binding, accessibility client, agent, or replay step requests a command instead of calling the application directly. This proposal extends the in-process boundary above; it does not change [JSON v1](../../formats/tessera-ui/README.md) or the implemented [action request](input.md#action-registration-and-lifetime) contract.

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

- Concrete host execution/result adapter types and serialized invocation/result schemas, including transport size limits.
- Domain-specific host object-ID validation beyond the settled permitted-ID set.
- Shortcut scopes, conflict diagnostics, and platform key conventions.

## Verification

First implementation fixtures: one menu command requested by pointer, keyboard, scripted gamepad, semantic identity, and replay with identical validated arguments; eligibility changing between dispatch and execution; a removed/replaced owner with a reused author ID; malformed and unknown arguments; a development-only command in production discovery; and a multi-update edit with one commit, cancellation with one rollback, nested begin rejection, and a late completion after token closure. Record descriptor revisions and eligibility as fixture inputs before extending [Replay](replay.md); no callback-only check establishes generation or semantic agreement.

Check that every input source produces the same command invocation under the same eligibility, that disabled commands and nodes agree in semantics, that invalid arguments fail with located diagnostics, that stale targets fail after replacement, and that a continuous gesture produces exactly one committed or rolled-back transaction. Use replayed fixtures rather than callback mocks; follow the [testing strategy](../guides/testing.md).
