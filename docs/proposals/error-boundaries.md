# UI error boundaries

> Proposed design, not yet implemented. Active scope is in [current work](../roadmap/current.md).

## Proposed error boundaries

Dynamic UI, live reload, and agent-generated UI must fail locally. An error boundary is a declared subtree whose validation, build, or binding failure is contained: the boundary presents a declared fallback and reports diagnostics linked to source locations under [inspection](../architecture/inspection.md#proposed-inspection-records-and-source-mapping), while the rest of the tree keeps its identity, focus, and state. At document level the same rule is the [reload transaction](live-reload.md#live-reload-transaction): a new document is validated and built as a candidate generation, swapped in only on success, and otherwise rejected while the previous valid generation stays live. Boundaries never catch host action failures; those return through the [command](../architecture/commands.md) result contract. Reactive calculation failures reach a subtree through the [reactive fault boundary](../architecture/reactive-runtime.md#implemented-subtree-fault-boundary), which contains and locates them but leaves fallback presentation to this boundary.

## Promotion criteria

Promote a declared UI fallback only when the active fixture must keep an unaffected subtree usable after a located build/binding failure. Whole-candidate rejection is sufficient when it satisfies that fixture.
