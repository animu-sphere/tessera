# Animation system

> Proposed design, not yet implemented. Active scope is in [current work](../roadmap/current.md).

## Proposed animation

Animate resolved opacity, transforms, color, size, scroll offset and [Look parameters](look.md) through an injected host `AnimationClock`. Replay supplies that clock's values; no hidden wall clock or CSS animation compatibility is required. Geometry changes invalidate layout; paint-only changes need not. Interruption, easing, authored-versus-animated precedence, and scroll interaction are open decisions. Complex game animation remains host-owned.

Opacity/transform and other paint-only changes may avoid layout, but compositor-only execution needs a concrete retained-render contract and evidence. Color can require repaint/upload; clip/transform changes may also affect input or semantic bounds. Do not classify all visual animations as GPU-only or skip those consumers.

Procedural fills and custom SDF shapes can consume explicit host time and typed uniforms without first implementing a general animation framework. Define time units/origin, evaluation and pause/resume behavior for the selected feature; expose the time, seed and parameters needed to reproduce it. Replay/host adapters need to supply those inputs explicitly rather than implying the current replay format already records them.

Changing shape coverage or fill parameters need not change layout allocation. Geometry transitions that do affect layout, input or semantic bounds update those consumers coherently. Visibility-aware redraw requests go through host scheduling rather than an unconditional animation loop.

## Promotion criteria

Select a useful interaction or visual result, such as a control transition, animated fill or morphing badge. Declare time, interruption and affected outputs for that slice. Generalized easing, retained composition and broader animation APIs can follow or accompany it where they simplify a demonstrated consumer need; they are not prerequisites for a time-driven fill.
