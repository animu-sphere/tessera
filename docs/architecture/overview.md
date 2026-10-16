# Architecture

## Core invariants

- The host owns application state, devices, external execution and frame scheduling; core APIs contain no platform, browser, GPU SDK, OpenUSD or editor SDK types.
- Layout consumes resolved values independently of rendering; backends consume resolved draw data independently of application/component state.
- Execution, preview, replay, inspection and agent operation share ordinary runtime generations and eligibility boundaries.

> [!NOTE]
> Implemented sections describe source-backed contracts. Proposed sections are Direction, and open decisions are Exploration. Neither schedules implementation; see [documentation authority](../README.md#contract-labels) and the [agent policy](../agent-policy.md).

## Purpose and constraints

[Product vision](../product/vision.md), [principles](../product/principles.md), [intended use cases](../product/use-cases.md) and [non-goals](../product/non-goals.md) own product intent. This page owns subsystem boundaries, dependency direction and coherent runtime updates.

The core concentrates on document and component identity, reactive view updates, style, layout, semantics, input and focus, actions, paint lists, the resource contract, inspection, and replay. OpenUSD, application asset systems, browser engines, editor SDKs, game engines, AI providers, and RPC transports reach it only through adapters, hosts, or optional modules.

Model updates through dependency, identity, time, and ownership: what invalidates a value, what persists across updates, when work may run, and who disposes it. The [reactive kernel](reactive-runtime.md) sits beneath components and preserves application-state ownership. Its dependency graph is distinct from its owner tree. The long-term performance goal is work proportional to affected observable UI, measured against representative workloads while preserving full-tree correctness.

Use real application workflows to choose runtime extensions. A new subsystem should close an authoring, interaction, observation, or verification loop, or satisfy a concrete requirement of the [Path-finder prototype](../integrations/path-finder.md#proposed-mini-prototype). Prefer connecting existing boundaries before deeper abstraction or optimization. Application traces and measured authoring friction guide broader layout, incremental evaluation, rendering retention, and backend work.

[Localization](localization.md) treats locale as a first-class reactive subtree context and preserves message identity until resolution before text measurement. Message formatting, Unicode/font processing, logical layout and semantic naming participate in one coherent update while keeping separate interfaces. Direction-aware layout belongs in runtime primitives; translation resources reach the runtime through host services, and no concrete localization library enters the core API.

## Ecosystem ownership

| Owner | Responsibilities |
| --- | --- |
| Tessera | UI tree and components, style/layout, events/actions, focus/navigation, semantic projection, text interfaces, paint generation, backend contract, document serialization |
| animu-sphere or another host | Windows, application lifecycle/state, world simulation, input devices, GPU device/queue ownership, frame scheduling, external resource system |
| Path-finder | Visual/structural editing, hierarchy and property panels, drag/drop authoring, source generation, preview orchestration, design-time metadata |

Tessera can expose optional adapters for engine or USD-backed data. A world scene reaches UI through application/view-model bindings, rather than becoming the internal UI tree. The visual editor and full application framework remain separate products.

## Pipeline and dependency direction

Applications primarily depend on an authoring surface and composed widget/component library above the runtime. The [C++ authoring proposal](../proposals/cpp-authoring.md) explores that surface. Runtime details such as reactive graph records, component mounting and reconciliation stay behind it for ordinary authoring; ownership and lifetime remain defined by the common contracts.

```text
Host application state <- command registry / transaction API
          |                       ^
     view model                   | semantic actions
          |                       |
Authoring API / DSL / bindings
          |
Component / widget library -> UiDocument / retained nodes
                            |
                    components + reactive bindings/events
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

Language frontends expose the public authoring/host model with language-appropriate APIs, rather than mechanically mirroring internal C++ classes. Any C ABI facade requires explicit handle, lifetime and result rules. C++, JSON, Path-finder, foreign-language bindings, Web hosts and agent operation share runtime semantics, inspection and replay; different authoring surfaces cannot establish separate execution models.

C++ construction, future DSLs, and visual authoring lower to the same [UI model](ui-model.md#common-representation). [Component descriptions](ui-model.md#implemented-component-description-and-lowering) lower into ordinary documents; composition extensions must not establish a second runtime. Dependency, owner, and batching semantics are defined by the [reactive runtime](reactive-runtime.md). Fine-grained layout/text evaluation and retained GPU updates require full-tree parity; they are not prerequisites for defining the IR or the reactive kernel's semantics.

[Inspection](inspection.md) joins semantic, layout, property, and source observations for DevTools, testing, and agent adapters. It observes settled generations and invokes declared operations without exposing mutable internals. Action requests from every input source resolve to application [commands](commands.md), whose metadata also drives menus, shortcuts, palettes, and agent discovery. An agent follows the same loop as other tooling: inspect a generation, resolve a semantic/source target, invoke a declared operation or edit its source, reload, capture, run invariants, compare, and accept or iterate. Application execution, editor preview, tests, and agents share the ordinary generation/publication boundary, including [reload](../proposals/live-reload.md#live-reload-transaction). [Web hosting](../proposals/web-host.md) projects the same semantics to DOM and renders resolved draw data to a WebGPU canvas, with platform editing handled at the host boundary.

The retained model may provide `Canvas` / custom paint callbacks for graphs, profilers, and debug drawing; the [graph canvas](../integrations/graph-editor.md#proposed-canvas-structure) is the main proposed consumer. These callbacks emit the same bounded paint vocabulary described in [rendering](rendering.md). They do not create a second widget framework.

## Frame scheduling

The host explicitly advances the runtime:

```text
poll/normalize input -> update application -> dispatch UI events
-> settle UI state -> resolve styles -> layout -> semantics + paint
-> submit UI rendering -> host presents
```

Full-tree style/layout/semantic/paint updates are the reference path. Property metadata identifies affected stages under [UI model](ui-model.md#implemented-property-metadata) and [styling](styling.md#property-effects). The proposed scheduler settles view values and bindings before these stages, publishes one validated generation, and delivers effects at explicit commit/submission boundaries under [reactive update batches](reactive-runtime.md#proposed-update-batches-and-scheduler). Introduce stage dirtiness, then finer incremental work only after proving equivalence to the reference.

### Update and snapshot rules

Architectural direction for future runtime orchestration:

- Apply mutations at defined update points. Do not mutate the tree during traversal; defer requested changes until traversal ends.
- Host action execution follows dispatch return, as defined in [input](input.md#action-registration-and-lifetime). No host callback runs inside core traversal.
- Resolve state, styles, layout, semantics, and paint against one coherent update generation before submission.
- Publish asynchronous resource readiness/replacement at a later update point, never into a snapshot already being traversed or submitted.
- Render submission borrows a coherent snapshot and obeys the completion/retirement contract in [rendering](rendering.md#implemented-frame-contract).
- Do not promise a multithreaded public API. Reentrant entry during an update must be rejected or deferred by a declared policy.

The immutable tree and per-call contracts do not constitute a scheduler. [Reactive scheduling](reactive-runtime.md#proposed-update-batches-and-scheduler) proposes thread confinement, queue semantics, failure containment, and reentrancy rules; its concrete orchestration API must preserve the snapshot boundaries above.

## Determinism

For identical document/state, normalized input, time, viewport/scale, styles, locale, font/asset configuration, random seed when used, and resource readiness, require reproducible style results, layout geometry, semantic output, action order, and paint commands. Use declared numeric tolerances where exact geometry is inappropriate. The [CPU reference backend](rendering.md#implemented-scalar-cpu-reference-backend) in deterministic mode is the image reference for a declared configuration; GPU images are compared with it under backend/device tolerances. [Replay](replay.md) owns recording these inputs and observing outputs; [inspection](../proposals/snapshot-bundles.md#proposed-runner-and-snapshot-bundle) owns coherent observation bundles.

## Module organization

Keep core, optional text implementations, renderer backends, and host/editor adapters independently consumable. Public core APIs expose no concrete SDK types. Repository directories are an implementation detail, not a parallel status inventory.

Backends can remain modules in this repository. Split packages or repositories only when independent consumers justify it. Core-only builds must be possible without GPU, text implementation, engine, or editor dependencies; see [dependencies](../reference/dependencies.md).

## Open decisions

- Packaging and ABI policy; no stable ABI promise precedes downstream evidence.
- Incremental node mutation/allocation. The implemented owned value tree, immutable snapshots, handles, and diagnostics are defined in [UI model](ui-model.md).
- Backend-specific render target, resize, and device-loss rules. The core submission/retirement contract is defined in [rendering](rendering.md).
- Explicit migrations beyond [JSON v1](../../formats/tessera-ui/README.md); unsupported versions fail.

Resolve decisions through the relevant [milestone](../roadmap/current.md), with explicit evidence rather than assuming illustrative APIs are final.
