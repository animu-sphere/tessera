# Overlays and portals

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed overlays and portals

An overlay root provides a presentation layer for popups, tooltips, context menus, and modals outside ordinary content clipping. A portal changes presentation ancestry while retaining declared component/state ownership and stable identity. Define logical versus presentation parentage explicitly before adding it to the document schema.

`OverlayRoot` and `Portal` are the shared primitives for composed Tooltip, Dropdown, and Modal behavior. Presentation relocation does not transfer the portal child's local state, subscriptions, source origin, or command ownership to the overlay root. The [SemanticTree](../architecture/semantics.md) must express the declared logical relationships rather than deriving meaning solely from the relocated paint hierarchy; concrete relationship encoding is a schema decision.

The model owns mount/unmount, keyed identity, and cleanup. [Layout](../architecture/layout.md#scrolling-and-overlay-geometry) owns anchor/viewport placement; [rendering](../architecture/rendering.md#proposed-overlay-paint) owns presentation order/clips; [input](../architecture/input.md#proposed-overlay-interaction) owns modal focus and hit eligibility. These consumers must use the same layer identity rather than inventing independent z-order schemes.

## Promotion criteria

Promote a portal or overlay behavior only when a fixture requires content outside ordinary clipping or a bounded modal interaction that existing composition cannot express. Name placement, hit eligibility and focus recovery outcomes before adding a primitive catalog.
