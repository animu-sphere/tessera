# Layout

## Core invariants

- Layout consumes nodes, resolved styles, available size and injected text metrics without renderer or application state access.
- Logical geometry and clip/scroll observations are shared by paint, hit testing and semantic bounds; physical conversion belongs to rendering/host boundaries.
- Full-tree calculation defines correctness for any incremental or virtualized path.

## Implemented foundation contract

The public headers are [geometry.hpp](../../include/tessera/layout/geometry.hpp) and [layout_box.hpp](../../include/tessera/layout/layout_box.hpp). Geometry uses `float` logical units with the origin at the top left, positive X right, and positive Y down. `Point`, `Size`, `Rect`, and `Edges` are plain values. `inset(rect, edges)` moves each side inward; an overconstrained axis keeps its origin offset and clamps its size to zero. `intersect(a, b)` returns the overlap; a disjoint axis keeps the larger origin with zero size. Device scale belongs to the renderer's frame information, not layout.

`LayoutInput` borrows, for one pass, a `UiTree`, a span of [resolved styles](styling.md) indexed by `NodeHandle::index` (exactly one per node, in tree preorder), the available viewport size for the root margin box, a [`TextShaper`](text.md), and an optional span of requested scroll offsets: empty, or one `Point` per node by `NodeHandle::index`. Offsets are host-owned interaction state; layout keeps nothing between passes. `validate(LayoutInput)` reports a missing tree or shaper (`missing_input`), a style count that differs from the tree size (`style_count`), a non-empty offset span of another size (`scroll_count` at `/scroll_offsets`), a non-finite offset (`invalid_number` at `/scroll_offsets/<index>/x` or `/y`), a non-finite or negative viewport, and every resolved-style error located at `/styles/<index>/<field>`. An unbounded viewport is rejected until intrinsic sizing defines it.

`LayoutResult` owns its boxes in tree preorder. Nodes with `Display::none` and their subtrees are absent. Each `LayoutBox` records its node handle, its parent's index in the result (`no_layout_parent` for the root), its border box in root logical coordinates, its border and padding edges, its optional clip, and, exactly for `overflow: scroll` boxes, a `ScrollGeometry` holding the scroll `extent` and applied `offset`. Padding and content boxes are derived with `inset`, so paint and hit testing use the same geometry. A scroll box's viewport is its padding box; `scroll_limit()` is the extent less that viewport on each axis, and zero without scroll geometry. A hidden box has `visible == false`: it keeps geometry but neither paints nor receives input. Handles in a result refer to the input tree and expire with it. Margins are not recorded.

## Implemented prototype algorithm

`compute_layout(LayoutInput)` in [layout_box.hpp](../../include/tessera/layout/layout_box.hpp) recomputes the full tree. It returns the `validate` diagnostics without a value for invalid input, text measurement failures relocated under `/nodes/<index>` (the node's `NodeHandle::index`), or `non_finite_geometry` at `/nodes/<index>` when float arithmetic overflows. Every `Display::flex` node is a flex container, single-line unless a row declares [wrapping](#implemented-flex-wrapping); a Text node is a leaf whose content size is its measured `TextMetrics::size`.

- **Box sizing.** `width`/`height` and min/max limits apply to the border box. A fixed dimension is clamped into its minimum/maximum range; the border box is never smaller than its border plus padding, which wins over a smaller maximum. Content boxes are derived with `inset`.
- **Root.** The root margin box starts at the viewport origin. An automatic root dimension fills the viewport less its margins; a fixed one ignores the viewport. A non-displayed root yields no boxes.
- **Content size.** An automatic dimension's basis is its content plus border and padding: along the main axis, displayed children's clamped bases plus margins plus gaps; across, the largest such child extent. Natural Text width is measured without constraints. Text height is measured again with the content width after width limits, border, and padding. For a column, determine each child's width (stretch or clamped natural/fixed basis) before computing its height basis; for a row, resolve flex widths before computing automatic cross heights. Recurse through ordinary boxes so wrapped descendant heights affect automatic parent heights, sibling placement, and scroll extents. Fixed heights retain their basis; min/max, flexing, and cross stretch can still make a box shorter or taller than its text. Non-stretched automatic widths retain their natural basis and may overflow a narrow parent. The root still fills its viewport on automatic axes.
- **Main axis.** `direction` selects the main axis. Each child starts from its basis (fixed dimension or content size). When the clamped bases plus margins and gaps are below the content box, positive free space is distributed by `grow`; otherwise overflow is removed by `shrink` multiplied by the basis. Factors are relative weights: any positive total distributes all free space. A child that violates its limits is clamped and frozen, then remaining space is redistributed (the CSS freeze loop); a fixed dimension is only a basis, so grow/shrink can still change it. Defaults (`grow` 0, `shrink` 0) keep bases unchanged.
- **Spacing.** Margins add to the item's outer size and never collapse. `gap` appears only between adjacent displayed children. `justify` places the remaining positive space at `start`, `center`, or `end`, or between children for `space_between` (a single child starts).
- **Cross axis.** `align: stretch` sets an automatic cross dimension to the content box less margins, clamped to the child's limits; a fixed cross dimension does not stretch. `start`, `center`, and `end` place the clamped basis within that space.
- **Overflow.** Alignment is safe: negative remaining space is treated as zero on both axes, so overflowing children start at the leading content edge and extend past the trailing edge. The parent keeps its size; only a scroll box records an extent.
- **Clipping.** `overflow: clip` and `overflow: scroll` restrict a box's descendants, not its own background or border, to its padding box. `LayoutBox::clip` is the intersection of every clipping ancestor's padding box in root logical coordinates, absent when no ancestor clips; disjoint ancestors yield a zero-size clip. Clips are rectangular: corner radius does not cut them. Recording them does not move any box. Paint and hit testing restrict each box to this same rectangle, so overflow that is not painted is not hit. A non-finite clip reports `non_finite_geometry`.
- **Scrolling.** A scroll box clips like `overflow: clip` and is sized like any other box, so it scrolls only when its size is constrained. Its extent covers its padding box and, measured from the padding box origin at zero offset, each displayed child's margin box plus the scroll box's own trailing padding on that axis. Overflow of grandchildren beyond a child's border box does not extend it. The node's requested offset, or zero without offsets, is clamped per axis into `[0, scroll_limit()]` and recorded; every descendant is placed moved back by it, so recorded border boxes and clips already include all ancestor offsets. The scroll box and its padding-box clip do not move. Nested scroll boxes compose their offsets and intersect their viewports. Offsets for nodes that do not scroll are ignored. A non-finite extent or offset reports `non_finite_geometry`.
- **Display and visibility.** `Display::none` children and their subtrees take no space and add no gap. `Visibility::hidden` boxes keep geometry and gaps and report `visible == false`. Each box reports its own resolved visibility; the [prototype style resolver](styling.md#implemented-prototype-style-resolution) does not inherit it.
- **Numerics.** Geometry uses `float` logical units without rounding or pixel snapping. Results are exact for dyadic fixture values and deterministic for identical input on one toolchain; proportional shares such as thirds are checked within 1e-4 logical units. Transform composition and snapping remain unspecified.

## Implemented Flex wrapping

`flex_wrap: wrap` on a `Display::flex` row breaks its flow children into lines, top to bottom; the default `no_wrap` keeps the single-line rules above. Column containers and Stack/Grid parents ignore it. Column or reverse wrapping and selectable line packing (`align-content`) are not provided.

- **Line breaking.** Children are taken in tree order. A child's outer size is its clamped width basis plus horizontal margins; it starts a new line when, after `gap`, it would pass the content width. A line always holds at least one child, so an oversized child takes its own line and overflows safely from the leading edge. Absolute, non-displayed and root-layer children take no line space; hidden children keep theirs.
- **Within a line.** Each line applies the main-axis rules independently: grow/shrink distribute only that line's free space or overflow, and `justify` places that line's remaining space.
- **Lines.** A line's cross size is its largest child margin-box height, measured at the widths flexed within that line. Lines stack from the content top, separated by the same `gap` used between children. When the content box is taller than the lines and gaps, the positive remainder is shared equally among the lines. `align` then places each child within its line, and `stretch` fills the line's cross size less margins, clamped to limits. Overflowing lines are never shrunk.
- **Intrinsic size.** The natural width stays the single-line sum, so a row on a non-stretched automatic width does not wrap. Automatic height at an assigned width is the sum of line cross sizes plus gaps between lines, so wrapped rows size their ancestors, Grid rows, later siblings and scroll extents.

The [Flex wrap checks](../../tests/layout/flex_wrap_tests.cpp) cover breaking with gaps and margins, per-line justify/grow/alignment/stretch, shared cross space, oversized children, natural width, Grid row heights, scroll extent/offset, display/visibility, ignored column/Stack wrapping, CPU pixels/hits and rejection. The [Inspector Visibility choices](../guides/inspector-hierarchy.md#visibility-choice) share one line in a wide pane and wrap in the narrow control column.

## Implemented Stack placement

`Display::stack` overlaps children within one parent **content box**. The public [`authoring::Stack`](../guides/stack.md) lowers to an ordinary Box with that display declaration. It does not relocate presentation ancestry, escape ancestor clipping or create a modal/focus scope; layers reorder with [`z_index`](#implemented-stacking-order).

- **Child sizing and placement.** Each child uses its own `stack_horizontal` and `stack_vertical` (`Align`, both default `stretch`). On an automatic axis, stretch fills the available content size less that child's margins, clamped to its min/max and border/padding floor. A fixed axis keeps its clamped fixed size. Non-stretched automatic width uses the natural basis; automatic height is remeasured at the final assigned width, including wrapped descendants. Start/stretch place at the leading margin edge; center/end use half/all positive space remaining after both margins and the border-box size. Overflow is safe: negative remaining space becomes zero, just as in Flex.
- **Intrinsic size.** An automatic Stack basis is the maximum displayed child margin-box extent on each axis, plus the Stack's own border and padding, not the sum of children. Height is recomputed using each child's assigned width. The root still fills the viewport on automatic axes. Nested Stack/Flex containers retain the same sizing rules.
- **Ignored Flex controls.** A Stack parent's `direction`, `justify`, `align` and `gap`, and its children's `grow`/`shrink`, do not affect overlapping placement. A Stack can itself grow/shrink as a child of a Flex parent. Its children's own Flex containers still use their ordinary Flex controls.
- **Order, overflow and eligibility.** Result boxes remain in tree preorder. Under equal [`z_index`](#implemented-stacking-order), later sibling subtrees paint above earlier subtrees and hit testing selects the topmost eligible box. There is no implicit pointer pass-through for transparent/zero-opacity overlays. Existing `overflow`, scroll extent/offset, ancestor clips, local visibility, inherited disabled state and `Display::none` behavior apply unchanged. Focus traversal retains declared order; directional navigation uses the shared resolved bounds.

The [Stack checks](../../tests/layout/stack_tests.cpp) cover numeric placement/limits, intrinsic and wrapped heights, nested scroll/clip coordinates, hidden/non-displayed layers, geometry rejection and selective CPU pixel/hit agreement. The [viewport toolbar](../../examples/viewport-toolbar/main.cpp) exercises public composition, ordinary commands, normalized pointer/keyboard/gamepad input and viewport resize without a native host.

## Implemented Grid placement

`Display::grid` and public [`authoring::Grid`](../guides/grid.md) arrange ordinary children in explicit row-major cells of the parent **content box**. They introduce no node kind or separate render/input geometry.

- **Explicit shape.** `grid_columns` and `grid_rows` must each contain 1–256 `GridTrack` values. `Display::none`, [absolute](#implemented-absolute-positioning) and root-layer children are omitted before placement; hidden children retain their area and local visibility behavior. Missing axes report `grid_tracks` at `/styles/<index>/grid_columns` or `/grid_rows`. Unoccupied cells remain empty. There are no implicit tracks or explicit child cell indices.
- **Spans and auto-placement.** Each child occupies `grid_column_span` × `grid_row_span` consecutive tracks (`uint16_t`, both default 1, valid 1–256). Children are placed in tree order by sparse row-major auto-placement: each takes the first free area at or after a cursor that then advances past it, wrapping to the next row when the span does not fit the remaining columns. Holes left before a wrapped span are never backfilled. A span longer than its parent's explicit axis reports `grid_span` at the child's `/styles/<child>/grid_column_span` or `/grid_row_span`; a child that finds no free area reports `grid_capacity` at `/styles/<index>`.
- **Tracks and allocation.** A default `GridTrack{}` is content-sized (`automatic`); `GridTrack::points(n)` uses a fixed logical size; `GridTrack::fraction(weight, minimum, maximum)` uses a positive weight. Every kind supports `minimum` (0) and `maximum` (+infinity). Fixed and automatic tracks clamp their fixed size or largest single-track child margin-box contribution into those limits. Spanning children then contribute, fewest tracks first and in tree order within a span count: any excess of their margin-box contribution over the spanned tracks plus internal gaps is added in equal shares to the content-sized tracks they cross, freezing tracks at their maxima and redistributing the remainder; excess with no growable track is dropped and overflows. A span crossing a fractional track on an assigned axis contributes nothing, leaving that track's share of free space to cover it. On a constrained axis, fractional tracks start at their minima and share positive remaining space by weight after fixed/automatic tracks and gaps, freezing capped tracks and redistributing their unused shares. Minima never shrink, so undersized containers overflow at the trailing edge. Capped tracks can leave trailing space. This is not CSS Grid's fractional-track sizing algorithm.
- **Intrinsic and wrapped sizes.** Without an assigned axis size, automatic and fractional tracks both use the content contributions above, including span excess, clamped to their limits; fixed tracks retain their size. Grid's automatic width is the sum of those columns, column gaps, border and padding. Determine assigned columns and child widths before measuring row contributions, including nested Flex/Stack/Grid and wrapped text; a spanning child is measured at its spanned column width. Automatic Grid height is the sum of those rows, row gaps, border and padding. Automatic rows keep these measured heights when the Grid has extra height; fractional rows use their declared minima/weights on a constrained height and may be shorter than their content. As elsewhere, automatic root dimensions fill the viewport.
- **Cell alignment.** Each child uses `grid_horizontal`/`grid_vertical` (`Align`, both default `stretch`) within its cell area: the spanned tracks plus the gaps between them. Automatic stretch fills that area less margins, clamped to child min/max and border/padding floors; fixed axes keep their clamped size. Non-stretched width uses the natural basis and height is measured at the assigned width. Start/stretch use the leading margin edge; center/end use half/all positive remaining space after both margins and child size. Overflow never shifts the child behind the leading edge. Tracks do not automatically grow to fit a child's minimum or clip individual cells.
- **Other controls and coordinates.** `column_gap`/`row_gap` appear between explicit tracks, including empty cells. Grid's own `direction`, `justify`, `align`, `gap`, and its children's `grow`/`shrink` do not distribute cells. Grid can still grow/shrink as a Flex child; each child's own container controls remain ordinary. Results stay in tree preorder, with existing paint/hit ordering, focus eligibility and ancestor clip/scroll rules. Scroll extents retain the ordinary direct-child margin-box rule above, not a separate track rectangle.
- **Validation.** Track arrays over 256 report `grid_track_bound`; invalid kinds, nonfinite/negative values and conflicting min/max use the [style rules](styling.md#implemented-resolved-style-contract) at `/grid_columns/<i>` or `/grid_rows/<i>`. Zero fractional weight is `out_of_range` at `/value`. Gaps and alignment use ordinary numeric/enum validation. Geometry overflow rejects the entire layout candidate.

The [Grid owner checks](../../tests/layout/grid_tests.cpp) cover allocation, caps/minima, alignment, intrinsic/wrapped row sizing, row/column spans, sparse placement, span contributions and caps, display/visibility, scroll/clip coordinates, selective CPU pixels/hits and rejection. The [Inspector](../guides/inspector-hierarchy.md#scale-hint-and-modified-badge) spans a contextual Scale hint across its property columns. The [Grid Inspector example](../../examples/grid-inspector/main.cpp) verifies shared columns, narrow input bounds, controlled editing, normalized pointer/keyboard/gamepad routing, resize retention and a rejected measurement without a native host.

## Implemented absolute positioning

A child with `position: absolute` leaves its parent's Flex, Stack or Grid flow and is placed against that parent's **padding box**; there is no positioned-ancestor lookup. It contributes nothing to the parent's intrinsic size, gaps, Flex distribution or Grid cells and capacity. `position` defaults to `flow`. The root and [root layers](#implemented-anchored-popups) ignore it.

- **Offsets.** `left`, `top`, `right` and `bottom` are optional finite logical offsets inward from the matching padding-box edge; empty is automatic and negative values extend past the edge. Margins add to the offsets. Offsets on a flow child have no effect.
- **Size.** On an automatic axis with both offsets, the border box fills the space between them less margins, clamped to its min/max and border/padding floor. Otherwise a fixed dimension keeps its clamped size; automatic width is the natural width bounded by the room left after declared offsets and margins, then clamped; automatic height is measured at the assigned width, including wrapped descendants.
- **Placement.** A leading offset places the leading margin edge and wins over a trailing offset when both are declared with a fixed size. A trailing offset alone places the trailing margin edge. With neither, the box starts at the leading padding edge plus its margin. Parent alignment, justification and Grid/Stack child alignment do not apply.
- **Order, clipping and scrolling.** Result boxes stay in tree preorder. Under equal [`z_index`](#implemented-stacking-order), an absolute child paints above earlier siblings and below later ones, and hit testing follows. The parent's ordinary clip, visibility, inherited disabled state and `Display::none` apply. A scroll parent's extent includes absolute children by the same direct-child margin-box rule, and they move with its offset. Focus traversal keeps declared order.
- **Validation.** Non-finite offsets report `invalid_number` at `/styles/<index>/left` (or `top`, `right`, `bottom`); an unknown position reports `unknown_value` at `/position`. Geometry overflow rejects the candidate with `non_finite_geometry`.

The [absolute owner checks](../../tests/layout/absolute_tests.cpp) cover offsets past the edge, filled/natural/limited sizes, leading precedence, flow/intrinsic/Grid exclusion, nested parents, scroll extent/offset, clip inheritance, CPU pixel/hit order, display/visibility and rejection. The [Inspector](../guides/inspector-hierarchy.md#scale-hint-and-modified-badge) straddles its property card's top edge with a Modified badge without moving the card's content. Positioned-ancestor containing boxes, anchoring to arbitrary nodes and percentage offsets remain deferred.

## Implemented stacking order

`z_index` (`int32_t`, default 0) orders a box among its displayed siblings for paint and pointer input. It applies to flow and [absolute](#implemented-absolute-positioning) children of Flex, Stack and Grid parents alike and changes no geometry.

- **Paint order.** Paint walks tree preorder, except that each box's displayed children are stably ordered by ascending `z_index`; equal values keep tree order. Each child paints with its whole subtree before the next sibling, so every box scopes its descendants' values: a descendant's `z_index` never lifts it above a later sibling of an ancestor, and a lowered child still paints after its parent's own decoration. Negative values paint below siblings at zero.
- **Root layers.** [Anchored popups](#implemented-anchored-popups) and the [Modal](#implemented-modal-placement) ignore `z_index` and keep following every ordinary root child in declared order, so raised content never covers an open menu, list or dialog. Ordinary root children still order among themselves.
- **Shared consumers.** [Hit testing](input.md#implemented-rectangular-hit-testing) visits the reverse of the same order, and [inspection point targets](inspection.md) select the topmost painted element by it. Clips, local visibility, inherited disabled state, `Display::none`, scroll extents and offsets are unchanged: a raised box stays inside its ancestors' clips.
- **Declared order elsewhere.** `LayoutResult` stays in tree preorder. Focus traversal, directional tie-breaking, semantics, inspection element order and replay comparisons keep declared order; only paint and pointer targeting use the stacking order.

The [stacking owner checks](../../tests/layout/z_index_tests.cpp) cover raised and lowered siblings, ties across the full integer range, unchanged geometry, subtree scoping, ordering within a parent, clips, hidden and non-displayed layers, root layers and inspection point targets, all through CPU pixels and hit tests. The [Inspector](../guides/inspector-hierarchy.md#selection-elevation) raises the selected Hierarchy row's shadow above its neighbor's hover fill and the focused row above both.

## Implemented Modal placement

A Box with `modal: true` is the single last root child, excluded from Flex/Stack/Grid flow, intrinsic contributions, cell capacity and scroll extents like a root Popup. Its border box is the intersection of viewport and root content, independent of its own dimensions/margins and background scroll offsets. Root ancestry, preorder, padding and clipping remain ordinary; no presentation relocation occurs. Closed composition has `display: none`.

The [Modal composition](ui-model.md#implemented-c-form-authoring) uses Stack placement. Only for a Modal's direct children, automatic axes additionally fit available content less margins, subject to child minima/maxima and border/padding floors. Height is measured at the assigned width before bounding. Explicit sizes and impossible minima can still overflow and are clipped by the Modal. Dialog's ordinary scrollable panel therefore wraps and scrolls at narrow sizes instead of growing beyond the viewport; its offsets and focus reveal remain host-owned.

The [owner fixture](../../tests/input/modal_tests.cpp) checks flow exclusion, padded viewport coordinates, resize/wrapping/scrolling, CPU pixels and hit order. [Input](input.md#implemented-modal-interaction) owns scope and dismissal; [usage](../guides/modal-dialog.md) owns composition.

## Boundary

```text
node tree + resolved style + available size + text metrics
-> layout calculation -> LayoutBox tree -> paint generation
```

Layout consumes resolved values, not selector grammar or backend objects. `LayoutBox` distinguishes border, padding, and content areas and records clipping/scrolling, so paint and hit testing use the same geometry.

Transform composition within layout, pixel snapping, rounding, and numerical tolerances must be specified before algorithms become public contracts.

## Coordinate spaces

Layout and normalized pointer positions use the logical coordinate convention defined by the foundation contract. Physical framebuffer pixels, device scale, clip/viewport coordinates, and host world-space coordinates are separate spaces. Physical conversion and backend snapping are owned by [rendering](rendering.md#coordinate-conversion); a world-space adapter maps host coordinates without importing world/engine types into layout.

Scale/viewport changes must be supplied coherently at an update boundary. Specify transform/inverse hit-test mapping with shared geometry before adding transformed UI; draw-list transforms alone do not change layout or pointer behavior.

## Extension order

| Order | Capability | Intended behavior |
| --- | --- | --- |
| 1 | Fixed dimensions | Resolve explicit width/height with min/max constraints |
| 2 | Row/Column and Stack | Ordered flow with margin/padding/gap; in-parent overlapping children under the implemented Stack contract |
| 3 | Flex-like | Distribute available space with explicit alignment rules |
| 4 | Scrolling/clipping | Separate viewport from content extent and scroll offset |
| 5 | Wrapping | Implemented width-constrained text and row Flex wrapping under the contracts above; column/reverse wrapping and line packing choices remain deferred |
| 6 | Absolute/anchored positioning | Implemented parent padding-box absolute placement, sibling `z_index` stacking and anchored popups under the contracts above; positioned-ancestor containing boxes remain deferred |
| 7 | Richer intrinsic sizing | Content-driven sizing with explicit constraints |
| 8 | Grid | Implemented explicit tracks with row/column spans and sparse row-major auto-placement under the Grid contract above; explicit cell indices, implicit tracks and advanced placement remain deferred |
| 9 | [Virtualization](../proposals/virtualization.md#proposed-virtualization-foundation) | Realized keyed items, estimated extents, stable scrolling |

Extension order is a dependency guide; milestone scope lives in [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md). Text measurement always goes through `TextShaper`.

Concrete application/widget requirements choose each extension. Strengthen Row/Column, min/max and intrinsic sizing, overflow, scrolling, split panes, stack/overlay placement and text-driven sizing only as those operations require. Further positioning, column or reverse flex wrapping, grid placement and richer constraints need their own consumer evidence; this sequence does not schedule a complete CSS layout model.

## Scrolling and overlay geometry

The [prototype scrolling rule](#implemented-prototype-algorithm) makes viewport bounds, content extent, clamped offset, and effective clips available to both paint and hit testing, including nested scroll boxes; [input](input.md#implemented-scroll-routing) turns wheel deltas and reveal requests into new requested offsets.

### Implemented anchored popups

A Box declaring `popup_anchor` is a root child after ordinary content under the [model restrictions](ui-model.md#implemented-foundation-contract). Ordinary layout excludes these children from intrinsic measurement, flow, Grid capacity and scroll extents. After ordinary content, layout places each popup subtree in declared order, retaining the root as its parent. There is no alternate presentation ancestry or layer sorting. Paint, hit tests, semantic bounds and inspection consume the same ordinary preorder boxes.

Placement uses the anchor's current border box, including ancestor scroll offsets. The placement boundary is the intersection of the logical viewport and the root content box. Width is the lesser of the ordinary preferred width and boundary width, clamped by the popup's own limits. With `popup_match_width` on a bottom/top side and no point placement, the preferred width is first raised to the anchor's border-box width; the boundary and limits still apply. Assigned-width measurement determines height, bounded by boundary height and clamped by ordinary limits. Border/padding/minimum floors can still exceed the boundary; explicit clip/scroll policies govern oversized descendants. Popup margins and flex/Stack/Grid child placement controls do not offset the anchored border box.

The preferred `popup_side` starts at the anchor's leading cross edge, separated by `popup_gap` on the main axis. If that side cannot hold the popup and the opposite side has strictly more room, use the opposite side; ties retain the preference. Shift both axes into the boundary, starting at its leading edge when oversized. Geometry uses double intermediates for placement arithmetic, then the existing finite-geometry checks.

An unmounted, display-none, locally hidden, zero-area or fully ancestor-clipped anchor suppresses the entire popup subtree. Missing anchors are intentional lifecycle state, not an unresolved strict document reference. Recalculation on scroll, resize or tree replacement never reuses obsolete anchor coordinates. The popup inherits clips from its actual root ancestry, not the anchor's scrolling ancestors. Root clipping is not bypassed. [Owner checks](../../tests/input/popup_tests.cpp) cover placement, flip/shift, viewport bounds, scroll suppression, CPU pixel/hit agreement and validation.

Paired `popup_x`/`popup_y` select explicit root-logical point placement for [ContextMenu](../guides/menu-context.md), overriding side/gap. The popup's leading corner shifts into the same boundary using the same measurement and minimum-size rules. Its anchor must still be visible; scrolling a visible anchor retains the requested point, while resize reclamps it. [Menu owner checks](../../tests/input/menu_tests.cpp) cover this shared pixel/hit geometry. Presentation reparenting remains proposed under the [portal model](../proposals/overlays-and-portals.md#proposed-overlays-and-portals).

## Proposed rules to settle

Settled by the resolved-style contract: the only units are `automatic` and logical `points`; non-finite and negative sizes, edges, and gaps are rejected; a minimum above its maximum is rejected rather than resolved by precedence; maximums may be unbounded. Sizing, flex distribution, spacing, overflow, scroll extents and limits, and display/visibility are settled by the prototype rules above; margins stay unrecorded because neither scrolling nor hit testing needs them. The prototype is flex-like, not browser flexbox conformance: row wrapping follows the [wrapping contract](#implemented-flex-wrapping), and there is no `align-self`, `align-content` choice, explicit flex basis, order property, or baseline alignment. Text line wrapping is defined separately by [text](text.md#implemented-boundary).

- Decide whether grandchild overflow should extend a scroll extent, and whether a consumer needs scroll snapping or overscroll.

These rules are implementation decisions to record with examples and algorithm tests. Percent sizing, advanced grid, and browser-specific formatting behavior are deferred until needed.

Responsive application layout should consume an explicit logical viewport or containing-box size. Breakpoint/rule syntax and general constraint/flow solvers are later consumer-driven candidates, following flex, scrolling, absolute positioning, and explicit grid. They must remain backend-neutral and diagnose conflicting or unsatisfiable rules. Validate selected viewport/scale fixtures through [inspection tooling](inspection.md), without claiming browser layout compatibility.

## Proposed logical direction and edges

Use a declared inline direction (LTR, RTL, or automatic resolution from the nearest [locale context](localization.md#proposed-locale-context)), independent of the flex `direction` field that selects row/column. An explicit subtree override wins over inherited locale direction. Text paragraph direction and mixed-direction runs are owned by [text](text.md#proposed-international-text-layout), rather than inferred from flex orientation or mirrored paint.

Resolve logical inline-start/inline-end and block-start/block-end edges and start/end alignment into physical geometry before placement. Physical left/right edges remain useful for intentionally fixed geometry; define precedence or reject conflicts when physical and logical values specify the same edge. Horizontal writing is the initial design domain; logical naming does not establish vertical-writing behavior. The resulting boxes and clips are shared by paint, hit testing, focus navigation and semantic bounds. Sequential focus and authored semantic order retain their declared identity/order; directional keyboard/gamepad movement uses resolved geometry under [input](input.md).

Locale/direction changes require remeasurement and placement where affected, including wrap heights, ancestor sizing, scroll extents and clamping. Prefer intrinsic/content sizing with min/max constraints and wrapping for translatable labels; explicit fixed sizes must have an intentional overflow policy. Semantic back/forward affordances resolve direction explicitly at component/paint preparation, while non-directional icons remain unchanged. Do not implement RTL by reflecting the final framebuffer: text, icons, interaction geometry and semantics must agree.

Verify logical-edge mapping, nested direction overrides, safe alignment, wrapped text, scroll/reveal geometry and directional navigation numerically before selective image checks under the [localization fixtures](../guides/testing.md#localization-fixtures). Property syntax, physical/logical conflict rules and automatic-direction defaults must be settled before extending authored styles.

## Proposed virtualization foundation

Virtualized geometry must match full realization within the viewport. Stable logical keys, explicit measurement/anchor policy and bounded realization cannot be replaced by recycled row indices.

Possible mechanisms: [virtualization](../proposals/virtualization.md#proposed-virtualization-foundation). This direction is not an implementation task; only the active roadmap can require a bounded part.

## Invalidation

Full-tree calculation is the correctness reference. Layout consumes resolved styles, constraints and text metrics independently of reactive graph storage and renderer objects.

Possible mechanisms: [incremental evaluation](../proposals/incremental-evaluation.md#invalidation). This direction is not an implementation task; only the active roadmap can require a bounded part.

## Verification

[Layout checks](../../tests/layout/layout_tests.cpp) verify geometry numerically: sizing and limits, stacks, spacing, alignment, grow/shrink freezing, overflow, clips, scroll extents and offsets, display/visibility, fractional values, repeated-run equality, and rejected input. Each new algorithm adds its own numeric cases, and virtualized geometry is compared with full realization. Geometry correctness has no screenshot dependency.
