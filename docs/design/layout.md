# Layout

Status: Phase 0 input/output contracts implemented. No layout algorithm is implemented. Layout must be verifiable without a GPU.

## Implemented foundation contract

The public headers are [geometry.hpp](../../include/tessera/layout/geometry.hpp) and [layout_box.hpp](../../include/tessera/layout/layout_box.hpp). Geometry uses `float` logical units with the origin at the top left, positive X right, and positive Y down. `Point`, `Size`, `Rect`, and `Edges` are plain values. `inset(rect, edges)` moves each side inward; an overconstrained axis keeps its origin offset and clamps its size to zero. Device scale belongs to the renderer's frame information, not layout.

`LayoutInput` borrows, for one pass, a `UiTree`, a span of [resolved styles](styling.md) indexed by `NodeHandle::index` (exactly one per node, in tree preorder), the available viewport size for the root margin box, and a [`TextShaper`](text.md). `validate(LayoutInput)` reports a missing tree or shaper (`missing_input`), a style count that differs from the tree size (`style_count`), a non-finite or negative viewport, and every resolved-style error located at `/styles/<index>/<field>`. An unbounded viewport is rejected until intrinsic sizing defines it.

`LayoutResult` owns its boxes in tree preorder. Nodes with `Display::none` and their subtrees are absent. Each `LayoutBox` records its node handle, its parent's index in the result (`no_layout_parent` for the root), its border box in root logical coordinates, and its border and padding edges. Padding and content boxes are derived with `inset`, so paint and hit testing use the same geometry. A hidden box has `visible == false`: it keeps geometry but neither paints nor receives input. Handles in a result refer to the input tree and expire with it. Margins, scroll extents, and clips are not yet recorded.

## Boundary

```text
node tree + resolved style + available size + text metrics
-> layout calculation -> LayoutBox tree -> paint generation
```

Layout consumes resolved values, not selector grammar or backend objects. A proposed `LayoutBox` contains node identity, bounds, content bounds, and information needed to derive clipping/scrolling. Distinguish border, padding, and content areas so paint and hit testing use the same geometry.

Transform composition within layout, pixel snapping, rounding, and numerical tolerances must be specified before algorithms become public contracts.

## Algorithm order

| Order | Capability | Intended behavior |
| --- | --- | --- |
| 1 | Fixed dimensions | Resolve explicit width/height with min/max constraints |
| 2 | Stack | Ordered row or column children with margin, padding, and gap |
| 3 | Flex-like | Distribute available space with explicit alignment rules |
| 4 | Absolute positioning | Position against a declared containing box |
| 5 | Scrolling | Separate viewport bounds from content extent and scroll offset |
| 6 | Grid | Explicit rows/columns before advanced placement |
| 7 | Intrinsic sizing | Content-driven sizing, including real text metrics |

The first layout prototype uses `Box`, placeholder `Text`, fixed sizes, stack/flex rows and columns, margin, padding, and gap. It can accept already-resolved styles before stylesheet parsing exists. Intrinsic text sizing must not be simulated by depending on a particular font library in layout.

## Proposed rules to settle

Settled by the resolved-style contract: the only units are `automatic` and logical `points`; non-finite and negative sizes, edges, and gaps are rejected; a minimum above its maximum is rejected rather than resolved by precedence; maximums may be unbounded. A fixed size is clamped into its minimum/maximum range.

- Define how fixed, content-sized, and available-space dimensions interact. Specify overflow behavior for undersized parents.
- State which flex features exist, including grow/shrink, basis, wrapping, and alignment. A flex-like label is not browser flexbox conformance.
- Keep child order deterministic. Define gap at sibling boundaries and handling of margins; CSS margin collapsing is outside initial scope.
- Specify whether hidden nodes retain geometry and how collapsed/non-displayed nodes leave layout; coordinate with [styling](styling.md).
- Preserve scroll limits and clipping in geometry available to both [rendering](rendering.md) and [input](input.md).

These rules are implementation decisions to record with examples and algorithm tests. Percent sizing, advanced grid, and browser-specific formatting behavior are deferred until needed.

## Invalidation

Initially recompute the full layout tree. Later classify changes as geometry-affecting or paint-only. Size changes can invalidate ancestors and siblings, so dirty-subtree optimization must preserve the result of a full calculation. Text measurement and resolved font changes participate in layout invalidation.

## Verification

Compare numeric boxes for fixed, row/column, nested padding, margins/gaps, constraint limits, and overflow fixtures. Later add absolute, scrolling, grid, and intrinsic cases as each algorithm lands. Test empty containers and fractional sizes. Record logical-unit tolerances; use no screenshot dependency for geometry correctness.
