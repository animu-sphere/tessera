# Animation system

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed animation

After baseline UI is correct, animate resolved opacity, transforms, color, size, and scroll offset through an injected host `AnimationClock`. Replay supplies that clock's values; no hidden wall clock or CSS animation compatibility is required. Geometry changes invalidate layout; paint-only changes need not. Interruption, easing, authored-versus-animated precedence, and scroll interaction are open decisions. Complex game animation remains host-owned.

Opacity/transform and other paint-only changes may avoid layout, but compositor-only execution needs a concrete retained-render contract and evidence. Color can require repaint/upload; clip/transform changes may also affect input or semantic bounds. Do not classify all visual animations as GPU-only or skip those consumers.

## Promotion criteria

Promote only a property transition required by a concrete interaction fixture. Declare time, interruption and affected outputs; generalized easing, retained composition and a broad animation API need separate consumer evidence.
