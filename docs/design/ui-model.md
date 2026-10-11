# UI model

## Implemented foundation contract

The public headers are [document.hpp](../../include/tessera/ui/document.hpp), [tree.hpp](../../include/tessera/ui/tree.hpp), and [serialization.hpp](../../include/tessera/ui/serialization.hpp). They depend only on the C++20 standard library. The serialized fields and normalization/rejection rules are owned by [JSON v1](../../formats/tessera-ui/README.md).

`UiDocument` owns one `UiNode` by value. Each node owns an ordered vector of children; copying performs a deep copy. This representation cannot express cycles, shared children, or multiple parents. Only `NodeKind::box` and `NodeKind::text` exist. Text nodes are leaves. Builder data is mutable until validated; invalid candidates do not produce a runtime tree or serialized result.

`UiTree::create` validates a borrowed const document before copying its own immutable snapshot, so a rejected candidate is not consumed or recursively copied first. `UiTree` is neither copyable nor movable; its owning `unique_ptr` can move. Inspection returns borrowed const node pointers valid until the tree is destroyed. Preorder indices and a process-unique monotonically allocated tree identity form `NodeHandle`; handles from another or destroyed tree fail lookup in a later tree. Index 0 is the root, and the all-zero handle is invalid. Tree identities are never reused; identity exhaustion throws. There is no node removal/reuse in this slice. No runtime handle is serialized. A process identity counter does not provide multithreaded mutation or callback guarantees.

Author IDs are optional document-scoped names used for diagnostics, lookup, and references. Duplicate IDs fail validation. They are distinct from runtime handles and from [reconciliation keys](#implemented-keyed-instance-reconciliation), which are not document fields. Class names are stored in authored order; the [prototype style resolver](styling.md#implemented-prototype-style-resolution) matches them regardless of that order.

The implemented semantic property vocabulary is deliberately small:

| Property | Nodes | Value | Behavior |
| --- | --- | --- | --- |
| `text` | Text only, required | UTF-8 string, including empty text | Measured by layout and shaped by paint through the injected `TextShaper`. Also names [semantic](semantics.md#implemented-prototype-projection) entries |
| `focusable` | Box/Text | Boolean | Stored interaction intent read by [focus dispatch](input.md#implemented-prototype-focus-dispatch) and exposed as semantic state |
| `disabled` | Box/Text | Boolean | Excludes the node/subtree from pointer targeting and semantic actions; absent means false; see [input](input.md) |
| `labelled_by` | Box/Text | `NodeReference` to an existing author ID | Existence validation; names the prototype semantic entry |

`Property` has explicit boolean, binary64, string, and reference alternatives. No current semantic property accepts a number; unknown properties or mismatched types fail without coercion. Absent boolean properties remain absent rather than materializing defaults. Layout/style properties and asset references are deferred; they are not arbitrary semantic properties.

`events` stores `activate`/`cancel` action names. Every bound action must exist in the caller's `ValidationContext.actions`; the default empty context rejects all bound actions. Validation snapshots the names for the call only. Pointer and focus dispatch consume `activate` names, and focus dispatch consumes `cancel` names, to return host action requests as defined in [input](input.md). The document stores no callback, and the host remains responsible for action implementations.

Document/node `extensions` preserve namespaced backend-neutral JSON metadata. Validation and runtime behavior do not interpret its contents. Names, strings, and metadata must be valid UTF-8; numeric metadata must be finite. Diagnostics and bounded recursion are described by the encoding page.

`Result<T>` returns an optional value and diagnostics; a failed load/save/create has no usable partial value. The current diagnostics are errors with stable codes, JSON-pointer locations, actionable messages, and source offsets when loaded. `load_document_with_sources` additionally returns node/property source spans as [inspection](inspection.md#implemented-prototype-capture-and-target-resolution) tooling metadata; it changes neither the document nor its diagnostics. Allocator failures and exhausted tree identities remain standard C++ exceptions, rather than validation diagnostics. There are no global node/action registries or implicit filesystem/resource loading operations.

## Common representation

Define the runtime IR before committing to an authoring language. Every frontend must produce the same backend-neutral model:

```text
UiDocument
  format version
  root: UiNode
  style / asset references

UiNode
  type
  optional author id
  classes[]
  typed properties
  style references / local style
  event binding identifiers
  children[]
```

The minimum document is implemented as one rooted, ordered tree. Sibling order is meaningful; property-map iteration must not determine behavior. Validation diagnoses unknown node types, invalid property types, invalid references, and duplicate author IDs. Style/asset references in the broader diagram remain proposed.

Runtime node handles, serialized author IDs, and reconciliation keys serve different purposes. Handles identify live instances; author IDs support references and diagnostics; keys preserve identity across component updates. The implemented handle/author-ID rules are above; owner-level key semantics are in [keyed instance reconciliation](#implemented-keyed-instance-reconciliation), and the remaining rules are proposed under [state and reconciliation](#state-and-reconciliation).

## Primitives and components

Candidate primitive vocabulary: `Box`, `Text`, `Image`, `Button`, `ScrollView`, `Spacer`, `Stack`, `Grid`, and `Canvas`, with a controlled custom-node extension point. `Container` is a category; use `Box` as the initial concrete container name. `Panel` and `List` can begin as composed components rather than additional storage categories. Scrolling is a Box's resolved [`overflow: scroll`](layout.md#implemented-prototype-algorithm); a `ScrollView` primitive or component lowers to it.

A user component maps `props + local state` to a subtree. Examples include `HealthBar`, `InventorySlot`, `Toolbar`, and `PropertyEditor`. A component provides composition, an owner scope, and an identity boundary over the [reactive kernel](reactive-runtime.md#proposed-kernel-boundary); reactive values can also belong to explicit scopes outside a component. Composition is preferred to widget inheritance. Primitive storage kinds and component authoring names need not have a one-to-one correspondence.

The candidate list is vocabulary, not a requirement to implement every primitive. Tool consumers such as data grids, tree tables, timelines, command palettes, and node editors are compositions of these primitives with [virtualization](layout.md#proposed-virtualization-foundation) and [commands](commands.md), not dedicated runtime widgets.

Proposed component descriptions should express typed props, declared signals/actions, named slots/children, local state, bindings, and composed subtrees. Keep drawing primitives, interactive controls, and application composites distinguishable without making them separate runtimes. C++ builders and later textual/visual frontends should have equivalent meaning for the same supported vocabulary; source mapping survives lowering under [inspection](inspection.md#proposed-inspection-records-and-source-mapping). These descriptions extend the common IR only after identity, validation, and lifetime rules are defined.

## Properties and validation

Separate semantic node properties, [style properties](styling.md), and host-resolved event bindings. Use a small explicit type vocabulary with named validation rules; avoid opaque reflection or silently coercing arbitrary values.

Event bindings identify host-registered actions rather than embedding function pointers or executable scripts in the document. Unknown actions must produce diagnostics under a declared policy. Registration and dispatch details are owned by [input](input.md).

Diagnostics should carry a stable code, message, document/node/property location, and source span when available. Recoverable warnings must be distinguishable from errors that prevent instantiation. Validation should complete before replacing a live document.

[Inspection](inspection.md#proposed-diagnostics-and-comparisons) adapts validation into a common tooling envelope while preserving original codes and locations. That proposal does not change the implemented diagnostics or JSON v1.

## Serialization

Required properties:

- Explicit format version and deterministic output for equivalent documents.
- Stable child ordering and documented canonical property formatting.
- Round trips preserve semantic data, including editor extension data under a defined policy.
- Unsupported versions fail clearly; migrations must be explicit.
- No renderer objects, GPU handles, platform input codes, or runtime callbacks in serialized data.

Documents are serialized as [JSON v1](../../formats/tessera-ui/README.md) or constructed directly as C++ values. Defaults, extension names, numeric precision, and rejection policy are recorded there. A later TypeScript-inspired DSL and optional JSX/TSX compiler lower into the IR. This does not require executing JavaScript inside the host.

## State and reconciliation

Applications retain ownership of game/application state. Tessera supplies minimal bindings and local view state, with a view model connecting external state to UI. The [reactive runtime](reactive-runtime.md#proposed-kernel-boundary) defines mutable sources, cached derivations, owner scopes, batches, effects, and resource observations. Public API names are open; a `State` or `Binding` convenience must compose these semantics rather than introduce another system.

Reconciliation uses explicit stable keys scoped to one sibling collection or identity boundary. Duplicate keys fail before publication. Reordering a compatible keyed child preserves its logical instance, local state, owner, focus, and subscriptions rather than reconstructing every row. Removal or incompatible kind/identity replacement closes the old owner and creates a new lifetime; reusing a key later cannot revive disposed state or accept its late results. [Keyed instance reconciliation](#implemented-keyed-instance-reconciliation) implements these rules for reactive owners, and [typed property bindings](#implemented-typed-property-bindings) lower their values into a document candidate. Unkeyed child matching and cross-parent moves require an explicit compatibility policy before exposing them; serialized keys and component descriptions are proposals.

Collection insert/remove/move/item-update operations can target keyed instances without requiring a new collection API. Logical item identity, realized presentation instances, and visible paint records are distinct; [virtualization](layout.md#proposed-virtualization-foundation) must not store logical selection/focus in recycled render nodes. [Input](input.md#implemented-prototype-focus-dispatch) owns recovery when a logical target disappears, while [reactive ownership](reactive-runtime.md#proposed-reactive-ownership) owns subscription/effect cleanup.

Compatible-state preservation during reload uses the same identity rules; see [Path-finder integration](path-finder-integration.md).

Property bindings follow [dependency and change detection](reactive-runtime.md#proposed-dependencies-and-change-detection) and [batch scheduling](reactive-runtime.md#proposed-update-batches-and-scheduler). Dirty property/subtree updates must match full-tree results. Test state capture/restore uses only host-declared slots through [inspection](inspection.md#proposed-inspection-records-and-source-mapping), with application state ownership unchanged.

## Implemented typed property bindings

[bindings.hpp](../../include/tessera/ui/bindings.hpp) defines `BoundProperty`, `PropertyBindings`, and `BoundDocument`; [bindings.cpp](../../src/ui/bindings.cpp) owns candidate lowering. `bind<BoundProperty::text>(author_id, value)` accepts a `Signal<std::string>` or `Computed<std::string>`; `focusable` and `disabled` accept Boolean values, and `labelled_by` accepts `NodeReference` values. Mismatched types have no binding overload. Bindings store weak reactive references, hold no callbacks or owner lifetime, and are host-built data outside the serialized document. Arbitrary property names, optional removal, style/event bindings, component prop declarations, and slots follow the composition proposals.

One set contains at most `max_property_bindings` (4,096) entries in registration order. Target IDs are nonempty UTF-8 of at most 256 bytes without control characters. Registration returns the inserted index or located `invalid_binding_target`, `duplicate_binding`, or `binding_limit` diagnostics; rejection leaves the set unchanged. Each target/property pair has one writer; competing values must be composed in the graph. Author IDs address presentation nodes, while the stored reactive references retain their original instance lifetime: reusing an ID does not revive a removed instance's bindings.

`apply(runtime, authored, validation_context)` validates the input before copying it; required properties, including Text's authored placeholder, must be present. It indexes the valid copy by author ID and checks every target before reading values. Missing targets produce `binding_target_missing` at `/bindings/<index>/target`; incompatible node kinds produce `binding_node_kind` at the node's property path. The reflection descriptors define accepted node kinds and affected stages.

After target validation, apply uses [settled presentation reads](reactive-runtime.md#implemented-settled-presentation-reads): the whole graph must satisfy publication eligibility, and every bound value must belong to that runtime and a live, healthy owner. Reads neither refresh calculations nor collect dependencies. Global entry/settlement failures retain their reactive location. Individual reference/fault failures retain their reactive code, report the document property path, and include the original reactive location in the message. All individual reactive failures are reported in binding order without returning a partial candidate.

Successful reads replace only their target properties in the copy. The complete candidate then uses ordinary document validation, including UTF-8, action registration, and reference existence; any failure returns no value. Authored nodes, order, classes, events, and extension metadata are preserved. `BoundDocument::stages` is the conservative union of descriptors for properties whose stored value differs from the supplied input, counting insertion as a difference; it compares against that input, not a previous publication. These bits are observations, not permission to skip inherited style or geometry work. Full-tree style/layout/semantic/paint evaluation is the presentation reference.

Apply never flushes, publishes, delivers effects, or owns the last accepted UI. The host settles updates, builds and validates the bound candidate and its presentation stages, retains the prior presentation on rejection, and calls `publish()` only after accepting the complete candidate. A faulted binding rejects the candidate; subtree fallback mapping is explicit host composition. [Binding checks](../../tests/ui/bindings_tests.cpp) compare both construction paths and verify type, schema, entry, effect, and lifetime boundaries. The [tool panel](../../examples/tool-panel/main.cpp) binds each keyed row's derived label while keeping its name prop and pinned local state in the row owner.

## Implemented keyed instance reconciliation

[keyed.hpp](../../include/tessera/ui/keyed.hpp) defines `KeyedChild`, `KeyedInstance`, `KeyedUpdate`, and `KeyedCollection`; [keyed.cpp](../../src/ui/keyed.cpp) owns matching. A collection maps one sibling collection to [reactive owners](reactive-runtime.md#implemented-in-process-graph-contract) created beneath a host-supplied scope owner. It never owns or disposes that scope, and one scope serves one collection. The collection is movable and noncopyable; it uses no host, renderer, or `UiTree` types, and its keys are neither document fields nor implicit author IDs.

`reconcile(runtime, children)` takes the complete candidate in presentation order. A child names a `key` and a `kind`; both are nonempty valid UTF-8 of at most 256 bytes without control characters, so a key can also serve as a reactive owner name and author ID. Keys are unique within the candidate, which holds at most `max_keyed_children` (4,096) children. Validation reports every `invalid_key`, `invalid_kind`, and `duplicate_key` at `/children/<index>/key` or `/kind`, and `keyed_child_limit` at `/children`; a rejected candidate changes nothing and allocates no lifetime.

A previous instance with an equal key and kind is reused with its owner, lifetime, local values, bindings, effects, and cleanup registrations; reordering creates and disposes nothing. A key absent from the candidate removes its instance, and a key whose kind differs replaces it. Replacement and new keys create an owner named by the key beneath the scope, so a replacement reuses the disposed name under a new lifetime. Lifetimes are allocated in candidate order, start at one, and are never reused within a collection. Disposal follows the previous instance order and uses ordinary owner disposal, so cleanup is queued for the host's explicit [cleanup delivery](reactive-runtime.md#implemented-external-cleanup-boundary) and disposed values fail later reads.

A successful `KeyedUpdate` returns the instances in candidate order, the ascending indices of instances it created, and removed or replaced instances in previous order. The host registers local state, prop sources, bindings, and cleanup in each created owner. `instances()` and `find(key)` return the current record; `current(key, lifetime)` tells the host whether a result addressed to a lifetime still names the live instance, so a late result for a removed or replaced instance is dropped even after its key returns.

Owner creation and disposal follow the runtime's thread, calculation, effect-delivery, foreign-owner, budget, and disposed-owner rules, and their failures propagate as `ReactiveError`. New keys are created before any disposal, and a failure while creating them disposes the owners already created by that call and leaves the collection unchanged. A later failure, which requires a disposed scope or a foreign runtime, leaves the collection without the instances it disposed. The record therefore never retains a disposed owner or omits an owner it created. Because new keys are created first, they count against the live owner budget before removed instances release theirs.

The collection does not build nodes or move focus. The host lowers the instances into an ordinary full-tree document, typically using keys as author IDs so that [focus recovery](input.md#implemented-prototype-focus-dispatch) follows the logical row. Conditional children are a collection whose candidate omits a child or changes its kind. [Typed property bindings](#implemented-typed-property-bindings) project the instances' reactive values into that candidate; component descriptions, serialized keys, unkeyed matching, and cross-parent moves follow the proposals above. [Keyed checks](../../tests/ui/keyed_tests.cpp) and the [tool panel](../../examples/tool-panel/main.cpp) exercise these rules.

## Proposed async state

Real-time UI shares one asynchronous state vocabulary instead of exposing backend futures or threads to components: `idle`, `loading`, `ready`, `failed`, and `cancelled`, optionally qualified by progress, retryable, and stale. Image, font, and other resource loading; shader compilation; remote data; AI responses; filesystem scans; asset import; and thumbnail generation report through it. The host or an optional library owns execution and delivers transitions only at [update points](architecture.md#update-and-snapshot-rules); a component observes the settled state of its generation. Removing the owning component cancels or detaches the work, and a late result for a removed or replaced owner is dropped with a diagnostic rather than applied. [Replay](replay.md) records transitions as declared readiness inputs, never wall-clock completion.

### Async transition and ownership rules

Each async observation identifies its owner, a host-issued request ID, and an attempt sequence. The owner is a component instance identity (including its reconciliation lifetime) or an explicit host scope; an author ID alone cannot retain ownership through replacement. A new attempt cancels/detaches the preceding attempt and increments the sequence. Deliveries name both request and attempt, so completion of an old attempt or removed owner is discarded with a located diagnostic.

Allowed transitions are `idle -> loading`, `loading -> ready/failed/cancelled`, and a terminal state to `loading` only as a new attempt. A synchronous cache hit may publish `idle -> ready` in one update when the host explicitly declares it. Within a loading attempt, progress is absent or finite in [0, 1] and non-decreasing; it is not inferred from elapsed time. `retryable` qualifies a failure and does not start retries. `stale` means a previously accepted ready value is retained while a replacement loads or fails; its original request/revision stays observable. Failure/cancellation never silently installs a partial value.

The host queues owned transitions and publishes them in explicit logical order at update points. The runtime validates owner/request/attempt, transition, and payload schema before settling a candidate generation; snapshots being inspected or rendered stay immutable. Published resources retain the [render completion lifetime](rendering.md#implemented-frame-contract) even if the logical owner cancels. Cancellation is a logical terminal state and need not claim that underlying work stopped; a late result is discarded. Errors in the host operation use this state/result boundary, while subtree construction errors use error boundaries.

[Replay](replay.md) readiness fixtures must declare request IDs, resource identities/digests, attempted transitions, and their update sequence; a test adapter supplies the resulting values, without filesystem/network timing. [Inspection](inspection.md#proposed-inspection-records-and-source-mapping) exposes only declared state slots and owned observations. [Commands](commands.md#proposed-transaction-boundary) own edit transaction tokens; an async request never commits application data merely by becoming ready.

Open decisions: concrete observation/payload types and bounds, host subscription delivery/backpressure, and whether a consumer needs stale values across more than one replacement. First implementation fixtures: immediate-ready cache, retry after failure with an old completion arriving later, cancellation/removal/reload with reused author IDs, progress regression rejection, preserved stale value, and resource replacement while an earlier GPU submission owns the old bytes.

## Implemented controlled host-state boundary

The [Replay host update boundary](replay.md#implemented-controlled-host-updates), defined in [replay.hpp](../../include/tessera/replay/replay.hpp), implements the async ownership rules for declared boolean fixture slots and host-built immutable replacement views. Loading attempts use strictly increasing request numbers; only the current attempt can complete, and view replacement cancels other pending owners before publication. Failure/cancellation retain accepted values, and rejected candidate views retain the previous generation. It has no component subscription, progress payload, provider execution, or application mutation. General component async observations follow the proposal above.

## Proposed error boundaries

Dynamic UI, live reload, and agent-generated UI must fail locally. An error boundary is a declared subtree whose validation, build, or binding failure is contained: the boundary presents a declared fallback and reports diagnostics linked to source locations under [inspection](inspection.md#proposed-inspection-records-and-source-mapping), while the rest of the tree keeps its identity, focus, and state. At document level the same rule is the [reload transaction](path-finder-integration.md#live-reload-transaction): a new document is validated and built as a candidate generation, swapped in only on success, and otherwise rejected while the previous valid generation stays live. Boundaries never catch host action failures; those return through the [command](commands.md) result contract. Reactive calculation failures reach a subtree through the [reactive fault boundary](reactive-runtime.md#implemented-subtree-fault-boundary), which contains and locates them but leaves fallback presentation to this boundary.

## Proposed context

A subtree can receive shared services through an explicit context: theme, locale, asset provider, [command registry](commands.md), selection model, undo stack, font environment, or diagnostics sink. A context value has a declared type, an owner that outlives every consumer, and a scope limited to the subtree that provides it; consumers resolve the nearest provider and diagnose a missing one. Context is not a global service locator, does not let components reach host internals, and does not move application state into the runtime. Changing a provided value invalidates its consumers under the ordinary update rules.

[Localization](localization.md#proposed-locale-context) defines locale inputs and message resolution. A localized message binding preserves its ID and typed arguments in the common representation; changing provider values changes presentation without replacing component keys or user editing state. Literal text and message references need distinct validated forms before a serialized schema is extended.

## Implemented property metadata

[property_metadata.hpp](../../include/tessera/ui/property_metadata.hpp) describes the authored semantic property vocabulary above without a reflection framework or editor dependency. `property_descriptors()` returns one `PropertyDescriptor` per property, ordered by name; `find_property_descriptor` performs exact, case-sensitive lookup. Descriptors have static storage duration and remain valid for the rest of the program.

| Field | Meaning |
| --- | --- |
| `name` | Stable identifier and JSON v1 member name under node `properties`; not a display label. `property_names` holds the same constants for runtime consumers. |
| `type` | `PropertyType` mirroring the `Property` alternative; `property_type` maps a value to it. No coercion. |
| `accepted` / `required` | `NodeKinds` bit sets. Other kinds reject the property as `unknown_property`; absence on a required kind is `missing_property`. |
| `absent_value` | Meaning of absence on an accepted, non-required kind, or none. It is never materialized into documents or output. |
| `stages` | `PropertyStages` bit set whose meaning is owned by [styling](styling.md#property-effects). |
| `category` | Editor grouping (`content`, `interaction`, `accessibility`); it never affects validation or defaults. |

| Name | Type | Accepted / required | Absent value | Stages | Category |
| --- | --- | --- | --- | --- | --- |
| `disabled` | boolean | Box/Text / none | `false` | input, semantics | interaction |
| `focusable` | boolean | Box/Text / none | `false` | input, semantics | interaction |
| `labelled_by` | reference | Box/Text / none | none | semantics | accessibility |
| `text` | string | Text / Text | none | layout, paint, semantics | content |

`validate` derives property name, node-kind, type, and required-property checks from these descriptors; string UTF-8, finite-number, and reference-existence checks remain value rules. JSON v1 encodes a property under its descriptor name using its `Property` alternative. `effective_property` returns the authored value or the descriptor's absent value, borrowing from the node or the static table; pointer and focus targeting read `disabled` and focus dispatch reads `focusable` through it, and layout/paint read `text` through `property_names`. Descriptors cover authored properties only; resolved style, layout, and other derived values are not listed. The current vocabulary has no numeric range or enumeration, so descriptors carry no range/enum fields.

[Property metadata checks](../../tests/ui/property_metadata_tests.cpp) validate every descriptor, node kind, and value type against validation, absence handling, and canonical encoded names, and [pointer checks](../../tests/input/pointer_tests.cpp) compare absent and explicit `disabled`.

## Proposed property reflection

Extend the implemented descriptors as the schema evolves, without a general reflection framework or editor SDK dependency. Add valid range/enum metadata when a numeric or enumerated property is introduced, and distinguish derived or read-only values if they are exposed. A property can affect multiple stages; do not force style/layout/paint/semantics into a mutually exclusive enum.

Use the same descriptors for Inspector generation and invalidation. Do not maintain a second editor-owned list of types/defaults. This proposal does not add fields to JSON v1. Path-finder consumes this metadata under its [bridge contract](path-finder-integration.md).

## Proposed overlays and portals

An overlay root provides a presentation layer for popups, tooltips, context menus, and modals outside ordinary content clipping. A portal changes presentation ancestry while retaining declared component/state ownership and stable identity. Define logical versus presentation parentage explicitly before adding it to the document schema.

The model owns mount/unmount, keyed identity, and cleanup. [Layout](layout.md#scrolling-and-overlay-geometry) owns anchor/viewport placement; [rendering](rendering.md#proposed-overlay-paint) owns presentation order/clips; [input](input.md#proposed-overlay-interaction) owns modal focus and hit eligibility. These consumers must use the same layer identity rather than inventing independent z-order schemes.

Virtualized views rely on keyed item identity and reconciliation cleanup. A realized item keeps its key across recycling and generations; selection and focus refer to item keys, so they persist while the item is unrealized, and focus recovery follows [input](input.md#implemented-prototype-focus-dispatch) when the keyed item disappears. Geometry and realization are owned by [layout](layout.md#proposed-virtualization-foundation).

## Verification

The [hello-ui example](../../examples/hello-ui/main.cpp) creates, inspects, validates, serializes, and restores one ordered tree without a renderer. [Document tests](../../tests/serialization/document_tests.cpp) verify semantic/canonical round trips, invalid properties/IDs/references/versions/actions, metadata, Unicode, bounds, and foreign/expired handles. [Keyed checks](../../tests/ui/keyed_tests.cpp) verify instance reuse across reorder, removal/replacement cleanup order, fresh lifetimes for reused keys, located candidate rejection, and runtime-failure record consistency. Configuration evidence is in the [support matrix](../reference/support-matrix.md).
