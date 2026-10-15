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

The Inspector selects one authored content string using public property descriptors and joins its effective value and template source span from the accepted inspection generation. Its host-owned draft uses the [logical string edit transform](../architecture/input.md#implemented-logical-string-editing), with UTF-8 byte anchor/caret observations displayed as ordinary Text nodes. Committed text and supported normalized editing keys are consumed before surrounding navigation/activation; value changes resolve an ordinary command and join one [gesture transaction](../architecture/commands.md#implemented-edit-gesture-boundary). Selection requests from synthetic pointer, keyboard, logical gamepad and semantic input execute the same selection command after dispatch. Command eligibility gates every operation. Draft presentation creates no history; completion records one host-owned before/after entry, and Escape restores the accepted value/selection with a newer revision.

The host edits the selected property's source bytes in memory, using runtime serialization for JSON escaping. It loads the candidate with source records, admits an otherwise identical document with that one compatible string change, and stages ordinary component lowering, command eligibility, style/layout, copied focus recovery, inspection, invariants and paint before committing history and accepting one generation. Staging retains mounted owners and delivers no effects or cleanup. Compatible label reload retains keyed state, selection, editing owner, focus and accepted async observations; old generation handles reject.

Both label and source-structure reload require the supplied command generation and inspection identity to match the host's accepted presentation. A stale or foreign presentation returns `stale_generation` at `/generation` before parsing the candidate, staging, closing a gesture or reconciling owners. This request diagnostic leaves accepted state and an open draft intact; retry uses the accepted presentation. Equal command counters from different editor hosts do not establish inspection identity.

Compatible label staging returns presentation rejection through the host operation's result diagnostics. Diagnostics retain their reported source; an unlocated staging failure is associated with the candidate's edited property value span. Rejection leaves the open draft, gesture and host history intact, so the same edit can be retried and committed once after presentation succeeds. This boundary handles declared presentation diagnostics; it does not contain arbitrary exceptions or allocation failures.

The separate source-structure operation admits only removal of the direct `headline` Text, replacement with an empty Box preserving its other authored fields, and restoration of the original authored Text. All other document changes reject. The preview component kind follows these three source shapes; hierarchy row kind follows Text/Box identity. Staging projects retained state for compatible rows and initial state/idle async status for fresh owners, without mounting. After successful presentation, acceptance cancels an affected draft against the accepted snapshot, reconciles the exact staged invocation records, clears incompatible Inspector selection/editing state or joins compatible selection to its new source span, and publishes one generation before explicit cleanup delivery. The preview observation owner and affected row recreate; unrelated panel/row owners and keyed state retain their lifetimes. Ineligible or removed hierarchy targets recover focus through the ordinary dispatcher and cannot execute selection commands.

Invalid JSON, schema validation, unsupported changes and text measurement/paint failures leave the exact accepted source/generation and owner lifetimes usable with source-located diagnostics. Old editing owners, generation handles and async attempts cannot address restored successors. This host operation supplies no generalized topology reload, rollback of mounting/runtime failures or migration API; the fixed invocation schemas and host callbacks are fixture constraints under [invocation mounting](../architecture/ui-model.md#implemented-component-invocation-mounting).

One declared preview async string follows explicit host start/failure/retry/completion operations. Replacing the preview with an empty component removes its observation owner, clears host selection/editing state and delivers latest-attempt detachment once through explicit cleanup delivery; recreation creates a fresh observation and editing lifetime, so old completion/edit requests reject. Scope shutdown uses the same delivery boundary.

Accepted component documents and styles feed ordinary replay, semantic operation requests and the CPU offscreen runner. Numeric geometry agrees with the accepted layout, repeated runs compare semantic/layout/action observations and owned RGBA8 bytes, and source label edits/removal/type replacement alter pixels. Inputs declare a fixed logical viewport/scale/time, placeholder shaping and an injected synthetic scalar rasterizer; images stay in memory and establish no real-font/native/GPU rendering claim. There is no editor transport, IME/clipboard adapter, editable caret geometry or serialized edit gesture protocol. [Current work](../roadmap/current.md#current-slice) owns integration scope; [development](../guides/development.md#editor-panel-fixture) owns execution procedures.

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
