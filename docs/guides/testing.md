# Testing strategy

Test algorithms and boundaries heavily; test appearance selectively. Keep tests deterministic, compact, and meaningful. Do not build exhaustive screenshot coverage for every widget.

## High-value checks

| Subsystem | Primary checks |
| --- | --- |
| [UI model](../design/ui-model.md) | Semantic round trip, stable ordering, schema/type failures, invalid IDs/references/versions, keyed instance reuse and disposal, typed binding candidate rejection and full-tree parity |
| [Reactive runtime](../design/reactive-runtime.md) | Chain/diamond coherence, dynamic edges, equality suppression, batching, cycle/reentrancy failures, fault-boundary containment, disposal and effect phases |
| [Layout](../design/layout.md) | Fixed, stack/flex, constraints, nested spacing, scroll extents/offsets; later absolute/grid/intrinsic geometry |
| [Styling](../design/styling.md) | Selector matching, layer/source-order ties, inheritance, overrides, pseudo-state invalidation |
| [Input](../design/input.md) | Hit testing, clip/transform agreement, cancellation, propagation, focus/navigation recovery, typed drop eligibility |
| [Commands](../design/commands.md) | Equal invocation and eligibility across input sources, argument validation, disabled agreement with semantics, one transaction per gesture |
| [Text](../design/text.md) | Shaping/measurement agreement, UTF-8 errors, fallback, wrapping, cache invalidation |
| [Localization](../design/localization.md) | Locale/argument validation, fallback and missing-key distinctions, formatting, atomic locale/catalog changes, logical direction and pseudo locales |
| [Rendering](../design/rendering.md) | Paint order, stack validity, resource lifetime, a few primitive/glyph images |
| [Semantics](../design/semantics.md) | Roles/names/relationships, state/action consistency, stale identities and adapter boundaries |
| [Replay](../design/replay.md) | Controlled time/readiness, deterministic outputs, malformed recording/version failures |
| [Editor bridge](../design/path-finder-integration.md) | Shared documents, metadata preservation, rejected reload, compatible state and cleanup |
| [Inspection](../design/inspection.md) | Coherent generations, target/source mapping, state-slot validation, invariant results, snapshot/diff bounds and production boundary |
| [Web host](../design/web-host.md) | Native/Web parity, semantic DOM/focus, browser editing, JS/WASM ownership and teardown |

Use synthetic inputs and numeric output wherever possible. Golden serialized fixtures must assert semantics as well as deterministic formatting. Later dirty-update paths should agree with the full-tree reference calculation.

[CMakeLists.txt](../../CMakeLists.txt) owns test registration. Subsystem pages link their focused checks; the [development guide](development.md) owns execution commands, and [support](../reference/support-matrix.md) owns dated outcomes/configurations. Checks should remain active in Release as well as Debug.

## Replay fixtures

Use the [Replay contract](../design/replay.md) for fixture inputs and observable outputs. Start with numeric geometry, semantic snapshots, expected action order, and owned paint commands. Add GPU images only for rendering behavior that numeric/command fixtures cannot prove.

Each milestone should retain the artifacts relevant to its capability: unit/boundary checks, serialized documents, numeric fixtures, replay/expected-action fixtures when interaction tooling exists, and selective runtime images when rendering exists. Record unsupported-feature diagnostics and the tested environment. Do not require GPU screenshots for a core-only foundation or claim a proposed replay tool ran.

## Evidence by capability kind

- Data model and serialization: tree creation/inspection/round trips without any GPU SDK.
- Geometry: exact or explicitly tolerance-bounded `LayoutBox` fixtures.
- GPU backends: actual pixels demonstrate order, clipping, and transforms; shader compilation alone is insufficient.
- Interaction: synthetic dispatch checks plus a native host smoke; physical-device sessions are separate evidence.
- Text: fixed fonts and representative Latin/Japanese fixtures with measurement and image agreement.
- Reference rendering: CPU reference images, identical across runs and thread counts in deterministic mode, with GPU agreement within declared tolerances.
- Components and reload: identity, cleanup, resource retirement, error containment, and repeated reload behavior.

The [roadmap](../roadmap/README.md) decides when each capability is scheduled; this list only states what kind of evidence it needs.

## Inspection and snapshot fixtures

As the [inspection proposal](../design/inspection.md) is implemented, use in-process fixtures before testing CLI or transport wrappers. Verify missing/ambiguous/scoped selectors, stale generation rejection after reload, source origins through lowering, and semantic versus raw-input action behavior. Reject undeclared/invalid state slots and restore state only at defined update points.

Capture tree, geometry, diagnostics, render counters, and optional image from one generation. Prefer [UI invariants](../design/inspection.md#proposed-ui-invariants) and semantic/layout comparisons over image comparison; an invariant with an ambiguous or missing target must fail. Test malformed/oversized bundles, unsupported versions, missing artifacts, and incompatible comparison environments. Match stable identities and assert semantic/layout changes directly. Reuse a small visual fixture set; multiple selected viewports should reveal layout/clip/focus issues without creating a widget screenshot catalog.

Check a core-only observation path with no renderer, a window-free CPU reference capture path, and a window-free GPU capture path with explicit device/completion requirements. Never silently substitute the CPU backend for a failing GPU configuration. Future CI artifacts may include actual/expected/diff images, a snapshot bundle, and a machine-readable report. Reports must distinguish unavailable observations from successful zero-valued metrics.

Development transports require bounded input, ordinary action eligibility, allowed-state enforcement, cancellation/disposal, and production exclusion fixtures. Browser adapters require their own focus/IME/accessibility and lifecycle checks under [Web host](../design/web-host.md#verification); native evidence does not establish browser behavior.

## Localization fixtures

As the [localization proposal](../design/localization.md) is implemented, verify locale normalization/rejection, script-preserving fallback, duplicate/missing message IDs, argument types, plural/select branches, number/date/time/unit formatting, and unavailable data with fixed catalog/formatter inputs. Distinguish fallback hits from missing messages and formatting failures. Cover rejected catalog reload, late resource completion after provider disposal, nested locale providers and stable component/focus/edit state across switches. Incremental results must equal full resolution and presentation.

Use a compact consumer fixture in its declared default locale, an expanded pseudo locale and an RTL pseudo locale. Pseudo transformations operate on message literal content while preserving IDs, argument types and formatting structure; make expansion and direction settings deterministic. Assert expected message coverage/markers, geometry, wrapping/clipping, logical edge/alignment mapping, nested direction overrides, semantic names, and directional icons. Check pointer targeting and keyboard/gamepad navigation against the resolved geometry. Intentional clipping/scrolling follows the fixture's declared overflow policy. Pseudo RTL stresses UI direction but does not replace actual mixed-direction shaping fixtures.

Text fixtures introduce real script/BiDi and language-aware fallback cases under the [text contract](../design/text.md#proposed-international-text-layout). Fix locale-data identity, catalog revisions, formatting preferences/time zone, fonts and backend in [replay](../design/replay.md#proposed-localization-inputs) and capture inputs. Pair numeric/semantic checks with a few CPU-reference images when that capture path is available, plus declared GPU comparisons for render behavior. Avoid locale-by-widget screenshot multiplication. CI/agent reports distinguish missing translation, fallback, missing glyph and overflow and identify resource/node source locations; these are procedures, not CLI flags or executed-check evidence.

## Image regression and performance

Keep a small reference set for overlapping rectangles, clips, transforms, borders, alpha, images, and glyphs as each capability arrives. Declare target size/scale, color format, font versions, pixel tolerances, and backend/GPU configuration. Once the [CPU reference backend](../design/rendering.md#proposed-cpu-reference-backend) exists, its deterministic output is the baseline: GPU candidates are compared with it under declared tolerances, and optimized CPU paths with its scalar result. Do not use images to prove behavior better established by geometry or event assertions.

Performance checks follow a correct baseline. Record UI size, changed-node count, layout/paint CPU cost, uploads, batches/submissions, and GPU cost when useful. Hardware timing is an observation with conditions, not a universal pass threshold. Avoid premature optimization gates.

Use representative dense lists/inspectors, deep composition, animation, and frequent property updates when a consumer needs them. State hardware/build/backend/fonts, warmup, samples, and tolerances; keep counter regressions separate from timing noise. Set budgets from measured consumer workloads rather than framework-speedup estimates. Include source-edit-to-verified-snapshot latency and its stage breakdown when reload/capture tooling exists. Incremental/reactive and retained-GPU optimizations must preserve full-tree/reference output before their performance matters.

## Documentation verification

For Markdown-only changes, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1
```

The [checker](../../scripts/check-docs.ps1) validates relative file/directory links, Markdown heading fragments, code fences, canonical ownership links, and misplaced rolling status/checklists. It also rejects progress wording and version labels in contract pages and planned-only support rows, under the [status and wording rules](../README.md#status-and-wording-rules). External links are left to a separate network check when needed; local verification requires no network.

Review [canonical ownership and change routing](../README.md#canonical-owners): technical details belong in one subsystem page, active scope in current, future scope in backlog, delivery history in changelog, and live capability/configuration evidence in support. A proposed API/schema must be labeled and must not silently change JSON v1.

Automated checks cannot establish semantic uniqueness or implementation truth. Review those manually against code and the owner-provided direction. Do not report runtime tests for a documentation-only check.
