# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.3.0 — Dynamic UI and editor prototype

Objective: prove one Path-finder/editor workflow through the ordinary Tessera runtime:

```text
author -> reload -> inspect -> operate -> capture -> verify
```

### Current slice

Carry the [editor fixture](../../examples/editor-prototype/main.cpp) from panel selection into source editing, using the [tool panel](../../examples/tool-panel/main.cpp) for command gesture patterns. Keep application state, selection, resource execution, and edit history host-owned; use the same UI document and runtime for execution and preview.

Carry one label edit through reload, Inspector operation, deterministic capture, and verification. Extend the fixture's schema-selected string observation with insertion, deletion and caret/selection operations through the ordinary edit boundary; preserve compatible keys, focus and host selection during accepted reload, and leave the accepted generation usable on invalid source. Exercise async owner removal/recreation as part of that loop. The broader [integration design](../integrations/path-finder.md#proposed-mini-prototype) is context; its entire widget and tooling inventory is not an exit gate.

### Implementation rule

Add runtime functionality only when this fixture cannot proceed without it, or a failing test demonstrates a correctness gap required by an existing contract. Record the failing operation, its observable expected result, the minimum fix, and its focused regression check before extending a subsystem. Consult the [support matrix](../reference/support-matrix.md) for capability evidence; this roadmap does not assert which blocker has been reproduced.

Do not deepen component lifecycle because adjacent Proposed sections lack implementations. Use the lifecycle foundation until the active fixture or an existing test demonstrates a concrete missing behavior. Stop subsystem work once the fixture can proceed and its required invariants pass. Preserve full-tree evaluation as the correctness reference.

### Integration blockers and minimum fixes

These are conditional work boundaries, not a subsystem priority queue. Establish each blocker in the fixture before selecting its supporting work.

| If the fixture demonstrates this blocker | Minimum supporting work | Owning constraint |
| --- | --- | --- |
| A source label edit cannot publish atomically while retaining compatible keyed state and Inspector selection | Stage and validate the edited document through ordinary publication; preserve only the compatibility exercised by the fixture, reject invalid candidates, and recreate incompatible nodes safely | [Reload transaction](../proposals/live-reload.md#live-reload-transaction) |
| The Inspector cannot discover or edit its one string property through runtime metadata | Join the property's schema, value, source and generation observations; validate the edit through an ordinary runtime or command/transaction boundary | [Property metadata](../architecture/ui-model.md#implemented-property-metadata), [reflection constraints](../proposals/property-reflection.md#proposed-property-reflection), [editing boundary](../architecture/input.md#proposed-editing-ime-and-clipboard-boundary) |
| Removing or recreating the fixture's async row permits a stale result or unsafe cleanup | Close only the demonstrated owner/request/attempt or disposal gap; use a deterministic host-driven resource fixture before introducing a new payload or execution mechanism | [Typed async observations](../architecture/ui-model.md#implemented-typed-async-view-observations), [owner-bound detachment](../architecture/ui-model.md#implemented-owner-bound-async-detachment) |
| The edit/operate loop cannot produce comparable observations from one accepted generation | Connect replay, inspection and CPU reference capture in-process; add only the observation or comparison boundary the fixture needs | [Replay](../architecture/replay.md#implemented-prototype-playback), [inspection](../architecture/inspection.md#implemented-prototype-capture-and-target-resolution), [CPU capture](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) |

### Exit criteria

- **Shared runtime:** toolbar, hierarchy, preview and Inspector are ordinary Tessera components. Runtime loading and editor preview round-trip the same fixture document and editor metadata; no parallel editor schema or private agent action path is introduced.
- **Reload:** changing the fixture's label source publishes one accepted generation and preserves its declared compatible keyed state, focus and Inspector selection. Invalid source leaves the previous generation usable with source-located diagnostics; type change or removal recreates/disposes the affected owner safely and rejects stale targets.
- **Inspector and operation:** metadata selects an editor for one string property without a hardcoded property table. Insertion, deletion and caret/selection operations update that property through the ordinary edit boundary; invalid values are rejected. Keyboard/gamepad navigation, pointer and semantic requests obey the same eligibility. One edit gesture creates one host-owned history entry, and cancellation restores the accepted value.
- **Lifetime:** keyed reorder preserves the fixture's state and focus. Its async observation can load, fail/retry, and complete deterministically; removing and recreating its owner rejects the old completion and delivers required cleanup once.
- **Verification:** fixed document/state, logical input/readiness, time, viewport/scale and font/resource inputs reproduce semantic/layout/invariant observations and a CPU reference image for the same accepted generation. The source edit has an expected property/source change; invalid reload has no accepted-generation change. Compare affected GPU rendering only when a rendering contract changes, under declared tolerances.

### Non-goals and stop condition

A complete visual editor, generalized lifecycle declarations, resource payload/provider frameworks, arbitrary reload migrations, full reflection, Portal/OverlayRoot and a Tooltip/Dropdown/Modal catalog, command palette, VirtualList, platform IME/clipboard hardening, versioned snapshot bundle/CLI, capability manifest, and performance optimization are not prerequisites. Promote only a bounded part if the fixture demonstrates a blocker; otherwise their scope stays in [unscheduled editor extensions](backlog.md#unscheduled-editor-extensions) or the other backlog candidates.

When the loop above works and its required invariants pass, stop infrastructure expansion and use the fixture's observed authoring friction to select subsequent scope. Web/browser hosts, Python/application bindings, broader layout/animation and retained/incremental rendering stay in backlog.

Live capability and validation evidence belongs to the [support matrix](../reference/support-matrix.md); delivery history belongs to the [changelog](../../CHANGELOG.md).
