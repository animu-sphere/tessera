# Automatic component lifecycle

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed automatic component lifecycle

Automatic lifecycle extends [invocation mounting](../architecture/ui-model.md#implemented-component-invocation-mounting), [keyed mounting](../architecture/ui-model.md#implemented-keyed-component-mounting) and [initial setup](../architecture/ui-model.md#implemented-component-initial-setup) to associate a component's scoped key and type with named local-state slots, declared signals, [effects](../architecture/ui-model.md#implemented-declared-component-effects), cleanup, async observations, source origin, fault boundary, and reload-compatibility metadata. Lowering uses the ordinary component/document representation; automatic setup composes [reactive owners](../architecture/reactive-runtime.md#implemented-in-process-graph-contract), keyed mounting, and [publication](../architecture/reactive-runtime.md#implemented-effect-and-publication-boundary). Public authoring syntax and concrete output-signal registration types are open decisions; async registration uses [declared observations](../architecture/ui-model.md#implemented-declared-component-async-observations).

Local-state and signal declarations need stable names and exact types so compatible updates retain accepted values without depending on declaration order. State compatibility is explicit by component identity/type and slot schema; an incompatible slot or subtree follows the declared reset policy under [reload](live-reload.md#live-reload-transaction). Component-local view state does not take ownership of application data or history.

Compatible keyed reorder retains the owner and its registered state, effects, and observations. Removal or incompatible replacement closes the lifetime, disposes children deterministically, invalidates queued work, and queues once-only external cleanup for host delivery after traversal. Component-bound declarations build on [owner-bound async detachment](../architecture/ui-model.md#implemented-owner-bound-async-detachment) to register host cancellation/detachment with that lifetime; a late completion must match the live owner/request/attempt and cannot update a recreated key. Submitted assets obey [completion-safe retirement](../architecture/rendering.md#implemented-frame-contract) independently of logical disposal.

Lifecycle creation preserves definition, invocation, argument, and available declaration origins for inspection and diagnostics. Component failures project the [fault boundary](../architecture/reactive-runtime.md#implemented-subtree-fault-boundary) through a declared [UI fallback](error-boundaries.md#proposed-error-boundaries), without publishing partial content. Verify a keyed row that creates state and an observation, survives reorder, is removed, rejects late completion, and is recreated with a fresh lifetime and owned source/inspection records.

## Promotion criteria

Promote only a bounded mechanism when the active fixture cannot compose its owners/state/observations through existing mounting APIs, or a failing fixture exposes unsafe disposal or stale completion. Stop when that operation and its lifetime checks pass.
