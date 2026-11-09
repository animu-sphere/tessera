# Overlays and portals

> Generalized portals and stacked modal layers remain proposed. Bounded root-sibling [Popup/Tooltip](../guides/popup-tooltip.md), single-level [Menu/ContextMenu](../guides/menu-context.md) and single [Modal/Dialog](../guides/modal-dialog.md) operations are implemented without presentation reparenting. Active scope is in [current work](../roadmap/current.md).

The implemented [Stack layout](../architecture/layout.md#implemented-stack-placement) is an in-parent overlay, not the presentation relocation proposed here. It keeps ordinary ancestry, clips and focus rules. The separate [anchored popup contract](../architecture/layout.md#implemented-anchored-popups) places explicitly composed root children outside the anchor's scroll clipping, retaining ordinary root ancestry and preorder. The [Modal contract](../architecture/input.md#implemented-modal-interaction) adds one last root sibling with shared scope/dismissal/recovery for settings-reset confirmation. None promotes OverlayRoot, Portal or nested/stacked modal lifetimes.

## Proposed overlays and portals

An overlay root provides a presentation layer for popups, tooltips, context menus, and modals outside ordinary content clipping. A portal changes presentation ancestry while retaining declared component/state ownership and stable identity. Define logical versus presentation parentage explicitly before adding it to the document schema.

`OverlayRoot` and `Portal` are candidate shared abstractions for composed Tooltip, Dropdown, ContextMenu, Popup, ComboBox and Modal behavior. The delivered Menu/ContextMenu and ComboBox operations reuse bounded root-sibling popup placement; they do not require Portal. Extract a shared relocation abstraction only from demonstrated widget needs, such as a future nested overlay. The inventory does not justify a generalized portal system. Presentation relocation does not transfer the portal child's local state, subscriptions, source origin, or command ownership to the overlay root. The [SemanticTree](../architecture/semantics.md) must express the declared logical relationships rather than deriving meaning solely from the relocated paint hierarchy; concrete relationship encoding is a schema decision.

The model owns mount/unmount, keyed identity, and cleanup. [Layout](../architecture/layout.md#scrolling-and-overlay-geometry) owns anchor/viewport placement; [rendering](../architecture/rendering.md#proposed-overlay-paint) owns presentation order/clips; [input](../architecture/input.md#proposed-overlay-interaction) owns modal focus and hit eligibility. These consumers must use the same layer identity rather than inventing independent z-order schemes.

## Promotion criteria

Promote a portal or overlay behavior only when a fixture requires content outside ordinary clipping or a bounded modal interaction that existing composition cannot express. Name placement, hit eligibility and focus recovery outcomes before adding a primitive catalog.
