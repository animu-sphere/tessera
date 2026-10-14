# Async resources

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed async state

General resource observations extend the [typed view boundary](../architecture/ui-model.md#implemented-typed-async-view-observations) without exposing backend futures or threads to components. Image, font, and other resource loading; shader compilation; remote data; AI responses; filesystem scans; asset import; and thumbnail generation share its state vocabulary. The host or an optional library owns execution and delivers transitions only at [update points](../architecture/overview.md#update-and-snapshot-rules); a component observes the settled state of its generation. Removing the owning component cancels or detaches the work, and a late result for a removed or replaced owner is dropped with a diagnostic rather than applied. [Replay](../architecture/replay.md) records transitions as declared readiness inputs, never wall-clock completion.

### Async transition and ownership rules

Resource observations preserve the typed view boundary's request/attempt, transition, monotonic progress, failure/cancellation and stale-value rules. Their owner is a component instance identity (including its reconciliation lifetime) or an explicit host scope; an author ID alone cannot retain ownership through replacement.

An adapter supplies a bounded payload schema and owned accepted resources instead of primitive `Property` values. Progress is declared by execution rather than inferred from elapsed time. Failure/cancellation never silently installs a partial resource.

The host queues owned transitions and publishes them in explicit logical order at update points. The runtime validates owner/request/attempt, transition, and payload schema before settling a candidate generation; snapshots being inspected or rendered stay immutable. Published resources retain the [render completion lifetime](../architecture/rendering.md#implemented-frame-contract) even if the logical owner cancels. Cancellation is a logical terminal state and need not claim that underlying work stopped; a late result is discarded. Errors in the host operation use this state/result boundary, while subtree construction errors use error boundaries.

[Replay](../architecture/replay.md) readiness fixtures must declare request IDs, resource identities/digests, attempted transitions, and their update sequence; a test adapter supplies the resulting values, without filesystem/network timing. [Inspection](../architecture/inspection.md#proposed-inspection-records-and-source-mapping) exposes only declared state slots and owned observations. [Commands](../architecture/commands.md#proposed-transaction-boundary) own edit transaction tokens; an async request never commits application data merely by becoming ready.

Open decisions: resource payload types and bounds, host subscription delivery/backpressure, and whether a consumer needs more than one retained ready revision. Resource fixtures cover cancellation/removal/reload with reused author IDs, bounded payload rejection, and replacement while an earlier GPU submission owns the old bytes, preserving the typed view transition checks.

## Promotion criteria

Promote an owned resource payload or delivery mechanism only when the active fixture requires a value that the typed observation and host adapters cannot express. Name the payload bounds, owner transition and retirement check; do not add a provider framework merely for symmetry.
