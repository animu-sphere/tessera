# Layout

## Implemented foundation contract

The public headers are [geometry.hpp](../../include/tessera/layout/geometry.hpp) and [layout_box.hpp](../../include/tessera/layout/layout_box.hpp). Geometry uses `float` logical units with the origin at the top left, positive X right, and positive Y down. `Point`, `Size`, `Rect`, and `Edges` are plain values. `inset(rect, edges)` moves each side inward; an overconstrained axis keeps its origin offset and clamps its size to zero. Device scale belongs to the renderer's frame information, not layout.

`LayoutInput` borrows, for one pass, a `UiTree`, a span of [resolved styles](styling.md) indexed by `NodeHandle::index` (exactly one per node, in tree preorder), the available viewport size for the root margin box, and a [`TextShaper`](text.md). `validate(LayoutInput)` reports a missing tree or shaper (`missing_input`), a style count that differs from the tree size (`style_count`), a non-finite or negative viewport, and every resolved-style error located at `/styles/<index>/<field>`. An unbounded viewport is rejected until intrinsic sizing defines it.

`LayoutResult` owns its boxes in tree preorder. Nodes with `Display::none` and their subtrees are absent. Each `LayoutBox` records its node handle, its parent's index in the result (`no_layout_parent` for the root), its border box in root logical coordinates, and its border and padding edges. Padding and content boxes are derived with `inset`, so paint and hit testing use the same geometry. A hidden box has `visible == false`: it keeps geometry but neither paints nor receives input. Handles in a result refer to the input tree and expire with it. Margins, scroll extents, and clips are not yet recorded.

## Implemented prototype algorithm

`compute_layout(LayoutInput)` in [layout_box.hpp](../../include/tessera/layout/layout_box.hpp) recomputes the full tree. It returns the `validate` diagnostics without a value for invalid input, text measurement failures relocated under `/nodes/<index>` (the node's `NodeHandle::index`), or `non_finite_geometry` at `/nodes/<index>` when float arithmetic overflows. Every `Display::flex` node is a single-line flex container; a Text node is a leaf whose content size is its measured `TextMetrics::size`.

- **Box sizing.** `width`/`height` and min/max limits apply to the border box. A fixed dimension is clamped into its minimum/maximum range; the border box is never smaller than its border plus padding, which wins over a smaller maximum. Content boxes are derived with `inset`.
- **Root.** The root margin box starts at the viewport origin. An automatic root dimension fills the viewport less its margins; a fixed one ignores the viewport. A non-displayed root yields no boxes.
- **Content size.** An automatic dimension's basis is its content plus border and padding: along the main axis, displayed children's clamped bases plus margins plus gaps; across, the largest such child extent. Text content does not depend on available space because the placeholder shaper never wraps.
- **Main axis.** `direction` selects the main axis. Each child starts from its basis (fixed dimension or content size). When the clamped bases plus margins and gaps are below the content box, positive free space is distributed by `grow`; otherwise overflow is removed by `shrink` multiplied by the basis. Factors are relative weights: any positive total distributes all free space. A child that violates its limits is clamped and frozen, then remaining space is redistributed (the CSS freeze loop); a fixed dimension is only a basis, so grow/shrink can still change it. Defaults (`grow` 0, `shrink` 0) keep bases unchanged.
- **Spacing.** Margins add to the item's outer size and never collapse. `gap` appears only between adjacent displayed children. `justify` places the remaining positive space at `start`, `center`, or `end`, or between children for `space_between` (a single child starts).
- **Cross axis.** `align: stretch` sets an automatic cross dimension to the content box less margins, clamped to the child's limits; a fixed cross dimension does not stretch. `start`, `center`, and `end` place the clamped basis within that space.
- **Overflow.** Alignment is safe: negative remaining space is treated as zero on both axes, so overflowing children start at the leading content edge and extend past the trailing edge. The parent keeps its size; no clip or scroll extent is recorded yet.
- **Display and visibility.** `Display::none` children and their subtrees take no space and add no gap. `Visibility::hidden` boxes keep geometry and gaps and report `visible == false`. Each box reports its own resolved visibility; propagating it to descendants is the style resolver's job.
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

The prototype contract above defines orders 1–3. Extension order is a dependency guide; milestone scope lives in [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md). Text measurement always goes through `TextShaper`.

## Scrolling and overlay geometry

Proposed ScrollView output must make viewport bounds, content extent, clamped offset, and effective clips available to both paint and hit testing. Define nested scrolling and coordinate conversion before adding virtualization. VirtualList follows ordinary scrolling and stable keyed reconciliation; realization/estimated-size changes must preserve a declared scroll anchor and expose enough geometry for navigation.

Overlay placement resolves an anchor in logical coordinates against a declared viewport, with bounded placement/fallback rules. Presentation ancestry can differ from component ownership under the [portal model](ui-model.md#proposed-overlays-and-portals). Layout supplies shared geometry; input and paint must not independently recompute popup positions.

## Proposed rules to settle

Settled by the resolved-style contract: the only units are `automatic` and logical `points`; non-finite and negative sizes, edges, and gaps are rejected; a minimum above its maximum is rejected rather than resolved by precedence; maximums may be unbounded. Sizing, flex distribution, spacing, overflow, and display/visibility are settled by the prototype rules above. The prototype is flex-like, not browser flexbox conformance: there is no wrapping, `align-self`, explicit flex basis, order property, or baseline alignment.

- Preserve scroll limits and clipping in geometry available to both [rendering](rendering.md) and [input](input.md).
- Decide whether recorded margins or an overflow extent are needed by scrolling and hit testing.
- Specify width-constrained text measurement when wrapping arrives with real line breaking.

These rules are implementation decisions to record with examples and algorithm tests. Percent sizing, advanced grid, and browser-specific formatting behavior are deferred until needed.

## Invalidation

Initially recompute the full layout tree. Later classify changes as geometry-affecting or paint-only. Size changes can invalidate ancestors and siblings, so dirty-subtree optimization must preserve the result of a full calculation. Text measurement and resolved font changes participate in layout invalidation.

## Verification

[Layout checks](../../tests/layout/layout_tests.cpp) compare numeric boxes for fixed sizes, row/column stacks, nested padding, margins/gaps, justify/align, grow/shrink with limit freezing, overflow, empty containers, fractional sizes, display/visibility, repeated-run equality, and failures. Later add absolute, scrolling, grid, and intrinsic cases as each algorithm lands. Use no screenshot dependency for geometry correctness.
