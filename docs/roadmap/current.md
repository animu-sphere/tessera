# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Productive tool UI

Objective: extend one bounded consumer operation from the Path-finder/editor workflow through the ordinary Tessera runtime:

```text
author -> reload -> inspect -> operate -> capture -> verify
```

### Current slice

Route label history shortcuts from the focused Inspector in the [editor fixture](../../examples/editor-prototype/main.cpp). The bounded operation is: commit a label, retain Inspector focus, send normalized Control+Z to undo, then Control+Shift+Z to redo. Use these as explicit fixture chords, without imposing platform conventions. The [source review](../reference/support-matrix.md#editor-history-shortcut-review--2026-10-16) owns the observed routing gap and its evidence limits; the [consumer contract](../integrations/path-finder.md#implemented-in-process-panel-fixture) owns accepted label history behavior.

Minimum scope:

- First add a focused regression to `run_label_history` demonstrating that normalized chords from Inspector focus must reach the existing history command. Establish the failure before adding routing; do not infer a core defect from the consumer gap.
- Compose existing [logical editing](../architecture/input.md#implemented-logical-string-editing), [keyboard shortcuts](../architecture/commands.md#implemented-keyboard-shortcut-boundary) and ordinary `history_command` execution in the host. Editing gets first refusal; pass its consumed result to shortcut routing, execute returned invocations after dispatch, and stop routing consumed input.
- Bind the two history commands to their toolbar activation owners in the editor root scope, rebuilding the immutable shortcut map for each replacement tree. Share the host sequence stream and accepted command/inspection identity checks with other history requests.
- Keep source snapshots, cursor, selection, resource execution and edit history host-owned. Use the same document, full-tree staging and runtime for execution and preview. Add no core API or encoded input format unless the focused regression demonstrates an existing contract failure.

The broader [productive tool candidates](backlog.md#v040-candidate--productive-tool-ui) and [integration design](../integrations/path-finder.md#proposed-mini-prototype) are context; their widget and tooling inventories are not exit gates.

### Implementation rule

Add runtime functionality only when this fixture cannot proceed without it, or a failing test demonstrates a correctness gap required by an existing contract. Record the failing operation, its observable expected result, the minimum fix, and its focused regression check before extending a subsystem. Consult the [support matrix](../reference/support-matrix.md) for capability evidence; this roadmap does not assert which blocker has been reproduced.

Do not deepen component lifecycle because adjacent Proposed sections lack implementations. Use the lifecycle foundation until the active fixture or an existing test demonstrates a concrete missing behavior. Stop subsystem work once the fixture can proceed and its required invariants pass. Preserve full-tree evaluation as the correctness reference.

### Exit criteria

- With Inspector focus retained, the two normalized chords restore exact before/after source, preview and Inspector values through ordinary history commands. Each successful traversal publishes one generation, moves the cursor once, records keyboard source, and creates no edit transaction or additional history entry.
- Selection, compatible edit owner, keyed state, focus and async observation identity survive traversal. Repeated CPU capture/replay agrees with the corresponding toolbar history result under the fixture's fixed inputs.
- Empty or mismatched history, disabled commands and an open draft cannot change source, cursor or draft. Consumed input and repeat-suppressed matching keys execute no history command; unmatched modifier combinations leave history unchanged.
- Replaced-tree shortcut maps and stale invocations reject without mutation. A layout/paint staging failure preserves accepted source, cursor, focus and owners, returns the ordinary located diagnostic, and permits retry with a fresh request.
- Preserve label editing, Undo/Redo branching, gesture cancellation/eligibility, source replacement and stale reload regressions. Validate the editor and affected input/shortcut/transaction boundaries in Debug and Release, record evidence only in support, and run documentation checks.

### Non-goals and stop condition

A complete visual editor, generalized lifecycle declarations, resource payload/provider frameworks, arbitrary reload migrations, full reflection, Portal/OverlayRoot and a Tooltip/Dropdown/Modal catalog, command palette, VirtualList, platform IME/clipboard hardening, versioned snapshot bundle/CLI, capability manifest, and performance optimization are not prerequisites. Promote only a bounded part if the fixture demonstrates a blocker; otherwise their scope stays in [unscheduled editor extensions](backlog.md#unscheduled-editor-extensions) or the other backlog candidates.

Stop when these two chords complete the existing label history loop and its required invariants pass; return to consumer review. Native shortcut adapters, extra platform/gamepad chords, shortcut customization, draft-local undo and generalized input routing are outside this slice. Web/browser hosts, Python/application bindings, broader layout/animation and retained/incremental rendering stay in backlog.

Live capability and validation evidence belongs to the [support matrix](../reference/support-matrix.md); delivery history belongs to the [changelog](../../CHANGELOG.md).
