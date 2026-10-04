# Architecture

## Purpose and constraints

Tessera provides web-inspired authoring for game and tool UI with a small native runtime. Its retained, declarative model supports source review, reusable components, predictable state updates, editor tooling, and accessibility metadata without requiring a visual editor.

Long-term constraints:

1. Declarative composition above imperative execution.
2. Layout is independent of rendering; rendering is independent of application state.
3. UI documents are backend-neutral, serializable, deterministic, and versioned.
4. The core has no browser engine, JavaScript VM, platform SDK, or OpenUSD requirement.
5. Keyboard/gamepad interaction, text, and semantic metadata are first-class subsystems.
6. Prefer composition, explicit ownership, small interfaces, and useful diagnostics.
7. Keep dependencies modular; validate boundaries before optimizing draw calls.
8. Path-finder edits Tessera documents and uses the same runtime for preview.

## Ecosystem ownership

| Owner | Responsibilities |
| --- | --- |
| Tessera | UI tree and components, style/layout, events/actions, focus/navigation, semantic projection, text interfaces, paint generation, backend contract, document serialization |
| animu-sphere or another host | Windows, application lifecycle/state, world simulation, input devices, GPU device/queue ownership, frame scheduling, external resource system |
| Path-finder | Visual/structural editing, hierarchy and property panels, drag/drop authoring, source generation, preview orchestration, design-time metadata |

Tessera can expose optional adapters for engine or USD-backed data. A world scene reaches UI through application/view-model bindings, rather than becoming the internal UI tree. The visual editor and full application framework remain separate products.

## Pipeline and dependency direction

```text
Host application state -> view model
                            |
Authoring frontend -> UiDocument / retained nodes
                            |
                    components + state/events
                            |
                      resolved styles
                            |
                       LayoutBox tree
                         /       \
                 SemanticTree   UiDrawList
                      |             |
               external adapters  renderer backend
                                    |
                              Vulkan / WebGPU
```

Frontends and host adapters depend on the common runtime representation. Backend implementations consume resolved draw data; they do not reach back into layout, event dispatch, or component state. Text and asset interfaces inject external services without importing their implementation types into UI contracts.

The retained model may provide `Canvas` / custom paint callbacks for graphs, profilers, and debug drawing. These callbacks emit the same bounded paint vocabulary described in [rendering](rendering.md). They do not create a second widget framework.

## Frame scheduling

The host explicitly advances the runtime:

```text
poll/normalize input -> update application -> dispatch UI events
-> settle UI state -> resolve styles -> layout -> semantics + paint
-> submit UI rendering -> host presents
```

Full-tree style/layout/semantic/paint updates are the reference path. Property metadata identifies affected stages under [UI model](ui-model.md#proposed-property-reflection) and [styling](styling.md#property-effects). Introduce dirty flags, then dirty-subtree and finer reactive updates only after proving equivalence to that reference.

### Update and snapshot rules

Architectural direction for future runtime orchestration:

- Apply mutations at defined update points. Do not mutate the tree during traversal; defer requested changes until traversal ends.
- Host action execution follows dispatch return, as defined in [input](input.md#action-registration-and-lifetime). No host callback runs inside core traversal.
- Resolve state, styles, layout, semantics, and paint against one coherent update generation before submission.
- Publish asynchronous resource readiness/replacement at a later update point, never into a snapshot already being traversed or submitted.
- Render submission borrows a coherent snapshot and obeys the completion/retirement contract in [rendering](rendering.md#implemented-frame-contract).
- Do not promise a multithreaded public API. Reentrant entry during an update must be rejected or deferred by a declared policy.

The existing immutable tree and per-call contracts do not constitute a scheduler. Queue ownership, orchestration API, failure/rollback behavior, and enforcement of reentrancy remain decisions to settle with fixtures.

## Determinism

For identical document/state, normalized input, time, viewport/scale, styles, and resource readiness, require reproducible style results, layout geometry, semantic output, action order, and paint commands. Use declared numeric tolerances where exact geometry is inappropriate. GPU images have separate backend/device tolerances. [Replay](replay.md) owns recording these inputs and observing outputs.

## Module organization

Keep core, optional text implementations, renderer backends, and host/editor adapters independently consumable. Public core APIs expose no concrete SDK types. Repository directories are an implementation detail, not a parallel status inventory.

Backends can remain modules in this repository. Split packages or repositories only when independent consumers justify it. Core-only builds must be possible without GPU, text implementation, engine, or editor dependencies; see [dependencies](../reference/dependencies.md).

## Open decisions

- Packaging and ABI policy; no stable ABI promise precedes downstream evidence.
- Incremental node mutation/allocation. The implemented owned value tree, immutable snapshots, handles, and diagnostics are defined in [UI model](ui-model.md).
- Backend-specific render target, resize, and device-loss rules. The core submission/retirement contract is defined in [rendering](rendering.md).
- Explicit migrations beyond [JSON v1](../../formats/tessera-ui/README.md); unsupported versions currently fail.

Resolve decisions through the relevant [milestone](../roadmap/current.md), with explicit evidence rather than assuming illustrative APIs are final.
