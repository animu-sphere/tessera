# Reactive runtime

## Proposed kernel boundary

Treat view updates as incremental evaluation of a dynamic dependency graph. Components provide composition, identity boundaries, and owner scopes above this kernel; state does not require component re-execution, a virtual-tree diff, or call-order-dependent hook slots. All frontends lower to the [common UI representation](ui-model.md#common-representation). Reactive closures, graph edges, and scheduler queues are runtime data, never serialized document fields.

The kernel owns dependency collection, value revisions, invalidation, evaluation, reactive owner disposal, and update batching. Applications own their data and expose explicit view sources; [UI model](ui-model.md#state-and-reconciliation) owns component keys and reconciliation. The host advances work at the [frame boundary](architecture.md#frame-scheduling), supplies time and external readiness, and executes external operations after traversal. The kernel imports no platform, GPU, text-library, editor, browser, or asynchronous executor types.

Conceptual primitives, with public spelling and C++ representation subject to API design:

| Concept | Responsibility |
| --- | --- |
| Mutable source (`Signal`) | Owned view value or explicit host-state observation with a change revision |
| Cached derivation (`Computed`) | Pure value calculation with dependencies collected from reactive reads |
| Owner scope | Deterministic lifetime for computations, subscriptions, pending work, and effect cleanup |
| Update batch | Coalesced invalidation and a boundary before observable effects |
| Effect | Owned external operation prepared from settled values and delivered to the host |
| Resource observation | Reactive projection of [async state](ui-model.md#proposed-async-state), using its request/attempt and cancellation rules |

Constants require no subscription. Bindings connect these concepts to typed presentation properties rather than introducing another state runtime. Resource observations do not execute file/network/font loading or create a second pending/error state machine.

## Proposed dependencies and change detection

A reactive read during a tracked calculation records a source-to-consumer edge. Reads outside a collector do not subscribe. Dynamic branches replace the consumer's dependency set after successful evaluation, removing obsolete edges; a branch that stops reading a source must stop reacting to it. Collection uses a scoped guard so failure or nested calculation restores the previous collector.

Each source and accepted derived output has a change revision. Equality is explicit per value: value comparison, identity/revision comparison, or always-changed. Comparators must be deterministic; there is no implicit deep comparison. An equal source write does not advance its revision. A derived output advances its revision only when its accepted value changes.

A write marks dependent work as potentially stale and queues it, without recursively executing calculations. Invalidation must reach every potentially affected observer, including across multiple derived nodes. Evaluation validates prerequisites before using cached results. Potential staleness is distinct from a changed output: when refreshed inputs leave a derived value equal, downstream consumers whose other inputs are unchanged can reuse their results. Queue deduplication alone cannot establish this property.

A chain and a diamond must both settle against the same source values. A diamond's sink must never publish a mixture of refreshed and stale branches. Shared prerequisites are refreshed once for a settled revision; laziness may avoid unobserved work but cannot leave an observed consumer stale. These rules hold through dynamic dependency replacement regardless of traversal order.

Synchronous direct and indirect evaluation cycles fail with a located dependency path; the kernel is not a fixed-point solver. Pure derivations cannot write reactive state, mutate the presentation tree, or perform external operations. Failed evaluation publishes no partial value or dependency set, restores bookkeeping, and reports through the [error boundary](ui-model.md#proposed-error-boundaries). Recovery must permit a later accepted update to retry without losing relevant invalidation.

## Proposed reactive ownership

The owner tree and dependency graph are different relationships. A popup's calculation can depend on a host-owned theme source while its lifetime belongs to the popup owner. A dependency does not own or prolong its source; it never transfers application state into the popup.

An owner contains child scopes, calculations, subscriptions, effects, and cancellation/cleanup registrations. Disposal closes the scope before cleanup, disposes children in a declared deterministic order, detaches edges, and removes or invalidates queued work. Cleanup runs once. Work queued for a disposed owner must not evaluate, publish, or invoke an effect. Late external completions follow the async owner/request checks rather than resolving by a reused author ID.

Runtime references identify a particular lifetime and reject stale use after disposal or storage reuse. An index/generation handle is a representation candidate, not a replacement for the implemented [tree handle](ui-model.md#implemented-foundation-contract) or a public ABI commitment. Reactive owner identity, presentation identity, sibling keys, semantic identity, and renderer resource identity need explicit mappings; they need not share one identifier. Reading a disposed source is diagnosed rather than dereferencing expired storage.

Logical disposal can request host cancellation or retirement. Already submitted resources obey [render completion](rendering.md#implemented-frame-contract); owner destruction cannot reuse GPU storage before completion. External cleanup is delivered after traversal under the same host boundary as effects.

## Proposed update batches and scheduler

Graph mutation and owner operations run on one host-designated UI thread. Background work delivers owned, ordered messages to a later host update point; it cannot mutate graph storage. Begin with a deterministic scheduler, stable queue order, duplicate suppression, and explicit flush points. Priority classes, browser microtasks, idle work, visibility skipping, and parallel evaluation require separate consumer evidence.

One normalized event and its accepted host action handling form an update batch by default. Multiple source writes coalesce before scheduled UI evaluation and effect delivery. Nested batch scopes join the outer batch; only its close schedules a flush. Source reads inside a batch see the latest write, and an explicitly demanded derived read refreshes prerequisites against those values. Such a read may calculate intermediate values, but it cannot publish a partial UI generation or deliver an effect.

An update batch is distinct from an application [edit transaction](commands.md#proposed-transaction-boundary): it neither opens an undo entry nor provides application-state rollback. A continuous edit can span several UI batches while retaining one application transaction token. A batch may wrap a non-editing action without an edit token. Closing a batch does not imply that the host mutation succeeded or that a candidate UI is valid.

The scheduler settles reactive values and presentation bindings before style, layout, semantics, and paint, then prepares one candidate generation for publication under [update and snapshot rules](architecture.md#update-and-snapshot-rules). A failed candidate retains the last valid published generation; it does not undo host state. No external effect observes a rejected candidate. Rendering consumes only published draw data and the host owns submission/presentation.

Effects have explicit delivery phases. Host/resource preparation needed for submission occurs after successful publication and before submission; post-commit notifications follow publication, without implying GPU completion. Work needing GPU completion follows the renderer completion boundary. Effect registrations own cleanup, and replacement runs prior cleanup once before the next delivery. Effect-induced writes are queued for a later batch rather than recursively entering a flush. Reentrant flush is rejected; bounded draining diagnoses feedback that cannot settle instead of looping indefinitely.

## Proposed presentation integration

Bindings invalidate consumers according to [property effects](styling.md#property-effects), using the same metadata as validation and Inspector tooling. Stage invalidation is a set, not one mutually exclusive classification. [Layout](layout.md#invalidation), [text](text.md#proposed-incremental-text-invalidation), and [render preparation](rendering.md#proposed-retained-render-updates) own their algorithms and cache keys; participating in scheduling does not merge these subsystems into the kernel.

A paint-only value change should avoid measurement and layout. Geometry, inherited state, text, and clip changes must invalidate every affected layout/input/semantic/paint consumer, including ancestors or siblings when required. Invisible or unrealized content cannot be skipped merely because it has no draw commands: dependencies, semantics, focus/navigation, and cleanup can remain observable. Fine-grained paths must match full-tree output before retained rendering or priority scheduling relies on them.

[Inspection](inspection.md#proposed-reactive-observations) owns graph snapshots and update-cause records; the kernel supplies bounded observations without exposing mutable storage. Components and future authoring facades use the same identity, ownership, and update model rather than establishing separate reconciliation or scheduling semantics.

## Open decisions

- Concrete typed source/binding API, comparator defaults, callable storage, and public lifetime/error types.
- Demand tracking and dirty-state representation, queue and dependency bounds, revision exhaustion, and deterministic sibling disposal order.
- Effect delivery/result adapter and cleanup failure policy; routing graph failures to subtree fallbacks without suppressing diagnostics.
- Allocation strategy and debug metadata storage, selected after measuring graph workloads rather than fixing an illustrative node ABI.

## Verification

Verify chains, fan-out, diamonds, equal source writes and derived outputs, multiple writes per batch, explicit reads within a batch, nested batch joining, dynamic branch edge removal, and stable execution order. Assert computation/effect counts as well as final values; unchanged outputs alone cannot prove bounded work or coherent evaluation.

Verify cycles, attempted writes during derivation, failed evaluation followed by recovery, rejected/reentrant flush, effect feedback limits, exactly-once cleanup, and disposal with queued work or late completions. No failed candidate may publish a partial generation or deliver its effects.

Use a keyed inventory/tool-panel fixture to compare full-tree and incremental properties, geometry, semantics, focus/navigation, actions, and paint under identical normalized pointer/keyboard/gamepad and replay inputs. Cover reorder, removal, branch switching, compatible reload, inherited style, and resource replacement with an earlier submission in flight. Follow [testing](../guides/testing.md); graph/scheduler tests must run core-only without GPU or host SDKs.
