# Path-finder integration

> [!IMPORTANT]
> Consumer design and requirements do not independently authorize core work. Proposed sections are Direction, and open decisions are Exploration. Promote only a demonstrated blocker into [current work](../roadmap/current.md), under the [agent policy](../agent-policy.md).

## Shared model

```text
Path-finder edits -> Tessera UI document -> validate/load -> Tessera runtime
```

Path-finder owns hierarchy/property editing, visual authoring, drag/drop, preview orchestration, and generated source. Tessera owns the runtime model and validation. Preview and deployed applications consume the same [UI document](../architecture/ui-model.md), not divergent editor/runtime node models.

The foundation uses namespaced document/node `extensions` objects for design metadata; [JSON v1](../../formats/tessera-ui/README.md) defines their semantic preservation policy. A sidecar remains an optional later editor choice. Runtime behavior does not interpret editor-only fields. This storage boundary does not implement an editor bridge or reload.

## Implemented source-consumption boundary

[CMakeLists.txt](../../CMakeLists.txt) exposes `tessera::core` for an existing source checkout consumed with `add_subdirectory` or `FetchContent`. The target propagates public headers and the C++20 requirement; consumers link the alias without duplicating include paths or language settings. Optional `tessera::vulkan` propagates core and Vulkan SDK requirements when the host explicitly enables the backend. Source acquisition and SDK installation belong to the host; Tessera configuration downloads nothing.

An embedded Tessera project defaults its tests and examples off independently of the host's testing setting. Standalone builds retain their development defaults. Option controls and concrete source-build procedures belong to [development](../guides/development.md#source-consumption-workflow). The host owns application state, windows/devices and scheduling under [architecture](../architecture/overview.md#ecosystem-ownership); linking a source target supplies no preview bridge, reload transaction, installed package or ABI guarantee.

The [consumer fixture](../../tests/cmake/consumer/CMakeLists.txt) imports the source in both modes beneath a separately named host project with its own tests enabled. Its core executable exercises ordinary document serialization and tree creation. Its optional backend executable checks linkage and empty-context rejection without creating a device or window; GPU behavior follows [rendering](../architecture/rendering.md#implemented-vulkan-primitive-boundary). The [driver](../../tests/cmake/run-consumer.cmake) keeps configure/build/test logs in isolated build directories and uses an existing local source path for FetchContent.

## Implemented in-process panel fixture

The [editor fixture](../../examples/editor-prototype/main.cpp) composes a toolbar, keyed hierarchy, central preview and Inspector through ordinary component invocations and full-tree style/layout/semantic/paint evaluation. Its [preview document](../../examples/editor-prototype/preview.json) loads through JSON v1 with source records; runtime serialization preserves document and node editor extensions, and composition preserves the preview subtree's node metadata. Host-owned selection points to preview author IDs, independently of hierarchy row keys and runtime generation handles.

The Inspector selects one authored content string using public property descriptors and joins its effective value and template source span from the accepted inspection generation. It displays these observations as ordinary Text nodes; it has no text-editing buffer, caret, history or source mutation operation. Selection requests from synthetic pointer, keyboard, logical gamepad and semantic input resolve and execute the same declared command after dispatch. Command eligibility governs each path. Keyed hierarchy state and focus survive reorder; previous-generation semantic targets reject.

One declared preview async string is driven by explicit host start/failure/retry/completion operations and projected into the preview status. Scope shutdown queues latest-attempt detachment for explicit once-only host delivery. The fixture uses placeholder text, no native window or renderer, and provides no atomic source reload or editor transport. The [active slice](../roadmap/current.md#current-slice) owns the edit/reload/capture work; [development](../guides/development.md#editor-panel-fixture) owns execution procedures.

## Proposed editor bridge

Expose narrow operations for document/schema inspection, validation diagnostics, reload requests, and preview results. The [source diagnostic boundary](../architecture/inspection.md#implemented-source-diagnostic-boundary) allows Path-finder to link validation errors back to authored text using the runtime's owned records. Avoid adding editor SDKs or reflection frameworks to the core.

Generate property inspectors from the runtime's [reflection metadata](../proposals/property-reflection.md#proposed-property-reflection), and consume [shared inspection](../architecture/inspection.md) for semantic/property/layout/source observations. Editor categories are metadata, not a second validation/default table. Keep runtime and editor schema versions compatible and preserve source locations in diagnostics.

Bridge transport, protocol versioning, process boundaries, preview asset resolution, and source-generation ownership are open. Start with an in-process adapter only if that is enough to prove shared-model behavior; do not commit to IPC before a consumer requires it.

## Live reload transaction

Editor reload uses ordinary candidate validation and generation publication. Rejection preserves the accepted UI, and preservation requires declared identity/type/schema compatibility rather than an author ID alone.

Possible mechanisms: [live reload transactions](../proposals/live-reload.md#live-reload-transaction). This direction is not an implementation task; only the active roadmap can require a bounded part.

## Proposed mini prototype

A small editor built from ordinary Tessera components proves the shared runtime under application pressure. It contains a toolbar, keyed hierarchy/list panel, central preview, schema-driven Inspector, command palette, portal-based tooltip/dropdown/modal, string property editing, async thumbnail/resource, live reload, and an undoable edit gesture. Path-finder owns editor orchestration and host application history; these panels use Tessera's common component, input, command, inspection, and rendering contracts.

The acceptance loop edits labels/styles/layout while preserving compatible state and keyed identity, rejects invalid edits while displaying the previous valid frame, and emits source diagnostics plus a coherent verified snapshot. Inspector edits use ordinary commands/transactions. Keyboard/gamepad operation, modal focus recovery, unrealized list identity, async owner removal, and repeated reload exercise the same boundaries as deployed applications.

Headless replay/capture and an [agent capability manifest](../proposals/agent-capability-manifest.md#proposed-agent-capability-manifest) expose the same prototype for inspection, declared operation, reload, invariant evaluation, and comparison. Measure component ergonomics, lifecycle/focus friction, reflection gaps, portal geometry, text composition, large-list behavior, async ownership, and edit-to-verification latency to inform later scope. The prototype is an integration consumer; it does not require a complete visual editor or a separate agent execution path.

## Verification and scope

Use one document fixture in both a runtime loader and an editor preview consumer. Check invalid edits, stable-ID state preservation, type changes, deleted nodes, focus recovery, extension metadata round trips, and cleanup across repeated reloads.

An integration prototype precedes any complete visual editor, and a full bridge depends on component identity and schema foundations; scheduling is owned by the [roadmap](../roadmap/README.md). Invalid candidates follow the [error boundary](../proposals/error-boundaries.md#proposed-error-boundaries) rule, and editor edits group through the [transaction boundary](../architecture/commands.md#proposed-transaction-boundary).
