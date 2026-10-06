# Semantics and external adapters

## Proposed semantic projection

`SemanticTree` is a backend-neutral projection of the settled UI snapshot, separate from paint commands and geometry. It represents meaning for accessibility, automated tests, external agents, and inspection. This section defines a proposal, not JSON v1 fields or a stable public API; the [implemented prototype](#implemented-prototype-projection) covers a small in-process subset. Actual capability status is in [support](../reference/support-matrix.md).

Each semantic entry needs:

- Identity associated with a live node, plus an explicitly scoped stable author identity for replay/inspection when available.
- Role, accessible label/name, optional value, and relationships.
- Enabled, focused, selected, and other role-specific states.
- Supported logical actions, with targets governed by the [input action boundary](input.md#event-to-action-boundary).
- Ordered semantic children; exact flattening of non-semantic containers is an open decision.

Derive enabled/focused/action availability from settled interaction state rather than maintaining competing state in adapters. Names may use explicit labels or relationships; define missing-reference, cycle, and fallback diagnostics before freezing schema v1. Existing `labelled_by` storage remains governed by [UI model](ui-model.md) and [JSON v1](../../formats/tessera-ui/README.md).

Semantic inclusion for hidden, display-none, virtualized, and modal content needs explicit fixtures. Do not infer eligibility solely from whether a paint command exists.

## Implemented prototype projection

[semantics.hpp](../../include/tessera/semantics/semantics.hpp) implements an in-process subset of this proposal. It is a prototype, not a serialized semantic schema or stable API, and it does not extend JSON v1. Roles are derived from the existing vocabulary because no authored role property exists.

- **Input.** `SemanticInput` borrows the same coherent tree/resolved-style/layout snapshot as [hit testing](input.md#implemented-rectangular-hit-testing) and is rejected with the same located snapshot codes. An optional `focused` handle carries host focus for that snapshot, such as `FocusDispatchResult::focused`; a handle from another tree fails as `stale_target` at `/focused`. No text shaper or renderer is involved.
- **Inclusion and order.** `build_semantic_tree` scans displayed nodes in tree preorder. Display-none subtrees have no entries. Visibility is local: a hidden node is excluded, while its visible descendants attach to the nearest included ancestor. Roles are `button` for a Box or Text with an `activate` binding, `text` for a Text node outside a button, and `root` for a Box root with neither. Other Boxes are flattened. Text whose nearest included ancestor is a button is presentational: it names that button and has no entry. A nested button keeps its own entry and label. A hidden root yields top-level entries without a parent.
- **Entries.** `SemanticNode` owns the author ID and records the live `NodeHandle`, parent entry index, `LayoutResult` box index for bounds, role, name and `NameSource`, the `labelled_by` target handle, `enabled` (false when the node or an ancestor is disabled), `focusable` and `focused` state, and eligible actions. Disabled entries remain exposed without actions. There is no selected or value state, and no value-bearing role exists. Handles expire with their tree; entries are not a serialized identity.
- **Focus state.** `focusable` is [focus dispatch](input.md#implemented-prototype-focus-dispatch) eligibility, computed by the same shared rule: because entries are displayed and visible, an entry is focusable when it is enabled and its `focusable` property is true. `focused` is true only on the focusable entry whose node is the supplied focus. Supplied focus on a disabled, hidden, or display-none node is reported on no entry, as [style resolution](styling.md#implemented-prototype-style-resolution) suppresses `:focus` on disabled nodes; hosts refresh focus dispatch after snapshot changes so that the supplied focus is eligible. Projection never moves or recovers focus. An eligible focused node without an entry, such as a focusable presentational label or a flattened Box, produces a successful result with a `focus_not_exposed` warning at `/nodes/<preorder index>`, and no entry reports focus.
- **Names.** A `labelled_by` reference names the entry with the Text content of the referenced subtree in preorder, including hidden or display-none content. References are not followed transitively, so relationship cycles cannot recurse. An empty relationship result falls back to content: a button's visible presentational Text runs, or a Text entry's own text. Nonempty runs are joined by one space without other whitespace normalization. Document validation already rejects missing references. A button without a name produces a successful result with a `missing_name` warning at `/nodes/<preorder index>`.
- **Invocation.** `request_semantic_action(input, target, binding)` re-projects the supplied snapshot and returns the same `ActionRequest` as a [pointer activation](input.md#implemented-pointer-dispatch) of that binding owner. A handle from another or destroyed tree fails as `stale_target` at `/target`, even when the replacement reuses author IDs. Display-none or hidden targets are `hidden_target` and disabled entries are `disabled_target`, both at `/target`. Bindings other than an exposed `activate`, labels, and flattened containers are `unsupported_action` at `/binding`. No host callback runs and no state changes.

Prototype [inspection](inspection.md#implemented-prototype-capture-and-target-resolution) resolves role/name targets against this projection and invokes them here. [Replay](replay.md#implemented-prototype-playback) records owned copies of this projection per generation and focus-changing step, and plays semantic action steps through the same invocation. Generation-aware identities for external adapters, selection/value state, and platform/DOM adapters remain proposals.

## Snapshot and identity

Publish a coherent semantic snapshot with the same update generation as style/layout and actions under the [frame lifecycle](architecture.md#frame-scheduling). Borrowed runtime handles expire with their tree. External adapters need a generation-aware identity and must reject stale action targets after replacement. Do not serialize process-unique handles into recordings.

Semantic output is deterministic for the same document, state, input, time, and resource readiness. Geometry may be supplied for adapter needs through layout references; semantics does not own layout calculation.

## Accessibility and automation boundary

Platform adapters translate this projection to native services such as Windows UI Automation or macOS accessibility. The [Web host](web-host.md#proposed-semantic-dom-bridge) projects it to semantic DOM alongside canvas rendering. OS/DOM types stay outside core, and all adapters share the same semantic schema.

An external automation adapter may find a role/name, inspect state, and invoke a declared semantic action. Invocation passes through the same eligibility and host action path as normal interaction; adapters do not call component callbacks or host functions directly. A semantic schema does not imply native screen-reader integration, a bundled AI runtime, or supported automation transport.

Path-finder and external tools consume this projection through [shared inspection](inspection.md), alongside [reflection](ui-model.md#proposed-property-reflection), without defining a second semantic schema. Inspection owns target resolution and source/geometry joins; it does not redefine accessible names or actions. Adapter transport, authorization policy, and platform-specific lifetime mapping are integration decisions.

## Verification

Check deterministic order, roles/names/relationships, state consistency, supported-action eligibility, hidden/modal content, and stale identity rejection. [Semantic checks](../../tests/semantics/semantics_tests.cpp) cover prototype roles, flattening, hidden/display-none inclusion, relationship/content names with fallback and warnings, disabled state, repeated-projection equality, and pointer-equivalent invocation with stale/hidden/disabled/unsupported rejection. They also check that `focusable`/`focused` agree with focus dispatch traversal and recovery and with a resolved `:focus` rule, including disabled and hidden focus, the `focus_not_exposed` warning, and stale focus rejection; modal content still needs fixtures. Native accessibility and external automation each require their own adapter evidence. Use semantic and expected-action fixtures with [Replay](replay.md), and record results in support.
