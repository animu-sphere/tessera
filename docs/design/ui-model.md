# UI model

Status: Draft design. Fields and snippets are conceptual.

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

The proposed minimum document is one rooted, ordered tree. Sibling order is meaningful; property-map iteration must not determine behavior. Nodes cannot have multiple parents or cycles. Validation should diagnose unknown node types, invalid property types, invalid references, and duplicate author IDs.

Runtime node handles, serialized author IDs, and reconciliation keys serve different purposes. Handles identify live instances; author IDs support references and diagnostics; keys preserve identity across component updates. Their exact types, scopes, and reuse rules must be settled during foundation work.

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

Begin with a simple JSON-compatible or TOML-style document and a C++ builder API. The choice of encoding, defaults, extension names, numeric precision, and version compatibility policy remains open. A later TypeScript-inspired DSL and optional JSX/TSX compiler lower into the IR. This does not require executing JavaScript inside the host.

## State and reconciliation

Applications retain ownership of game/application state. Tessera supplies minimal bindings and local component state, with a view model connecting external state to UI. `Signal` / `Computed` / `Effect` and `State` / `Binding` are alternative candidate APIs, not two required systems.

Proposed update semantics: state writes schedule a defined update, rendering observes settled values, and subscriptions end with their owning component. Conditional children and dynamic lists require identity rules before reconciliation is implemented. Use stable sibling keys; reject or diagnose duplicate keys. Specify mount/update/unmount cleanup and focus/interaction recovery when nodes disappear.

Keyed reconciliation is planned for a later component milestone. Compatible-state preservation during reload uses the same identity rules; see [Path-finder integration](path-finder-integration.md).

## Foundation evidence

Create a small tree, inspect its ordered structure, validate it, serialize it, and restore equivalent semantic data without a renderer. Cover malformed properties, duplicate IDs, invalid references, and unknown format versions.
