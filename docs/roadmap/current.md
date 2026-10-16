# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Productive tool UI

Objective: extend one bounded consumer operation from the Path-finder/editor workflow through the ordinary Tessera runtime:

```text
author -> reload -> inspect -> operate -> capture -> verify
```

### Current slice

Review the next bounded consumer operation using the [editor fixture](../../examples/editor-prototype/main.cpp), its [consumer contract](../integrations/path-finder.md#implemented-in-process-panel-fixture), and [label history evidence](../reference/support-matrix.md#editor-label-history-evidence--2026-10-16). Choose from observed authoring friction in the author/reload/inspect/operate/capture/verify loop. Keep application state, selection, resource execution and edit history host-owned; use the same UI document and runtime for execution and preview.

This review schedules no additional feature implementation. Promote a follow-up only from an observed operation, its expected result and minimum required change. The broader [productive tool candidates](backlog.md#v040-candidate--productive-tool-ui) and [integration design](../integrations/path-finder.md#proposed-mini-prototype) are context; their widget and tooling inventories are not exit gates.

### Implementation rule

Add runtime functionality only when this fixture cannot proceed without it, or a failing test demonstrates a correctness gap required by an existing contract. Record the failing operation, its observable expected result, the minimum fix, and its focused regression check before extending a subsystem. Consult the [support matrix](../reference/support-matrix.md) for capability evidence; this roadmap does not assert which blocker has been reproduced.

Do not deepen component lifecycle because adjacent Proposed sections lack implementations. Use the lifecycle foundation until the active fixture or an existing test demonstrates a concrete missing behavior. Stop subsystem work once the fixture can proceed and its required invariants pass. Preserve full-tree evaluation as the correctness reference.

### Exit criteria

- Identify one concrete consumer operation or validation requirement that warrants a follow-up slice, with observable acceptance and a subsystem stop condition.
- Route its minimum scope here and broader inactive mechanisms to backlog. Do not infer native/editor, real-font/GPU, arbitrary migration or serialized workflow requirements from the scripted fixture.
- Preserve label editing, Undo/Redo/branching, gesture/eligibility, source replacement and stale reload as regression constraints when selecting subsequent work.

### Non-goals and stop condition

A complete visual editor, generalized lifecycle declarations, resource payload/provider frameworks, arbitrary reload migrations, full reflection, Portal/OverlayRoot and a Tooltip/Dropdown/Modal catalog, command palette, VirtualList, platform IME/clipboard hardening, versioned snapshot bundle/CLI, capability manifest, and performance optimization are not prerequisites. Promote only a bounded part if the fixture demonstrates a blocker; otherwise their scope stays in [unscheduled editor extensions](backlog.md#unscheduled-editor-extensions) or the other backlog candidates.

When the loop above works and its required invariants pass, stop infrastructure expansion and use the fixture's observed authoring friction to select subsequent scope. Web/browser hosts, Python/application bindings, broader layout/animation and retained/incremental rendering stay in backlog.

Live capability and validation evidence belongs to the [support matrix](../reference/support-matrix.md); delivery history belongs to the [changelog](../../CHANGELOG.md).
