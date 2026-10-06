# Testing strategy

Test algorithms and boundaries heavily; test appearance selectively. Keep tests deterministic, compact, and meaningful. Do not build exhaustive screenshot coverage for every widget.

## High-value checks

| Subsystem | Primary checks |
| --- | --- |
| [UI model](../design/ui-model.md) | Semantic round trip, stable ordering, schema/type failures, invalid IDs/references/versions |
| [Layout](../design/layout.md) | Fixed, stack/flex, constraints, nested spacing, scroll extents/offsets; later absolute/grid/intrinsic geometry |
| [Styling](../design/styling.md) | Selector matching, layer/source-order ties, inheritance, overrides, pseudo-state invalidation |
| [Input](../design/input.md) | Hit testing, clip/transform agreement, cancellation, propagation, focus/navigation recovery |
| [Text](../design/text.md) | Shaping/measurement agreement, UTF-8 errors, fallback, wrapping, cache invalidation |
| [Rendering](../design/rendering.md) | Paint order, stack validity, resource lifetime, a few primitive/glyph images |
| [Semantics](../design/semantics.md) | Roles/names/relationships, state/action consistency, stale identities and adapter boundaries |
| [Replay](../design/replay.md) | Controlled time/readiness, deterministic outputs, malformed recording/version failures |
| [Editor bridge](../design/path-finder-integration.md) | Shared documents, metadata preservation, rejected reload, compatible state and cleanup |
| [Inspection](../design/inspection.md) | Coherent generations, target/source mapping, state-slot validation, snapshot/diff bounds and production boundary |
| [Web host](../design/web-host.md) | Native/Web parity, semantic DOM/focus, browser editing, JS/WASM ownership and teardown |

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
- Reference rendering: CPU reference images for the existing primitive/glyph set, identical across runs and thread counts in deterministic mode, with GPU agreement within declared tolerances.
- Components/reload: identity, cleanup, resource retirement, and repeated reload behavior.

Candidate versions and ordering are owned by the [roadmap](../roadmap/README.md).

## Inspection and snapshot fixtures

As the [inspection proposal](../design/inspection.md) is implemented, use in-process fixtures before testing CLI or transport wrappers. Verify missing/ambiguous/scoped selectors, stale generation rejection after reload, source origins through lowering, and semantic versus raw-input action behavior. Reject undeclared/invalid state slots and restore state only at defined update points.

Capture tree, geometry, diagnostics, render counters, and optional image from one generation. Test malformed/oversized bundles, unsupported versions, missing artifacts, and incompatible comparison environments. Match stable identities and assert semantic/layout changes directly. Reuse a small visual fixture set; multiple selected viewports should reveal layout/clip/focus issues without creating a widget screenshot catalog.

Check a core-only observation path with no renderer, a window-free CPU reference capture path, and a window-free GPU capture path with explicit device/completion requirements. Never silently substitute the CPU backend for a failing GPU configuration. Future CI artifacts may include actual/expected/diff images, a snapshot bundle, and a machine-readable report. Reports must distinguish unavailable observations from successful zero-valued metrics.

Development transports require bounded input, ordinary action eligibility, allowed-state enforcement, cancellation/disposal, and production exclusion fixtures. Browser adapters require their own focus/IME/accessibility and lifecycle checks under [Web host](../design/web-host.md#verification); native evidence does not establish browser behavior.

## Image regression and performance

Keep a small reference set for overlapping rectangles, clips, transforms, borders, alpha, images, and glyphs as each capability arrives. Declare target size/scale, color format, font versions, pixel tolerances, and backend/GPU configuration. Once the [CPU reference backend](../design/rendering.md#proposed-cpu-reference-backend) exists, its deterministic output is the baseline: GPU candidates are compared with it under declared tolerances, and optimized CPU paths with its scalar result. Do not use images to prove behavior better established by geometry or event assertions.

Performance checks follow a correct baseline. Record UI size, changed-node count, layout/paint CPU cost, uploads, batches/submissions, and GPU cost when useful. Hardware timing is an observation with conditions, not a universal pass threshold. Avoid premature optimization gates.

Use representative dense lists/inspectors, deep composition, animation, and frequent property updates when a consumer needs them. State hardware/build/backend/fonts, warmup, samples, and tolerances; keep counter regressions separate from timing noise. Set budgets from measured consumer workloads rather than framework-speedup estimates. Include source-edit-to-verified-snapshot latency and its stage breakdown when reload/capture tooling exists. Incremental/reactive and retained-GPU optimizations must preserve full-tree/reference output before their performance matters.

## Documentation verification

For Markdown-only changes, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1
```

The [checker](../../scripts/check-docs.ps1) validates relative file/directory links, Markdown heading fragments, code fences, canonical ownership links, and misplaced rolling status/checklists. External links are left to a separate network check when needed; local verification requires no network.

Review [canonical ownership and change routing](../README.md#canonical-owners): technical details belong in one subsystem page, active scope in current, future scope in backlog, delivery history in changelog, and live capability/configuration evidence in support. A proposed API/schema must be labeled and must not silently change JSON v1.

Automated checks cannot establish semantic uniqueness or implementation truth. Review those manually against code and the owner-provided direction. Do not report runtime tests for a documentation-only check.
