# Current

Updated: 2026-10-05. Phase 0 and the Phase 1 layout prototype are implemented and verified on the recorded configuration. The v0.1.0 minimal Vulkan menu candidate is next. Delivery history is in the [changelog](../../CHANGELOG.md), with configuration evidence in the [support matrix](../reference/support-matrix.md).

## Documentation foundation

Objective: turn the implementation strategy into a compact set of canonical subsystem designs and reviewable milestones.

Delivered:

- [x] Root README and documentation index.
- [x] Design pages for architecture, UI model, layout, styling, rendering, text, input, and Path-finder integration.
- [x] Development/testing guides and dependency/support references.
- [x] Separate current work, backlog, and phase/milestone overview.
- [x] Contributor and agent guidance consistent with the strategy.

These items describe documentation delivery, not implementation or platform validation.

## Phase 0 — Runtime foundation

Delivered: **create a tree -> inspect it -> validate it -> serialize it**, plus the resolved-style, layout, input, draw-list, and text boundary contracts, without a renderer or platform SDK. Its work items and exit criteria are recorded in the [changelog](../../CHANGELOG.md) and [support matrix](../reference/support-matrix.md); contracts live in their design pages.

## Phase 1 — Layout prototype

Objective: **tree + resolved styles + viewport + text metrics -> deterministic `LayoutBox` tree**, without any renderer.

### Work

- [x] Implement full-tree `compute_layout` consuming `LayoutInput` and producing `LayoutResult`; see [layout](../design/layout.md).
- [x] Fixed Box dimensions with min/max limits and a border/padding floor; the root fills the viewport when automatic.
- [x] Placeholder Text measured through `TextShaper`, with `PlaceholderTextShaper` in fixtures.
- [x] Single-line row/column stacks with margin, padding, gap, justify, align/stretch, and grow/shrink with limit freezing.
- [x] Define overflow, display-none, and hidden-box behavior.
- [x] Numeric geometry fixtures and the `flex-layout` example; verified commands in [development](../guides/development.md).

### Exit criteria

- Fixed, row/column, nested padding, margin/gap, constraint, overflow, empty, and fractional fixtures produce exact or tolerance-bounded boxes.
- Repeated layout of identical input produces identical results.
- Invalid input, text measurement failures, and non-finite geometry yield located diagnostics without a partial result.
- Layout depends only on the tree, resolved styles, viewport, and the `TextShaper` interface; no GPU, font, or platform type is involved.

### Scope limits

No wrapping, absolute positioning, scrolling/clipping, grid, style resolution, dirty-subtree invalidation, or pixel snapping. Placeholder text metrics are not representative of real fonts.

## Immediately next — v0.1.0 minimal Vulkan menu

Generate backend-neutral paint commands from `LayoutResult` and resolved styles, then prove them with Vulkan primitives, pointer hit testing, and a minimal native menu host. The objective, work, and exit criteria are in the [backlog](backlog.md#v010-candidate--minimal-vulkan-menu) until that milestone is activated.
