# UI model

## Implemented foundation contract

The public headers are [document.hpp](../../include/tessera/ui/document.hpp), [tree.hpp](../../include/tessera/ui/tree.hpp), and [serialization.hpp](../../include/tessera/ui/serialization.hpp). They depend only on the C++20 standard library. The serialized fields and normalization/rejection rules are owned by [JSON v1](../../formats/tessera-ui/README.md).

`UiDocument` owns one `UiNode` by value. Each node owns an ordered vector of children; copying performs a deep copy. This representation cannot express cycles, shared children, or multiple parents. Only `NodeKind::box` and `NodeKind::text` exist. Text nodes are leaves. Builder data is mutable until validated; invalid candidates do not produce a runtime tree or serialized result.

`UiTree::create` validates a borrowed const document before copying its own immutable snapshot, so a rejected candidate is not consumed or recursively copied first. `UiTree` is neither copyable nor movable; its owning `unique_ptr` can move. Inspection returns borrowed const node pointers valid until the tree is destroyed. Preorder indices and a process-unique monotonically allocated tree identity form `NodeHandle`; handles from another or destroyed tree fail lookup in a later tree. Index 0 is the root, and the all-zero handle is invalid. Tree identities are never reused; identity exhaustion throws. There is no node removal/reuse in this slice. No runtime handle is serialized. A process identity counter does not provide multithreaded mutation or callback guarantees.

Author IDs are optional document-scoped names used for diagnostics, lookup, and references. Duplicate IDs fail validation. They are distinct from runtime handles; reconciliation keys are deferred. Class names are stored in authored order; the [prototype style resolver](styling.md#implemented-prototype-style-resolution) matches them regardless of that order.

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

Runtime node handles, serialized author IDs, and reconciliation keys serve different purposes. Handles identify live instances; author IDs support references and diagnostics; keys preserve identity across component updates. The implemented handle/author-ID rules are above; key semantics are proposed under [state and reconciliation](#state-and-reconciliation).

## Primitives and components

Candidate primitive vocabulary: `Box`, `Text`, `Image`, `Button`, `ScrollView`, `Spacer`, `Stack`, `Grid`, and `Canvas`, with a controlled custom-node extension point. `Container` is a category; use `Box` as the initial concrete container name. `Panel` and `List` can begin as composed components rather than additional storage categories. Scrolling is a Box's resolved [`overflow: scroll`](layout.md#implemented-prototype-algorithm); a `ScrollView` primitive or component lowers to it.

A user component maps `props + local state` to a subtree. Examples include `HealthBar`, `InventorySlot`, `Toolbar`, and `PropertyEditor`. Composition is preferred to widget inheritance. Primitive storage kinds and component authoring names need not have a one-to-one correspondence.

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

Applications retain ownership of game/application state. Tessera supplies minimal bindings and local component state, with a view model connecting external state to UI. `Signal` / `Computed` / `Effect` and `State` / `Binding` are alternative candidate APIs, not two required systems.

Proposed update semantics: state writes schedule a defined update, rendering observes settled values, and subscriptions end with their owning component. Conditional children and dynamic lists require identity rules before reconciliation is implemented. Use stable sibling keys; reject or diagnose duplicate keys. Specify mount/update/unmount cleanup and focus/interaction recovery when nodes disappear.

Compatible-state preservation during reload uses the same identity rules; see [Path-finder integration](path-finder-integration.md).

Property-level dependency tracking is the long-term reactive direction. Define dependency ownership, cycle/reentrancy handling, settled update order, and subscription cleanup before selecting an API. Dirty property/subtree updates must match full-tree results. Test state capture/restore uses only host-declared slots through [inspection](inspection.md#proposed-inspection-records-and-source-mapping), with application state ownership unchanged.

## Proposed async state

Real-time UI shares one asynchronous state vocabulary instead of exposing backend futures or threads to components: `idle`, `loading`, `ready`, `failed`, and `cancelled`, optionally qualified by progress, retryable, and stale. Image, font, and other resource loading; shader compilation; remote data; AI responses; filesystem scans; asset import; and thumbnail generation report through it. The host or an optional library owns execution and delivers transitions only at [update points](architecture.md#update-and-snapshot-rules); a component observes the settled state of its generation. Removing the owning component cancels or detaches the work, and a late result for a removed or replaced owner is dropped with a diagnostic rather than applied. [Replay](replay.md) records transitions as declared readiness inputs, never wall-clock completion.

## Proposed error boundaries

Dynamic UI, live reload, and agent-generated UI must fail locally. An error boundary is a declared subtree whose validation, build, or binding failure is contained: the boundary presents a declared fallback and reports diagnostics linked to source locations under [inspection](inspection.md#proposed-inspection-records-and-source-mapping), while the rest of the tree keeps its identity, focus, and state. At document level the same rule is the [reload transaction](path-finder-integration.md#live-reload-transaction): a new document is validated and built as a candidate generation, swapped in only on success, and otherwise rejected while the previous valid generation stays live. Boundaries never catch host action failures; those return through the [command](commands.md) result contract.

## Proposed context

A subtree can receive shared services through an explicit context: theme, locale, asset provider, [command registry](commands.md), selection model, undo stack, font environment, or diagnostics sink. A context value has a declared type, an owner that outlives every consumer, and a scope limited to the subtree that provides it; consumers resolve the nearest provider and diagnose a missing one. Context is not a global service locator, does not let components reach host internals, and does not move application state into the runtime. Changing a provided value invalidates its consumers under the ordinary update rules.

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

The [hello-ui example](../../examples/hello-ui/main.cpp) creates, inspects, validates, serializes, and restores one ordered tree without a renderer. [Document tests](../../tests/serialization/document_tests.cpp) verify semantic/canonical round trips, invalid properties/IDs/references/versions/actions, metadata, Unicode, bounds, and foreign/expired handles. Configuration evidence is in the [support matrix](../reference/support-matrix.md).
