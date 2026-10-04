# Layout

Status: Draft design. Layout must be verifiable without a GPU.

## Boundary

```text
node tree + resolved style + available size + text metrics
-> layout calculation -> LayoutBox tree -> paint generation
```

Layout consumes resolved values, not selector grammar or backend objects. A proposed `LayoutBox` contains node identity, bounds, content bounds, and information needed to derive clipping/scrolling. Distinguish border, padding, and content areas so paint and hit testing use the same geometry.

Proposed coordinate convention: floating-point logical units, origin at the top left, positive X right and positive Y down. Device scale conversion belongs at the rendering/host boundary. Coordinate spaces, transform composition, pixel snapping, rounding, and numerical tolerances must be specified before algorithms become public contracts.

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

- Define how fixed, content-sized, and available-space dimensions interact. Choose supported units explicitly; do not imply support for all CSS units.
- Define constraint precedence when minimum exceeds maximum and reject invalid/non-finite sizes. Specify overflow behavior for undersized parents.
- State which flex features exist, including grow/shrink, basis, wrapping, and alignment. A flex-like label is not browser flexbox conformance.
- Keep child order deterministic. Define gap at sibling boundaries and handling of margins; CSS margin collapsing is outside initial scope.
- Specify whether hidden nodes retain geometry and how collapsed/non-displayed nodes leave layout; coordinate with [styling](styling.md).
- Preserve scroll limits and clipping in geometry available to both [rendering](rendering.md) and [input](input.md).

These rules are implementation decisions to record with examples and algorithm tests. Percent sizing, advanced grid, and browser-specific formatting behavior are deferred until needed.

## Invalidation

Initially recompute the full layout tree. Later classify changes as geometry-affecting or paint-only. Size changes can invalidate ancestors and siblings, so dirty-subtree optimization must preserve the result of a full calculation. Text measurement and resolved font changes participate in layout invalidation.

## Verification

Compare numeric boxes for fixed, row/column, nested padding, margins/gaps, constraint limits, and overflow fixtures. Later add absolute, scrolling, grid, and intrinsic cases as each algorithm lands. Test empty containers and fractional sizes. Record logical-unit tolerances; use no screenshot dependency for geometry correctness.
