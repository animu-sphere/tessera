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

Expose narrow operations for document/schema inspection, validation diagnostics, reload requests, and preview results. Source locations allow Path-finder to link runtime/validation errors back to authored text. Avoid adding editor SDKs or reflection frameworks to the core.

Generate property inspectors from the runtime's [reflection metadata](ui-model.md#proposed-property-reflection), and consume [shared inspection](inspection.md) for semantic/property/layout/source observations. Editor categories are metadata, not a second validation/default table. Keep runtime and editor schema versions compatible and preserve source locations in diagnostics.

Bridge transport, protocol versioning, process boundaries, preview asset resolution, and source-generation ownership are open. Start with an in-process adapter only if that is enough to prove shared-model behavior; do not commit to IPC before a consumer requires it.

## Live reload transaction

Proposed sequence:

1. Parse and validate a candidate document using the runtime schema.
2. Resolve required references and report errors with source locations.
3. Determine which existing node identities and local state remain compatible.
4. Commit the replacement at a defined frame boundary.
5. Recalculate style/layout/paint and recover focus/interaction state.

A rejected candidate leaves the last valid tree usable. Successful reload cleans up removed bindings/components and preserves only state declared compatible by identity, node/component type, and state schema. Author IDs and reconciliation keys must have explicit scopes before this can be reliable.

Application state remains host-owned throughout reload. GPU resources replaced by a document follow completion-safe retirement; see [rendering](rendering.md). Initial atomic full-document reload can precede fine-grained patches.

Compatible reload should preserve keyed component identity/local state, focus, and scroll anchors under their subsystem rules, and reuse still-valid resources. Type/state-schema changes, removed nodes, or incompatible bindings require declared reset/cleanup and recovery rather than forced preservation. A successful reload publishes a new generation before inspection/capture. Tooling may request a snapshot after that generation renders and measure edit-to-verified-snapshot latency under [inspection](inspection.md#proposed-diagnostics-and-comparisons).

## Verification and scope

Use one document fixture in both a runtime loader and an editor preview consumer. Check invalid edits, stable-ID state preservation, type changes, deleted nodes, focus recovery, extension metadata round trips, and cleanup across repeated reloads.

An integration prototype precedes any complete visual editor, and a full bridge depends on component identity and schema foundations; scheduling is owned by the [roadmap](../roadmap/README.md). Invalid candidates follow the [error boundary](ui-model.md#proposed-error-boundaries) rule, and editor edits group through the [transaction boundary](commands.md#proposed-transaction-boundary).
