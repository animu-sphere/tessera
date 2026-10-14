# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.3.0 — Dynamic UI and editor prototype

Objective: prove one Path-finder/editor workflow through the ordinary Tessera runtime:

```text
author -> reload -> inspect -> operate -> capture -> verify
```

### Current slice

Carry the [editor fixture](../../examples/editor-prototype/main.cpp) through source-authored node removal and type replacement at its staged reload boundary. Keep application state, selection, resource execution and edit history host-owned; use the same UI document and runtime for execution and preview.

Extend only the compatibility exercised by one label removal/type-change fixture. Reconcile affected owners after successful staging, recover focus and clear or re-resolve Inspector selection/editor lifetime, and reject old targets/completions. Keep rejected candidates on the exact accepted source/generation and compare deterministic observations through the same in-process capture path. The broader [integration design](../integrations/path-finder.md#proposed-mini-prototype) is context; its entire widget and tooling inventory is not an exit gate.

### Implementation rule

Add runtime functionality only when this fixture cannot proceed without it, or a failing test demonstrates a correctness gap required by an existing contract. Record the failing operation, its observable expected result, the minimum fix, and its focused regression check before extending a subsystem. Consult the [support matrix](../reference/support-matrix.md) for capability evidence; this roadmap does not assert which blocker has been reproduced.

Do not deepen component lifecycle because adjacent Proposed sections lack implementations. Use the lifecycle foundation until the active fixture or an existing test demonstrates a concrete missing behavior. Stop subsystem work once the fixture can proceed and its required invariants pass. Preserve full-tree evaluation as the correctness reference.

### Integration blockers and minimum fixes

These are conditional work boundaries, not a subsystem priority queue. Establish each blocker in the fixture before selecting its supporting work.

| If the fixture demonstrates this blocker | Minimum supporting work | Owning constraint |
| --- | --- | --- |
| A source node removal/type replacement cannot stage and commit without prematurely disposing retained owners | Stage the exact fixture candidate through ordinary lowering/presentation, commit its affected owner replacement, and preserve unaffected keyed state | [Reload transaction](../proposals/live-reload.md#live-reload-transaction), [invocation mounting](../architecture/ui-model.md#implemented-component-invocation-mounting) |
| A removed/replaced source target retains Inspector editing state or redirects old work to its successor | Recover focus and host selection/editor lifetime at acceptance; exercise ordinary stale handle, edit-owner and async-attempt rejection with once-only cleanup | [Logical string editing](../architecture/input.md#implemented-logical-string-editing), [owner-bound detachment](../architecture/ui-model.md#implemented-owner-bound-async-detachment) |

### Exit criteria

- **Source replacement:** one source-authored label removal and type change each publish one accepted generation through ordinary runtime loading/lowering. Dispose/recreate only affected owners, preserve unaffected keyed state, recover focus/Inspector selection and reject stale targets and editing requests.
- **Rejected staging:** parse, validation or presentation failure preserves the exact accepted source/generation and owner lifetimes, with source-located diagnostics and no candidate cleanup/effect delivery to retained owners.
- **Verification:** fixed document/state, logical input/readiness, time, viewport/scale and declared synthetic text inputs reproduce semantic/layout/invariant observations and CPU bytes for these accepted replacements. Removing/recreating the affected async owner rejects old completion and delivers required cleanup once. Preserve the label-edit/gesture/eligibility fixture as regression coverage; add no generalized migration or serialized editor protocol.

### Non-goals and stop condition

A complete visual editor, generalized lifecycle declarations, resource payload/provider frameworks, arbitrary reload migrations, full reflection, Portal/OverlayRoot and a Tooltip/Dropdown/Modal catalog, command palette, VirtualList, platform IME/clipboard hardening, versioned snapshot bundle/CLI, capability manifest, and performance optimization are not prerequisites. Promote only a bounded part if the fixture demonstrates a blocker; otherwise their scope stays in [unscheduled editor extensions](backlog.md#unscheduled-editor-extensions) or the other backlog candidates.

When the loop above works and its required invariants pass, stop infrastructure expansion and use the fixture's observed authoring friction to select subsequent scope. Web/browser hosts, Python/application bindings, broader layout/animation and retained/incremental rendering stay in backlog.

Live capability and validation evidence belongs to the [support matrix](../reference/support-matrix.md); delivery history belongs to the [changelog](../../CHANGELOG.md).
