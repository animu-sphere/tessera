# Live reload transactions

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Live reload transaction

Proposed sequence:

1. Parse and validate the edited source using the runtime schema, preserving source origins.
2. Lower components and resolve required references into a candidate using ordinary runtime boundaries.
3. Analyze identity/type/state-schema compatibility and prepare declared state preservation or migration.
4. Settle the candidate and calculate full-tree style/layout/semantics/paint, including focus/interaction recovery, before acceptance.
5. Atomically publish one coherent generation at the host update boundary, then render and expose inspection, invariant evaluation, and capture for that generation.

A rejected candidate leaves the last valid tree usable. Successful reload cleans up removed bindings/components and preserves only state declared compatible by identity, node/component type, and state schema. Author IDs and reconciliation keys must have explicit scopes before this can be reliable.

Application state remains host-owned throughout reload. GPU resources replaced by a document follow completion-safe retirement; see [rendering](../architecture/rendering.md). Initial atomic full-document reload can precede fine-grained patches.

Compatible reload preserves declared keyed component identity/owner and local state, focus, compatible scroll offsets or anchors, selection by stable logical/semantic identity, accepted async values when resource identity is unchanged, and command ownership where valid. Generation-bound handles and action targets must be resolved again against the accepted generation. Compatibility must be checked under each owning subsystem's rules; matching an author ID alone is insufficient. Type/state-schema changes, removed nodes, or incompatible bindings require declared subtree recreation, reset/cleanup, and recovery rather than forced preservation. Migration failure rejects the candidate.

Invalid parse, validation, lowering, or presentation candidates leave the previous valid generation visible and usable, with source-located diagnostics. Preparation must not dispose retained owners or deliver candidate effects before acceptance; rejected staging work is discarded and its resources cleaned up safely. Atomic publication is a UI-generation guarantee, not rollback of unrelated host application data. Reload reuses the ordinary generation/publication model rather than creating an editor-specific runtime. Tooling may request a snapshot after that generation renders and measure edit-to-verified-snapshot latency under [inspection](../architecture/inspection.md#proposed-diagnostics-and-comparisons).

## Promotion criteria

Promote only the reload behavior required by a source-edit fixture: atomic acceptance/rejection and the exact state/selection compatibility it exercises. General migration schemas, arbitrary patching and broader resource preservation require separate fixtures.
