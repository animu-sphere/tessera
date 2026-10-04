# Semantics and external adapters

## Proposed semantic projection

`SemanticTree` is a backend-neutral projection of the settled UI snapshot, separate from paint commands and geometry. It represents meaning for accessibility, automated tests, external agents, and inspection. This page defines a proposal, not JSON v1 fields or an implemented public API. Actual capability status is in [support](../reference/support-matrix.md).

Each semantic entry needs:

- Identity associated with a live node, plus an explicitly scoped stable author identity for replay/inspection when available.
- Role, accessible label/name, optional value, and relationships.
- Enabled, focused, selected, and other role-specific states.
- Supported logical actions, with targets governed by the [input action boundary](input.md#event-to-action-boundary).
- Ordered semantic children; exact flattening of non-semantic containers is an open decision.

Derive enabled/focused/action availability from settled interaction state rather than maintaining competing state in adapters. Names may use explicit labels or relationships; define missing-reference, cycle, and fallback diagnostics before freezing schema v1. Existing `labelled_by` storage remains governed by [UI model](ui-model.md) and [JSON v1](../../formats/tessera-ui/README.md).

Semantic inclusion for hidden, display-none, virtualized, and modal content needs explicit fixtures. Do not infer eligibility solely from whether a paint command exists.

## Snapshot and identity

Publish a coherent semantic snapshot with the same update generation as style/layout and actions under the [frame lifecycle](architecture.md#frame-scheduling). Borrowed runtime handles expire with their tree. External adapters need a generation-aware identity and must reject stale action targets after replacement. Do not serialize process-unique handles into recordings.

Semantic output is deterministic for the same document, state, input, time, and resource readiness. Geometry may be supplied for adapter needs through layout references; semantics does not own layout calculation.

## Accessibility and automation boundary

Platform adapters translate this projection to native services such as Windows UI Automation or macOS accessibility. Browser-host accessibility is a separate integration. OS SDK types stay outside core.

An external automation adapter may find a role/name, inspect state, and invoke a declared semantic action. Invocation passes through the same eligibility and host action path as normal interaction; adapters do not call component callbacks or host functions directly. A semantic schema does not imply native screen-reader integration, a bundled AI runtime, or supported automation transport.

Path-finder consumes this projection for inspection alongside [reflection](ui-model.md#proposed-property-reflection), without defining a second semantic schema. Adapter transport, authorization policy, and platform-specific lifetime mapping are integration decisions.

## Verification

Check deterministic order, roles/names/relationships, state consistency, supported-action eligibility, hidden/modal content, and stale identity rejection. Native accessibility and external automation each require their own adapter evidence. Use semantic and expected-action fixtures with [Replay](replay.md), and record results in support.
