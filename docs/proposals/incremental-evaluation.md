# Incremental evaluation

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed presentation integration

Bindings invalidate consumers according to [property effects](../architecture/styling.md#property-effects), using the same metadata as validation and Inspector tooling. Stage invalidation is a set, not one mutually exclusive classification. [Layout](#invalidation), [text](#proposed-incremental-text-invalidation), and [render preparation](#proposed-retained-render-updates) own their algorithms and cache keys; participating in scheduling does not merge these subsystems into the kernel.

A paint-only value change should avoid measurement and layout. Geometry, inherited state, text, and clip changes must invalidate every affected layout/input/semantic/paint consumer, including ancestors or siblings when required. Invisible or unrealized content cannot be skipped merely because it has no draw commands: dependencies, semantics, focus/navigation, and cleanup can remain observable. Fine-grained paths must match full-tree output before retained rendering or priority scheduling relies on them.

[Inspection](../architecture/inspection.md#proposed-reactive-observations) owns graph snapshots and update-cause records; the kernel supplies bounded observations without exposing mutable storage. Components and future authoring facades use the same identity, ownership, and update model rather than establishing separate reconciliation or scheduling semantics.

## Proposed stage invalidation

The [reactive scheduler](#proposed-presentation-integration) consumes this stage mapping without importing renderer or component state into style resolution. Distinguish style resolution, text/intrinsic measurement, placement, paint, input targeting, and semantic projection. Measurement and placement can refine the layout stage; they do not change the implemented descriptor bits. Resolve inherited values and pseudo-state effects before deciding which outputs actually changed.

Text color affects paint without requiring shaping or layout. Font face/size and text content affect measurement, placement, paint, and any dependent semantic names/bounds. Disabled changes affect input and semantics and can also affect style/layout through matching rules. Clip/transform changes can affect input and semantic bounds as well as paint. Theme changes invalidate actual consumers, including inherited dependents; do not infer paint-only work from a property's visual name.

## Invalidation

Full-tree calculation is the correctness reference. Proposed incremental calculation distinguishes intrinsic/text measurement from placement under [stage invalidation](#proposed-stage-invalidation). Size changes can invalidate ancestors and siblings; available size, inherited font metrics, flex allocation, scroll extents/clamping, and clips must be included in dependency propagation. An unchanged measured size may stop size propagation, but changed placement or clips must reach input, semantic bounds, and paint. Paint-only color changes do not require layout.

The [reactive scheduler](#proposed-presentation-integration) selects work and publication boundaries; layout consumes nodes, resolved styles, constraints, and text metrics without accessing graph internals or a renderer. Incremental output must preserve the result of a full calculation.

## Proposed incremental text invalidation

Treat font selection, Unicode/script analysis, shaping, line breaking, placement, and raster preparation as distinct dependency stages under [reactive presentation integration](#proposed-presentation-integration). Text color changes only paint. Font/alias/fallback/variation or string changes invalidate the relevant analysis, shaping, measurement, and paint; a width change invalidates wrapping and placement and can require reshaping where the wrapping algorithm shapes candidate segments. Do not promise line-break-only work unless cached shaping remains valid for that algorithm.

Cache keys include all consumed text/style/constraint and font-environment revisions, plus locale/direction/options when supported. Raster size, face revision, and scale obey the [glyph cache contract](../architecture/text.md#implemented-grayscale-raster-and-cache-contract). Atlas or paint reuse cannot keep glyph indices from a replaced face. An accepted cache hit must preserve metrics, glyphs, and diagnostics, with `measure` and `shape` agreeing under the same inputs. Repeated full shaping/measurement is the correctness reference; stage reuse must not infer broader Unicode support or couple text to GPU storage.

## Proposed retained render updates

[Reactive presentation invalidation](#proposed-presentation-integration) can feed persistent display items, retained packets/atlas pages, instance-buffer regions, and partial uploads through backend-neutral paint data. Backends consume resolved changes and resource identities, never signals, owner objects, or application state. A paint-only change should update affected records without rebuilding unrelated geometry; static content can reuse prepared data under a declared lifetime.

Preserve ordering, blend/clip/transform batch boundaries, and completion-safe ownership. Removal or replacement invalidates retained records; uploads and atlas regions cannot be overwritten while an earlier submission references them. Logical owner cleanup requests retirement rather than bypassing completion. Compare both commands and rendered output with full paint/reference rendering before measuring work reduction. Record affected items, rebuilt batches, and upload bytes without assuming that a local change always has constant cost. A separate Paint Tree or Render Tree is not required merely to mirror an architecture diagram; introduce a representation only with a defined consumer and lifetime.

## Promotion criteria

Promote a particular incremental stage only after a representative consumer identifies measured work worth avoiding. Require parity with the full-tree/scalar reference and explicit dependency/cache/lifetime checks before accepting a work-reduction claim.
