# Architecture

## Purpose and constraints

Tessera is a deterministic GPU-native UI runtime for humans and agents. It is backend-neutral, declarative, deterministic, inspectable, replayable, and operable by people and by machines, for editors, games, DCC tools, and visualization. Its value is not a fast renderer alone: one runtime contract carries UI, meaning, operation, verification, and automation, so the loop of building, inspecting, semantically operating, replaying, verifying, and optimizing UI stays inside the runtime. The combination of [semantic projection](semantics.md), the [command registry](commands.md), [inspection](inspection.md), [replay](replay.md), snapshots, and [UI invariants](inspection.md#proposed-ui-invariants) is the core of that contract. Tessera does not aim to replace Qt completely or reimplement the Web.

The retained, declarative component model targets dense application UI, including inspectors, timelines, data grids, node graphs, conversational/agent workspaces, and UI alongside 3D viewports. [Graph editors](graph-editor.md) and [conversational workspaces](conversational-ui.md) are reference workloads that stress text, virtualization, input, custom paint, commands, and automation together; they remain consumers of the common runtime rather than separate frameworks. Advanced UI is composed from small runtime primitives instead of a large widget catalog. Source review, reusable composition, predictable updates, live reload, accessibility, and machine observation/operation are design requirements.

The core concentrates on document and component identity, style, layout, semantics, input and focus, actions, paint lists, the resource contract, inspection, and replay. OpenUSD, application asset systems, browser engines, editor SDKs, game engines, AI providers, and RPC transports reach it only through adapters, hosts, or optional modules.

Long-term constraints:

1. Declarative composition above imperative execution.
2. Layout is independent of rendering; rendering is independent of application state.
3. UI documents are backend-neutral, serializable, deterministic, and versioned.
4. The core has no browser engine, JavaScript VM, platform SDK, or OpenUSD requirement.
5. Keyboard/gamepad interaction, text, and semantic metadata are first-class subsystems.
6. Prefer composition, explicit ownership, small interfaces, and useful diagnostics.
7. Keep dependencies modular; validate boundaries before optimizing draw calls.
8. Path-finder edits Tessera documents and uses the same runtime for preview.
9. DevTools, tests, and external agents share inspection and ordinary action boundaries; no agent SDK or AI model is required by core.
10. Native and Web hosts share the runtime model; browser services belong to optional host adapters.
11. Applications own their state, data, and edit history. Tessera provides view bindings and [command and transaction boundaries](commands.md), not an application model or undo store.
12. Pointer, keyboard, gamepad, accessibility, and agent input reach the application through one semantic action and command path with the same eligibility; no input source gets a private route.

## Ecosystem ownership

| Owner | Responsibilities |
| --- | --- |
| Tessera | UI tree and components, style/layout, events/actions, focus/navigation, semantic projection, text interfaces, paint generation, backend contract, document serialization |
| animu-sphere or another host | Windows, application lifecycle/state, world simulation, input devices, GPU device/queue ownership, frame scheduling, external resource system |
| Path-finder | Visual/structural editing, hierarchy and property panels, drag/drop authoring, source generation, preview orchestration, design-time metadata |

Tessera can expose optional adapters for engine or USD-backed data. A world scene reaches UI through application/view-model bindings, rather than becoming the internal UI tree. The visual editor and full application framework remain separate products.

## Pipeline and dependency direction

```text
Host application state <- command registry / transaction API
          |                       ^
     view model                   | semantic actions
          |                       |
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
                        Vulkan / WebGPU / CPU reference
```

Frontends and host adapters depend on the common runtime representation. Backend implementations consume resolved draw data; they do not reach back into layout, event dispatch, or component state. Text and asset interfaces inject external services without importing their implementation types into UI contracts.

C++ construction, future DSLs, and visual authoring lower to the same [UI model](ui-model.md#common-representation). Proposed component descriptions extend that representation; they must not establish a second runtime. Fine-grained reactive updates and retained GPU data are optimization directions after full-tree parity, not prerequisites for defining the IR.

[Inspection](inspection.md) joins semantic, layout, property, and source observations for DevTools, testing, and agent adapters. It observes settled generations and invokes declared operations without exposing mutable internals. Action requests from every input source resolve to application [commands](commands.md), whose metadata also drives menus, shortcuts, palettes, and agent discovery. An agent follows the same loop as other tooling: inspect, resolve a semantic target, invoke an action or command, capture a snapshot, and verify it. [Web hosting](web-host.md) projects the same semantics to DOM and renders resolved draw data to a WebGPU canvas, with platform editing handled at the host boundary.

The retained model may provide `Canvas` / custom paint callbacks for graphs, profilers, and debug drawing; the [graph canvas](graph-editor.md#proposed-canvas-structure) is the main proposed consumer. These callbacks emit the same bounded paint vocabulary described in [rendering](rendering.md). They do not create a second widget framework.

## Frame scheduling

The host explicitly advances the runtime:

```text
poll/normalize input -> update application -> dispatch UI events
-> settle UI state -> resolve styles -> layout -> semantics + paint
-> submit UI rendering -> host presents
```

Full-tree style/layout/semantic/paint updates are the reference path. Property metadata identifies affected stages under [UI model](ui-model.md#implemented-property-metadata) and [styling](styling.md#property-effects). Introduce dirty flags, then dirty-subtree and finer reactive updates only after proving equivalence to that reference.

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

For identical document/state, normalized input, time, viewport/scale, styles, locale, font/asset configuration, random seed when used, and resource readiness, require reproducible style results, layout geometry, semantic output, action order, and paint commands. Use declared numeric tolerances where exact geometry is inappropriate. The [CPU reference backend](rendering.md#proposed-cpu-reference-backend) in deterministic mode is the image reference for a declared configuration; GPU images are compared with it under backend/device tolerances. [Replay](replay.md) owns recording these inputs and observing outputs; [inspection](inspection.md#proposed-runner-and-snapshot-bundle) owns coherent observation bundles.

## Module organization

Keep core, optional text implementations, renderer backends, and host/editor adapters independently consumable. Public core APIs expose no concrete SDK types. Repository directories are an implementation detail, not a parallel status inventory.

Backends can remain modules in this repository. Split packages or repositories only when independent consumers justify it. Core-only builds must be possible without GPU, text implementation, engine, or editor dependencies; see [dependencies](../reference/dependencies.md).

## Open decisions

- Packaging and ABI policy; no stable ABI promise precedes downstream evidence.
- Incremental node mutation/allocation. The implemented owned value tree, immutable snapshots, handles, and diagnostics are defined in [UI model](ui-model.md).
- Backend-specific render target, resize, and device-loss rules. The core submission/retirement contract is defined in [rendering](rendering.md).
- Explicit migrations beyond [JSON v1](../../formats/tessera-ui/README.md); unsupported versions fail.

Resolve decisions through the relevant [milestone](../roadmap/current.md), with explicit evidence rather than assuming illustrative APIs are final.
