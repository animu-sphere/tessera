# Current

Updated: 2026-10-05. Phase 0, the Phase 1 layout prototype, backend-neutral paint generation, and rectangular pointer targeting/activation are implemented and verified on the recorded configuration. The v0.1.0 minimal Vulkan menu candidate is active. Delivery history is in the [changelog](../../CHANGELOG.md), with configuration evidence in the [support matrix](../reference/support-matrix.md).

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

Delivered: **tree + resolved styles + viewport + text metrics -> deterministic `LayoutBox` tree**, without any renderer. Fixed/stack/flex algorithms, scope limits, and numeric fixtures are recorded in [layout](../design/layout.md), the [changelog](../../CHANGELOG.md), and the [support matrix](../reference/support-matrix.md).

## Active — v0.1.0 minimal Vulkan menu candidate

Objective: prove the declarative UI -> style -> layout -> paint -> GPU pipeline with a small pointer-operated menu.

Depends on: Phase 0 model/contracts and Phase 1 deterministic fixed/stack/flex layout.

### Work

- [x] Backend-neutral background/border/Text paint generation, with deterministic preorder, alpha/visibility rules, validation, and numeric fixtures; see [rendering](../design/rendering.md#implemented-paint-generation).
- [ ] Vulkan primitive execution: rectangles/colors, inside borders, clips, and transforms, with actual runtime image evidence.
- [ ] Slang shader build path and a small number of primitive pipelines.
- [ ] Conservative batching that preserves visible draw order.
- [x] Rectangular pointer hit testing, hover/primary-click state, cancellation, snapshot refresh, and host action requests; synthetic tests and `pointer-menu` smoke; see [input](../design/input.md).
- [ ] Minimal standalone menu host with placeholder Text, OS input normalization/cancellation, and explicit window/device ownership.

Low-level image sampling can be proven during the renderer phase; a full asset/component API is not a release gate.

### Exit criteria

- A document produces deterministic geometry, paint commands, and a visible Vulkan menu.
- Overlap, border, clip, transform, and basic alpha fixtures have actual runtime image evidence.
- Synthetic pointer checks and a native-window smoke show predictable targeting and action dispatch.
- Public core APIs remain backend-neutral; submission/resource lifetime is verified for the supported host.
- Text placeholders and validated OS/compiler/GPU limits are explicitly reported.

### Immediately next

Select and record Vulkan/Slang and example-host dependency choices, then implement primitive execution below `UiDrawList`. Paint currently emits rectangles, borders, and placeholder glyph runs without implicit clips/transforms; the existing draw-list vocabulary supports manually authored clip/transform fixtures. No GPU pixels or native-window behavior have been validated.

Design owners: [rendering](../design/rendering.md), [input](../design/input.md), [text](../design/text.md). Later candidates are in the [backlog](backlog.md).
