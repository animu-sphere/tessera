# Layout

## Implemented foundation contract

The public headers are [geometry.hpp](../../include/tessera/layout/geometry.hpp) and [layout_box.hpp](../../include/tessera/layout/layout_box.hpp). Geometry uses `float` logical units with the origin at the top left, positive X right, and positive Y down. `Point`, `Size`, `Rect`, and `Edges` are plain values. `inset(rect, edges)` moves each side inward; an overconstrained axis keeps its origin offset and clamps its size to zero. `intersect(a, b)` returns the overlap; a disjoint axis keeps the larger origin with zero size. Device scale belongs to the renderer's frame information, not layout.

`LayoutInput` borrows, for one pass, a `UiTree`, a span of [resolved styles](styling.md) indexed by `NodeHandle::index` (exactly one per node, in tree preorder), the available viewport size for the root margin box, a [`TextShaper`](text.md), and an optional span of requested scroll offsets: empty, or one `Point` per node by `NodeHandle::index`. Offsets are host-owned interaction state; layout keeps nothing between passes. `validate(LayoutInput)` reports a missing tree or shaper (`missing_input`), a style count that differs from the tree size (`style_count`), a non-empty offset span of another size (`scroll_count` at `/scroll_offsets`), a non-finite offset (`invalid_number` at `/scroll_offsets/<index>/x` or `/y`), a non-finite or negative viewport, and every resolved-style error located at `/styles/<index>/<field>`. An unbounded viewport is rejected until intrinsic sizing defines it.

`LayoutResult` owns its boxes in tree preorder. Nodes with `Display::none` and their subtrees are absent. Each `LayoutBox` records its node handle, its parent's index in the result (`no_layout_parent` for the root), its border box in root logical coordinates, its border and padding edges, its optional clip, and, exactly for `overflow: scroll` boxes, a `ScrollGeometry` holding the scroll `extent` and applied `offset`. Padding and content boxes are derived with `inset`, so paint and hit testing use the same geometry. A scroll box's viewport is its padding box; `scroll_limit()` is the extent less that viewport on each axis, and zero without scroll geometry. A hidden box has `visible == false`: it keeps geometry but neither paints nor receives input. Handles in a result refer to the input tree and expire with it. Margins are not recorded.

## Implemented prototype algorithm

`compute_layout(LayoutInput)` in [layout_box.hpp](../../include/tessera/layout/layout_box.hpp) recomputes the full tree. It returns the `validate` diagnostics without a value for invalid input, text measurement failures relocated under `/nodes/<index>` (the node's `NodeHandle::index`), or `non_finite_geometry` at `/nodes/<index>` when float arithmetic overflows. Every `Display::flex` node is a single-line flex container; a Text node is a leaf whose content size is its measured `TextMetrics::size`.

- **Box sizing.** `width`/`height` and min/max limits apply to the border box. A fixed dimension is clamped into its minimum/maximum range; the border box is never smaller than its border plus padding, which wins over a smaller maximum. Content boxes are derived with `inset`.
- **Root.** The root margin box starts at the viewport origin. An automatic root dimension fills the viewport less its margins; a fixed one ignores the viewport. A non-displayed root yields no boxes.
- **Content size.** An automatic dimension's basis is its content plus border and padding: along the main axis, displayed children's clamped bases plus margins plus gaps; across, the largest such child extent. Text content does not depend on available space because the placeholder shaper never wraps.
- **Main axis.** `direction` selects the main axis. Each child starts from its basis (fixed dimension or content size). When the clamped bases plus margins and gaps are below the content box, positive free space is distributed by `grow`; otherwise overflow is removed by `shrink` multiplied by the basis. Factors are relative weights: any positive total distributes all free space. A child that violates its limits is clamped and frozen, then remaining space is redistributed (the CSS freeze loop); a fixed dimension is only a basis, so grow/shrink can still change it. Defaults (`grow` 0, `shrink` 0) keep bases unchanged.
- **Spacing.** Margins add to the item's outer size and never collapse. `gap` appears only between adjacent displayed children. `justify` places the remaining positive space at `start`, `center`, or `end`, or between children for `space_between` (a single child starts).
- **Cross axis.** `align: stretch` sets an automatic cross dimension to the content box less margins, clamped to the child's limits; a fixed cross dimension does not stretch. `start`, `center`, and `end` place the clamped basis within that space.
- **Overflow.** Alignment is safe: negative remaining space is treated as zero on both axes, so overflowing children start at the leading content edge and extend past the trailing edge. The parent keeps its size; only a scroll box records an extent.
- **Clipping.** `overflow: clip` and `overflow: scroll` restrict a box's descendants, not its own background or border, to its padding box. `LayoutBox::clip` is the intersection of every clipping ancestor's padding box in root logical coordinates, absent when no ancestor clips; disjoint ancestors yield a zero-size clip. Clips are rectangular: corner radius does not cut them. Recording them does not move any box. Paint and hit testing restrict each box to this same rectangle, so overflow that is not painted is not hit. A non-finite clip reports `non_finite_geometry`.
- **Scrolling.** A scroll box clips like `overflow: clip` and is sized like any other box, so it scrolls only when its size is constrained. Its extent covers its padding box and, measured from the padding box origin at zero offset, each displayed child's margin box plus the scroll box's own trailing padding on that axis. Overflow of grandchildren beyond a child's border box does not extend it. The node's requested offset, or zero without offsets, is clamped per axis into `[0, scroll_limit()]` and recorded; every descendant is placed moved back by it, so recorded border boxes and clips already include all ancestor offsets. The scroll box and its padding-box clip do not move. Nested scroll boxes compose their offsets and intersect their viewports. Offsets for nodes that do not scroll are ignored. A non-finite extent or offset reports `non_finite_geometry`.
- **Display and visibility.** `Display::none` children and their subtrees take no space and add no gap. `Visibility::hidden` boxes keep geometry and gaps and report `visible == false`. Each box reports its own resolved visibility; the [prototype style resolver](styling.md#implemented-prototype-style-resolution) does not inherit it.
- **Numerics.** Geometry uses `float` logical units without rounding or pixel snapping. Results are exact for dyadic fixture values and deterministic for identical input on one toolchain; proportional shares such as thirds are checked within 1e-4 logical units. Transform composition and snapping remain unspecified.

## Boundary

```text
node tree + resolved style + available size + text metrics
-> layout calculation -> LayoutBox tree -> paint generation
```

Layout consumes resolved values, not selector grammar or backend objects. A proposed `LayoutBox` contains node identity, bounds, content bounds, and information needed to derive clipping/scrolling. Distinguish border, padding, and content areas so paint and hit testing use the same geometry.

Transform composition within layout, pixel snapping, rounding, and numerical tolerances must be specified before algorithms become public contracts.

## Coordinate spaces

Layout and normalized pointer positions use the logical coordinate convention defined by the foundation contract. Physical framebuffer pixels, device scale, clip/viewport coordinates, and host world-space coordinates are separate spaces. Physical conversion and backend snapping are owned by [rendering](rendering.md#coordinate-conversion); a world-space adapter maps host coordinates without importing world/engine types into layout.

Scale/viewport changes must be supplied coherently at an update boundary. Specify transform/inverse hit-test mapping with shared geometry before adding transformed UI; draw-list transforms alone do not change layout or pointer behavior.

## Extension order

| Order | Capability | Intended behavior |
| --- | --- | --- |
| 1 | Fixed dimensions | Resolve explicit width/height with min/max constraints |
| 2 | Stack | Ordered row or column children with margin, padding, and gap |
| 3 | Flex-like | Distribute available space with explicit alignment rules |
| 4 | Scrolling/clipping | Separate viewport from content extent and scroll offset |
| 5 | Wrapping | Width-constrained text and declared line breaking |
| 6 | Absolute/anchored positioning | Position against a declared containing box or anchor |
| 7 | Richer intrinsic sizing | Content-driven sizing with explicit constraints |
| 8 | Grid | Explicit rows/columns before advanced placement |
| 9 | Virtualization support | Realized keyed items, estimated extents, stable scrolling |

The prototype contract above defines orders 1–4. Extension order is a dependency guide; milestone scope lives in [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md). Text measurement always goes through `TextShaper`.

## Scrolling and overlay geometry

The [prototype scrolling rule](#implemented-prototype-algorithm) makes viewport bounds, content extent, clamped offset, and effective clips available to both paint and hit testing, including nested scroll boxes; [input](input.md#implemented-scroll-routing) turns wheel deltas and reveal requests into new requested offsets. The remainder of this section is proposed. VirtualList follows ordinary scrolling and stable keyed reconciliation; realization/estimated-size changes must preserve a declared scroll anchor and expose enough geometry for navigation. A proposed anchor is an item key plus a local offset within that item, so prepending items or correcting estimated heights does not move visible content. A follow-end policy keeps the trailing edge visible as content grows, ends when the user scrolls away, and resumes only on request. Conversation, log, and terminal views motivate these rules under [conversational workspaces](conversational-ui.md#proposed-list-virtualization-requirements).

Overlay placement resolves an anchor in logical coordinates against a declared viewport, with bounded placement/fallback rules. Presentation ancestry can differ from component ownership under the [portal model](ui-model.md#proposed-overlays-and-portals). Layout supplies shared geometry; input and paint must not independently recompute popup positions.

## Proposed rules to settle

Settled by the resolved-style contract: the only units are `automatic` and logical `points`; non-finite and negative sizes, edges, and gaps are rejected; a minimum above its maximum is rejected rather than resolved by precedence; maximums may be unbounded. Sizing, flex distribution, spacing, overflow, scroll extents and limits, and display/visibility are settled by the prototype rules above; margins stay unrecorded because neither scrolling nor hit testing needs them. The prototype is flex-like, not browser flexbox conformance: there is no wrapping, `align-self`, explicit flex basis, order property, or baseline alignment.

- Specify width-constrained text measurement when wrapping arrives with real line breaking.
- Decide whether grandchild overflow should extend a scroll extent, and whether a consumer needs scroll snapping or overscroll.

These rules are implementation decisions to record with examples and algorithm tests. Percent sizing, advanced grid, and browser-specific formatting behavior are deferred until needed.

Responsive application layout should consume an explicit logical viewport or containing-box size. Breakpoint/rule syntax and general constraint/flow solvers are later consumer-driven candidates, following flex, scrolling, absolute positioning, and explicit grid. They must remain backend-neutral and diagnose conflicting or unsatisfiable rules. Validate selected viewport/scale fixtures through [inspection tooling](inspection.md), without claiming browser layout compatibility.

## Invalidation

Initially recompute the full layout tree. Later classify changes as geometry-affecting or paint-only. Size changes can invalidate ancestors and siblings, so dirty-subtree optimization must preserve the result of a full calculation. Text measurement and resolved font changes participate in layout invalidation.

## Verification

[Layout checks](../../tests/layout/layout_tests.cpp) compare numeric boxes for fixed sizes, row/column stacks, nested padding, margins/gaps, justify/align, grow/shrink with limit freezing, overflow, overflow clips, scroll extents with clamped and nested offsets, empty containers, fractional sizes, display/visibility, repeated-run equality, and failures. Later add absolute, grid, and intrinsic cases as each algorithm lands. Use no screenshot dependency for geometry correctness.
