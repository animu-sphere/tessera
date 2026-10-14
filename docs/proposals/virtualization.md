# Virtualization

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed virtualization foundation

Virtualization is one shared layer, not a VirtualList-specific implementation. VirtualList, VirtualTree, VirtualGrid, DataGrid, TreeTable, log views, and timelines build on it. It follows ordinary scrolling and realizes only items intersecting the viewport plus a declared overscan, exposing enough geometry for navigation and [reveal](../architecture/input.md#implemented-scroll-routing).

- **Keys and extents.** Every item has a stable key from [keyed identity](../architecture/ui-model.md#state-and-reconciliation). Unrealized items use an estimated extent; a realized item's measured extent replaces the estimate. Grids and tables apply the same rule per axis, with row and column extents kept separately.
- **Anchor.** Scroll position is restored from an anchor item key plus a local offset within that item, not a pixel offset, so prepending items, correcting estimates, or reloading does not move visible content. A follow-end policy keeps the trailing edge visible as content grows, ends when the user scrolls away, and resumes only on request.
- **Recycling and generations.** Realized components may be recycled across keys, but identity, state, selection, and focus follow keys, never recycled instances. Realization changes settle in an update generation like any other change, and observations name the generation they belong to.
- **Correctness reference.** Realizing every item is the reference: a virtualized frame must match the full frame within the viewport, as required by [architecture](../architecture/overview.md#frame-scheduling).

For a bounded VirtualList consumer, declare a deterministic measurement/estimate policy, visible-range/overscan limits, and a semantic virtualization policy: which logical items are exposed and how navigation requests realization of an unrealized item. Semantic order and focus/selection use stable item keys rather than visible indices. State retained for unrealized keys has an explicit owner and bounded lifetime; destroying a realized row detaches its async observations, and remounting cannot accept a completion from the old lifetime. Keeping logical item state does not keep obsolete row resources alive. The concrete retention and semantic schemas are open decisions.

Verify a fixture with thousands of logical items and a small bounded realized set, without treating its size as a performance guarantee. Reorder, scrolling, estimate correction, focus reveal, async row replacement, and reload must preserve declared keyed state and scroll identity and produce deterministic replay/inspection observations.

Conversation, log, and terminal views motivate the anchor and follow-end rules under [conversational workspaces](../integrations/conversational-ui.md#proposed-list-virtualization-requirements); spatial culling for 2D canvases follows the [graph editor](../integrations/graph-editor.md#proposed-visibility-culling-and-lod).

## Promotion criteria

Promote bounded realization only when the active consumer demonstrates a large logical collection that cannot proceed with ordinary scrolling. Declare measurement/overscan limits, logical focus/selection and semantic exposure; stop when that consumer works.
