# Testing strategy

Status: Verification plan. No automated tests or runtime results exist yet.

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
| [Editor bridge](../design/path-finder-integration.md) | Shared documents, metadata preservation, rejected reload, compatible state and cleanup |

Use synthetic inputs and numeric output wherever possible. Golden serialized fixtures must assert semantics as well as deterministic formatting. Later dirty-update paths should agree with the full-tree reference calculation.

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

For Markdown-only changes, verify local link targets, balanced code fences, unique canonical ownership, and agreement between README, current work, backlog, and support matrix. Planned examples and conceptual API snippets must be labeled. Do not report a runtime test pass when only documentation checks ran.

As tests are introduced, record verified commands in the [development guide](development.md) and proven configurations in the [support matrix](../reference/support-matrix.md).
