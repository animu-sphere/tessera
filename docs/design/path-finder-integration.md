# Path-finder integration

## Shared model

```text
Path-finder edits -> Tessera UI document -> validate/load -> Tessera runtime
```

Path-finder owns hierarchy/property editing, visual authoring, drag/drop, preview orchestration, and generated source. Tessera owns the runtime model and validation. Preview and deployed applications consume the same [UI document](ui-model.md), not divergent editor/runtime node models.

The foundation uses namespaced document/node `extensions` objects for design metadata; [JSON v1](../../formats/tessera-ui/README.md) defines their semantic preservation policy. A sidecar remains an optional later editor choice. Runtime behavior does not interpret editor-only fields. This storage boundary does not implement an editor bridge or reload.

## Implemented source-consumption boundary

[CMakeLists.txt](../../CMakeLists.txt) exposes `tessera::core` for an existing source checkout consumed with `add_subdirectory` or `FetchContent`. The target propagates public headers and the C++20 requirement; consumers link the alias without duplicating include paths or language settings. Optional `tessera::vulkan` propagates core and Vulkan SDK requirements when the host explicitly enables the backend. Source acquisition and SDK installation belong to the host; Tessera configuration downloads nothing.

An embedded Tessera project defaults its tests and examples off independently of the host's testing setting. Standalone builds retain their development defaults. Option controls and concrete source-build procedures belong to [development](../guides/development.md#source-consumption-workflow). The host owns application state, windows/devices and scheduling under [architecture](architecture.md#ecosystem-ownership); linking a source target supplies no preview bridge, reload transaction, installed package or ABI guarantee.

The [consumer fixture](../../tests/cmake/consumer/CMakeLists.txt) imports the source in both modes beneath a separately named host project with its own tests enabled. Its core executable exercises ordinary document serialization and tree creation. Its optional backend executable checks linkage and empty-context rejection without creating a device or window; GPU behavior follows [rendering](rendering.md#implemented-vulkan-primitive-boundary). The [driver](../../tests/cmake/run-consumer.cmake) keeps configure/build/test logs in isolated build directories and uses an existing local source path for FetchContent.

## Proposed editor bridge

Expose narrow operations for document/schema inspection, validation diagnostics, reload requests, and preview results. The [source diagnostic boundary](inspection.md#implemented-source-diagnostic-boundary) allows Path-finder to link validation errors back to authored text using the runtime's owned records. Avoid adding editor SDKs or reflection frameworks to the core.

Generate property inspectors from the runtime's [reflection metadata](ui-model.md#proposed-property-reflection), and consume [shared inspection](inspection.md) for semantic/property/layout/source observations. Editor categories are metadata, not a second validation/default table. Keep runtime and editor schema versions compatible and preserve source locations in diagnostics.

Bridge transport, protocol versioning, process boundaries, preview asset resolution, and source-generation ownership are open. Start with an in-process adapter only if that is enough to prove shared-model behavior; do not commit to IPC before a consumer requires it.

## Live reload transaction

Proposed sequence:

1. Parse and validate the edited source using the runtime schema, preserving source origins.
2. Lower components and resolve required references into a candidate using ordinary runtime boundaries.
3. Analyze identity/type/state-schema compatibility and prepare declared state preservation or migration.
4. Settle the candidate and calculate full-tree style/layout/semantics/paint, including focus/interaction recovery, before acceptance.
5. Atomically publish one coherent generation at the host update boundary, then render and expose inspection, invariant evaluation, and capture for that generation.

A rejected candidate leaves the last valid tree usable. Successful reload cleans up removed bindings/components and preserves only state declared compatible by identity, node/component type, and state schema. Author IDs and reconciliation keys must have explicit scopes before this can be reliable.

Application state remains host-owned throughout reload. GPU resources replaced by a document follow completion-safe retirement; see [rendering](rendering.md). Initial atomic full-document reload can precede fine-grained patches.

Compatible reload preserves declared keyed component identity/owner and local state, focus, compatible scroll offsets or anchors, selection by stable logical/semantic identity, accepted async values when resource identity is unchanged, and command ownership where valid. Generation-bound handles and action targets must be resolved again against the accepted generation. Compatibility must be checked under each owning subsystem's rules; matching an author ID alone is insufficient. Type/state-schema changes, removed nodes, or incompatible bindings require declared subtree recreation, reset/cleanup, and recovery rather than forced preservation. Migration failure rejects the candidate.

Invalid parse, validation, lowering, or presentation candidates leave the previous valid generation visible and usable, with source-located diagnostics. Preparation must not dispose retained owners or deliver candidate effects before acceptance; rejected staging work is discarded and its resources cleaned up safely. Atomic publication is a UI-generation guarantee, not rollback of unrelated host application data. Reload reuses the ordinary generation/publication model rather than creating an editor-specific runtime. Tooling may request a snapshot after that generation renders and measure edit-to-verified-snapshot latency under [inspection](inspection.md#proposed-diagnostics-and-comparisons).

## Proposed mini prototype

A small editor built from ordinary Tessera components proves the shared runtime under application pressure. It contains a toolbar, keyed hierarchy/list panel, central preview, schema-driven Inspector, command palette, portal-based tooltip/dropdown/modal, string property editing, async thumbnail/resource, live reload, and an undoable edit gesture. Path-finder owns editor orchestration and host application history; these panels use Tessera's common component, input, command, inspection, and rendering contracts.

The acceptance loop edits labels/styles/layout while preserving compatible state and keyed identity, rejects invalid edits while displaying the previous valid frame, and emits source diagnostics plus a coherent verified snapshot. Inspector edits use ordinary commands/transactions. Keyboard/gamepad operation, modal focus recovery, unrealized list identity, async owner removal, and repeated reload exercise the same boundaries as deployed applications.

Headless replay/capture and an [agent capability manifest](inspection.md#proposed-agent-capability-manifest) expose the same prototype for inspection, declared operation, reload, invariant evaluation, and comparison. Measure component ergonomics, lifecycle/focus friction, reflection gaps, portal geometry, text composition, large-list behavior, async ownership, and edit-to-verification latency to inform later scope. The prototype is an integration consumer; it does not require a complete visual editor or a separate agent execution path.

## Verification and scope

Use one document fixture in both a runtime loader and an editor preview consumer. Check invalid edits, stable-ID state preservation, type changes, deleted nodes, focus recovery, extension metadata round trips, and cleanup across repeated reloads.

An integration prototype precedes any complete visual editor, and a full bridge depends on component identity and schema foundations; scheduling is owned by the [roadmap](../roadmap/README.md). Invalid candidates follow the [error boundary](ui-model.md#proposed-error-boundaries) rule, and editor edits group through the [transaction boundary](commands.md#proposed-transaction-boundary).
