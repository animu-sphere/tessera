# Architecture

Status: Architectural direction with implemented Phase 0 tree/document and subsystem boundary contracts, the Phase 1 fixed/stack/flex layout prototype, backend-neutral background/border/Text paint generation, and rectangular pointer targeting/activation. Other layout algorithms, style resolution, focus/navigation, real text, and backends remain draft proposals.

## Purpose and constraints

Tessera provides web-inspired authoring for game and tool UI with a small native runtime. Its retained, declarative model supports source review, reusable components, predictable state updates, editor tooling, and accessibility metadata without requiring a visual editor.

Long-term constraints:

1. Declarative composition above imperative execution.
2. Layout is independent of rendering; rendering is independent of application state.
3. UI documents are backend-neutral, serializable, deterministic, and versioned.
4. The core has no browser engine, JavaScript VM, platform SDK, or OpenUSD requirement.
5. Keyboard/gamepad interaction and text are first-class subsystems.
6. Prefer composition, explicit ownership, small interfaces, and useful diagnostics.
7. Keep dependencies modular; validate boundaries before optimizing draw calls.
8. Path-finder edits Tessera documents and uses the same runtime for preview.

## Ecosystem ownership

| Owner | Responsibilities |
| --- | --- |
| Tessera | UI tree and components, style/layout, events, focus/navigation, accessibility metadata, text interfaces, paint generation, backend contract, document serialization |
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
                            |
                       UiDrawList
                            |
                     renderer backend
                            |
                       Vulkan / WebGPU
```

Frontends and host adapters depend on the common runtime representation. Backend implementations consume resolved draw data; they do not reach back into layout, event dispatch, or component state. Text and asset interfaces inject external services without importing their implementation types into UI contracts.

The retained model may provide `Canvas` / custom paint callbacks for graphs, profilers, and debug drawing. These callbacks emit the same bounded paint vocabulary described in [rendering](rendering.md). They do not create a second widget framework.

## Frame scheduling

The host explicitly advances the runtime:

```text
poll/normalize input -> update application -> dispatch UI events
-> settle UI state -> resolve styles -> layout -> build paint list
-> submit UI rendering -> host presents
```

Full-tree updates are acceptable initially. Introduce separate style/layout/paint dirty flags after correctness is established, then consider finer reactive invalidation. Property changes must identify which stage becomes invalid; pseudo-state changes can affect geometry through styling.

Proposed lifecycle constraints: mutations are applied at defined update points, render submission consumes a coherent frame snapshot, and host callbacks do not mutate a tree during traversal. Threading, callback reentrancy, and asynchronous resource completion rules remain open decisions. No multithreaded API guarantee is made yet.

## Proposed source layout

The complete layout below remains proposed. The current implementation has `CMakeLists.txt`, `include/tessera/{ui,layout,style,input,text,render}`, the matching `src/` directories plus internal `src/detail`, `formats/tessera-ui`, `examples/{hello-ui,flex-layout,pointer-menu}`, and `tests/{serialization,layout,text,input,render}`; other directories remain future work.

```text
CMakeLists.txt
include/tessera/{ui,layout,style,input,text,render,assets}/
src/{ui,layout,style,input,text,render,assets}/
backends/{vulkan,webgpu}/
shaders/
formats/tessera-ui/
examples/{hello-ui,flex-layout,pointer-menu,gamepad-menu,inventory}/
tests/{layout,style,input,text,render,serialization}/
docs/{design,guides,reference,roadmap}/
```

Backends can remain modules in this repository. Split packages or repositories only when independent consumers justify it. Core-only builds must be possible without GPU, text implementation, engine, or editor dependencies; see [dependencies](../reference/dependencies.md).

## Open decisions

- Packaging and ABI policy. The first slice uses C++20 and `tessera::core`; it makes no stable ABI promise.
- Incremental node mutation/allocation. The implemented owned value tree, immutable snapshots, handles, and diagnostics are defined in [UI model](ui-model.md).
- Backend-specific render target, resize, and device-loss rules. The core submission/retirement contract is defined in [rendering](rendering.md).
- Explicit migrations beyond [JSON v1](../../formats/tessera-ui/README.md); unsupported versions currently fail.

Resolve these through the [foundation milestone](../roadmap/current.md), with explicit evidence rather than assuming the illustrative APIs are final.
