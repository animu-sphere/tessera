# Testing strategy

Test algorithms and boundaries heavily; test appearance selectively. Keep tests deterministic, compact, and meaningful. Do not build exhaustive screenshot coverage for every widget.

## High-value checks

| Subsystem | Primary checks |
| --- | --- |
| [UI model](../design/ui-model.md) | Semantic round trip, stable ordering, schema/type failures, invalid IDs/references/versions |
| [Layout](../design/layout.md) | Fixed, stack/flex, constraints, nested spacing; later absolute/scroll/grid/intrinsic geometry |
| [Styling](../design/styling.md) | Selector matching, cascade ties, inheritance, overrides, pseudo-state invalidation |
| [Input](../design/input.md) | Hit testing, clip/transform agreement, cancellation, propagation, focus/navigation recovery |
| [Text](../design/text.md) | Shaping/measurement agreement, UTF-8 errors, fallback, wrapping, cache invalidation |
| [Rendering](../design/rendering.md) | Paint order, stack validity, resource lifetime, a few primitive/glyph images |
| [Semantics](../design/semantics.md) | Roles/names/relationships, state/action consistency, stale identities and adapter boundaries |
| [Replay](../design/replay.md) | Controlled time/readiness, deterministic outputs, malformed recording/version failures |
| [Editor bridge](../design/path-finder-integration.md) | Shared documents, metadata preservation, rejected reload, compatible state and cleanup |

Use synthetic inputs and numeric output wherever possible. Golden serialized fixtures must assert semantics as well as deterministic formatting. Later dirty-update paths should agree with the full-tree reference calculation.

[CMakeLists.txt](../../CMakeLists.txt) owns test registration. Subsystem pages link their focused checks; the [development guide](development.md) owns execution commands, and [support](../reference/support-matrix.md) owns dated outcomes/configurations. Checks should remain active in Release as well as Debug.

## Replay fixtures

Use the [Replay contract](../design/replay.md) for fixture inputs and observable outputs. Start with numeric geometry, semantic snapshots, expected action order, and owned paint commands. Add GPU images only for rendering behavior that numeric/command fixtures cannot prove.

Each milestone should retain the artifacts relevant to its capability: unit/boundary checks, serialized documents, numeric fixtures, replay/expected-action fixtures when interaction tooling exists, and selective runtime images when rendering exists. Record unsupported-feature diagnostics and the tested environment. Do not require GPU screenshots for a core-only foundation or claim a proposed replay tool ran.

## Evidence by milestone

- Foundation: tree creation/inspection/serialization without any GPU SDK.
- Layout prototype: exact or explicitly tolerance-bounded `LayoutBox` fixtures.
- Vulkan prototype: shaders compile and actual pixels demonstrate order/clipping/transforms; compilation alone is insufficient.
- Input menu: synthetic dispatch checks plus a native host smoke.
- Text/menu styling: fixed fonts and representative Latin/Japanese fixtures with measurement and image agreement.
- Components/reload: identity, cleanup, resource retirement, and repeated reload behavior.

Candidate versions and ordering are owned by the [roadmap](../roadmap/README.md).

## Image regression and performance

Keep a small reference set for overlapping rectangles, clips, transforms, borders, alpha, images, and glyphs as each capability arrives. Declare target size/scale, color format, font versions, pixel tolerances, and GPU configuration. Do not use images to prove behavior better established by geometry or event assertions.

Performance checks follow a correct baseline. Record UI size, changed-node count, layout/paint CPU cost, uploads, batches/submissions, and GPU cost when useful. Hardware timing is an observation with conditions, not a universal pass threshold. Avoid premature optimization gates.

## Documentation verification

For Markdown-only changes, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1
```

The [checker](../../scripts/check-docs.ps1) validates relative file/directory links, Markdown heading fragments, code fences, canonical ownership links, and misplaced rolling status/checklists. External links are left to a separate network check when needed; local verification requires no network.

Review [canonical ownership and change routing](../README.md#canonical-owners): technical details belong in one subsystem page, active scope in current, future scope in backlog, delivery history in changelog, and live capability/configuration evidence in support. A proposed API/schema must be labeled and must not silently change JSON v1.

Automated checks cannot establish semantic uniqueness or implementation truth. Review those manually against code and the owner-provided direction. Do not report runtime tests for a documentation-only check.
