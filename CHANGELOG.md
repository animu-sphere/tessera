# Changelog

## Unreleased — 2026-10-05

- Added the Phase 1 layout prototype: full-tree `compute_layout` producing `LayoutResult` from a tree, resolved styles, viewport, and `TextShaper`, with border-box fixed sizes and min/max limits, single-line row/column flex (margin, padding, gap, justify, align/stretch, grow/shrink with limit freezing), safe overflow, and display-none/hidden handling. Added numeric layout fixtures and the `flex-layout` example. Rules are recorded in [layout](docs/design/layout.md).
- Added the first Phase 0 implementation slice: C++20 `tessera::core`, owned Box/Text documents, immutable indexed tree snapshots, typed properties, action-name validation, and actionable diagnostics.
- Added deterministic JSON v1 load/save with namespaced editor metadata, UTF-8 checks, finite binary64 values, and bounded input/nesting.
- Added Phase 0 subsystem contracts: logical-unit geometry, `ResolvedStyle` and `LayoutInput`/`LayoutBox`, normalized `InputEvent` values and host `ActionRequest` lifetime rules, `UiDrawList` with balanced clip/transform validation, `FrameInfo`/`UiRenderer` submission and retirement rules, and the `TextShaper` boundary with a deterministic `PlaceholderTextShaper`.
- Added a core-only serialization example and focused document/failure/lifetime and contract validation checks. Verification configuration and limitations are recorded in the [support matrix](docs/reference/support-matrix.md).

No public release, style resolver, input dispatcher, real text shaper, paint generator, or renderer backend is delivered yet. Next work is tracked in [current work](docs/roadmap/current.md).
