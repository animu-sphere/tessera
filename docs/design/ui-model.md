# UI model

## Implemented foundation contract

The public headers are [document.hpp](../../include/tessera/ui/document.hpp), [tree.hpp](../../include/tessera/ui/tree.hpp), and [serialization.hpp](../../include/tessera/ui/serialization.hpp). They depend only on the C++20 standard library. The serialized fields and normalization/rejection rules are owned by [JSON v1](../../formats/tessera-ui/README.md).

`UiDocument` owns one `UiNode` by value. Each node owns an ordered vector of children; copying performs a deep copy. This representation cannot express cycles, shared children, or multiple parents. Only `NodeKind::box` and `NodeKind::text` exist. Text nodes are leaves. Builder data is mutable until validated; invalid candidates do not produce a runtime tree or serialized result.

`UiTree::create` validates a borrowed const document before copying its own immutable snapshot, so a rejected candidate is not consumed or recursively copied first. `UiTree` is neither copyable nor movable; its owning `unique_ptr` can move. Inspection returns borrowed const node pointers valid until the tree is destroyed. Preorder indices and a process-unique monotonically allocated tree identity form `NodeHandle`; handles from another or destroyed tree fail lookup in a later tree. Index 0 is the root, and the all-zero handle is invalid. Tree identities are never reused; identity exhaustion throws. There is no node removal/reuse in this slice. No runtime handle is serialized. A process identity counter does not provide multithreaded mutation or callback guarantees.

Author IDs are optional document-scoped names used for diagnostics, lookup, and references. Duplicate IDs fail validation. They are distinct from runtime handles; reconciliation keys are deferred. Class names are stored in authored order without stylesheet behavior.

The implemented semantic property vocabulary is deliberately small:

| Property | Nodes | Value | Current behavior |
| --- | --- | --- | --- |
| `text` | Text only, required | UTF-8 string, including empty text | Measured by layout and shaped by paint through `TextShaper`; placeholder implementation only |
| `focusable` | Box/Text | Boolean | Stored interaction intent; no focus runtime |
| `disabled` | Box/Text | Boolean | Excludes the node/subtree from pointer targeting; absent means false; see [input](input.md) |
| `labelled_by` | Box/Text | `NodeReference` to an existing author ID | Relationship storage/existence validation; no native accessibility adapter |

`Property` has explicit boolean, binary64, string, and reference alternatives. No current semantic property accepts a number; unknown properties or mismatched types fail without coercion. Absent boolean properties remain absent rather than materializing defaults. Layout/style properties and asset references are deferred; they are not arbitrary semantic properties.

`events` stores `activate`/`cancel` action names. Every bound action must exist in the caller's `ValidationContext.actions`; the default empty context rejects all bound actions. Validation snapshots the names for the call only. Pointer dispatch consumes `activate` names to return host action requests as defined in [input](input.md); logical cancel dispatch remains planned. The document stores no callback, and the host remains responsible for action implementations.

Document/node `extensions` preserve namespaced backend-neutral JSON metadata. Validation and runtime behavior do not interpret its contents. Names, strings, and metadata must be valid UTF-8; numeric metadata must be finite. Diagnostics and bounded recursion are described by the encoding page.

`Result<T>` returns an optional value and diagnostics; a failed load/save/create has no usable partial value. The current diagnostics are errors with stable codes, JSON-pointer locations, actionable messages, and source offsets when loaded. Allocator failures and exhausted tree identities remain standard C++ exceptions, rather than validation diagnostics. There are no global node/action registries or implicit filesystem/resource loading operations.

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

Runtime node handles, serialized author IDs, and reconciliation keys serve different purposes. Handles identify live instances; author IDs support references and diagnostics; keys preserve identity across component updates. The implemented handle/author-ID rules are above; key semantics still need component implementation evidence.

## Primitives and components

Candidate primitive vocabulary: `Box`, `Text`, `Image`, `Button`, `ScrollView`, `Spacer`, `Stack`, `Grid`, and `Canvas`, with a controlled custom-node extension point. `Container` is a category; use `Box` as the initial concrete container name. `Panel` and `List` can begin as composed components rather than additional storage categories.

A user component maps `props + local state` to a subtree. Examples include `HealthBar`, `InventorySlot`, `Toolbar`, and `PropertyEditor`. Composition is preferred to widget inheritance. Primitive storage kinds and component authoring names need not have a one-to-one correspondence.

The foundation should implement only the minimum node/property/document representation. A list of candidate primitives is not a requirement to implement them all in Phase 0.

## Properties and validation

Separate semantic node properties, [style properties](styling.md), and host-resolved event bindings. Use a small explicit type vocabulary with named validation rules; avoid opaque reflection or silently coercing arbitrary values.

Event bindings identify host-registered actions rather than embedding function pointers or executable scripts in the document. Unknown actions must produce diagnostics under a declared policy. Registration and dispatch details are owned by [input](input.md).

Diagnostics should carry a stable code, message, document/node/property location, and source span when available. Recoverable warnings must be distinguishable from errors that prevent instantiation. Validation should complete before replacing a live document.

## Serialization

Required properties:

- Explicit format version and deterministic output for equivalent documents.
- Stable child ordering and documented canonical property formatting.
- Round trips preserve semantic data, including editor extension data under a defined policy.
- Unsupported versions fail clearly; migrations must be explicit.
- No renderer objects, GPU handles, platform input codes, or runtime callbacks in serialized data.

The foundation now uses [JSON v1](../../formats/tessera-ui/README.md) and direct C++ value construction. Defaults, extension names, numeric precision, and rejection policy are recorded there. A later TypeScript-inspired DSL and optional JSX/TSX compiler lower into the IR. This does not require executing JavaScript inside the host.

## State and reconciliation

Applications retain ownership of game/application state. Tessera supplies minimal bindings and local component state, with a view model connecting external state to UI. `Signal` / `Computed` / `Effect` and `State` / `Binding` are alternative candidate APIs, not two required systems.

Proposed update semantics: state writes schedule a defined update, rendering observes settled values, and subscriptions end with their owning component. Conditional children and dynamic lists require identity rules before reconciliation is implemented. Use stable sibling keys; reject or diagnose duplicate keys. Specify mount/update/unmount cleanup and focus/interaction recovery when nodes disappear.

Keyed reconciliation is planned for a later component milestone. Compatible-state preservation during reload uses the same identity rules; see [Path-finder integration](path-finder-integration.md).

## Proposed property reflection

Expose explicit property metadata from the runtime schema, without a general reflection framework or editor SDK dependency. A descriptor needs a stable property/serialization name, value type, authored default or absence policy, valid range/enum, editor category, and affected stages. A property can affect multiple stages; do not force style/layout/paint/semantics into a mutually exclusive enum. Stage meaning is owned by [styling](styling.md#property-effects).

Use the same descriptors or shared definitions for validation, serialization, Inspector generation, and invalidation. Do not maintain a second editor-owned list of types/defaults. Derived or read-only values must be distinguished from authored properties. Property identifiers are independent of display labels, and descriptor lifetime must be explicit for consumers.

Basic metadata starts with the existing vocabulary; full introspection follows schema evolution. This proposal does not add fields to JSON v1. Tests must catch disagreement between descriptors, accepted values, defaults, and encoded names. Path-finder consumes this metadata under its [bridge contract](path-finder-integration.md).

## Proposed overlays and portals

An overlay root provides a presentation layer for popups, tooltips, context menus, and modals outside ordinary content clipping. A portal changes presentation ancestry while retaining declared component/state ownership and stable identity. Define logical versus presentation parentage explicitly before adding it to the document schema.

The model owns mount/unmount, keyed identity, and cleanup. [Layout](layout.md#scrolling-and-overlay-geometry) owns anchor/viewport placement; [rendering](rendering.md#proposed-overlay-paint) owns presentation order/clips; [input](input.md#proposed-overlay-interaction) owns modal focus and hit eligibility. These consumers must use the same layer identity rather than inventing independent z-order schemes.

Virtualized lists rely on keyed item identity and reconciliation cleanup, including offscreen focus/selection policy. Geometry and realization requirements are owned by [layout](layout.md#scrolling-and-overlay-geometry).

## Verification

The [hello-ui example](../../examples/hello-ui/main.cpp) creates, inspects, validates, serializes, and restores one ordered tree without a renderer. [Document tests](../../tests/serialization/document_tests.cpp) verify semantic/canonical round trips, invalid properties/IDs/references/versions/actions, metadata, Unicode, bounds, and foreign/expired handles. Configuration evidence is in the [support matrix](../reference/support-matrix.md).
