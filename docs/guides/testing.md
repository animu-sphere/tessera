# Testing strategy

Test algorithms and boundaries heavily; test appearance selectively. Keep tests deterministic, compact, and meaningful. Do not build exhaustive screenshot coverage for every widget.

## Verification ownership

Guarantees belong at the boundary that implements them; consumers verify their use. Apply the [bounded acceptance rule](../agent-policy.md#verification-ownership-and-bounded-acceptance) before adding tests. Cite an inherited owner test and check that its assumptions match the application. An unchanged guarantee does not need a new exhaustive consumer proof.

| Layer | Responsibility | Consumer scope |
| --- | --- | --- |
| L1 Runtime contract | Universal invariants, validation, ownership, lifetime and consistency | Exercise affected owner tests when their boundary changes |
| L2 Widget contract | Widget-specific input, state and public API | Representative success and boundary cases for the widget |
| L3 Consumer integration | A real application operation through public APIs | Successful user flow, wiring and operation-specific risks |
| L4 Host/platform | Source imports, compiler/configuration, GPU and OS integration | Affected integration changes and wider scheduled/release checks |

The source-backed responsibility map below identifies reusable checks, not executed results or a requirement to run every row for each operation. Host-specific guarantees stay with their host.

| Property | Primary owner/check | Minimum consumer observation |
| --- | --- | --- |
| Once-only sequence consumption and stale command/activation rejection | [commands](../../tests/commands/commands_tests.cpp), including `execution_rejection_and_lifetime` | The intended command reaches the handler once with the correct identity |
| Logical text draft/selection and edit owner/revision rejection | [text_edit](../../tests/input/text_edit_tests.cpp) | Correct field routing and one representative independent-field retention case; cross-field routing is a consumer risk |
| Focus recovery, pointer cancellation and scroll geometry | [focus](../../tests/input/focus_tests.cpp), [pointer](../../tests/input/pointer_tests.cpp), [scroll](../../tests/input/scroll_tests.cpp) | A representative reachable operation using the application's normalized mappings |
| Accepted settings/view retention after rejected staging | The host's [Diagnostics fixture](../../tests/integrations/renderer_diagnostics_tests.cpp) or [Settings fixture](../../examples/settings/main.cpp) | Cover a new staging branch and retained unrelated state; executor tests do not establish host atomicity |
| Deterministic CPU pixels and replay processing | [cpu_renderer](../../tests/render/cpu_renderer_tests.cpp), [replay](../../tests/replay/replay_tests.cpp), [offscreen_runner](../../tests/inspection/offscreen_runner_tests.cpp) | Representative accepted semantics/geometry/paint; add image comparisons for changed drawing behavior |

Pointer and keyboard/gamepad/semantic routing must share ordinary commands. Check representative parity at the consumer's routing boundary; do not multiply every runtime failure by every input source. A first cross-layer failure mode, a real regression or a different trust/owner boundary warrants deeper L3 coverage with a stated reason and stop condition. Keep a minimal regression fixture and useful diagnostics rather than all edits multiplied by all injected failures. Distinguish an inherited guarantee from a test executed for this change in [support](../reference/support-matrix.md).

## Validation tiers

This is the execution policy for selecting and evolving checks. Workflow configuration and required-check settings must be verified before claiming a tier is enforced; source/configuration findings and measured outcomes belong only in support.

| Tier | Scope and frequency |
| --- | --- |
| 0 Local | Build affected targets and run focused CTests; documentation-only changes use the documentation checker. Aim for seconds to a few minutes, subject to measurement. |
| 1 PR | One representative core environment, initially Windows x64 Debug as the candidate, fast owner contracts and a small affected consumer smoke. Widen for foundation/build changes. PR p95 at most ten minutes is a provisional SLO to revise after baseline measurement. |
| 2 Integration/periodic | Full core Debug/Release, major consumers, both independent import modes, deterministic CPU/replay checks, saved durations and failures. Run after main integration or on a regular schedule; prefer daily coverage when runner capacity permits. |
| 3 Release/platform | Version/tag/changelog consistency, full required Debug/Release and both imports, plus optional module, GPU/font/native-host evidence for the supported configuration. Unavailable hardware checks are unvalidated, never replaced by compile success. |

Keep broad checks when moving them out of the PR gate. Repeated Tier 2 failure blocks the next release and takes repair priority; promote a missed critical regression check back to Tier 1. GPU/desktop checks require suitable runners or explicitly recorded local evidence. The release workflow remains responsible for release verification/publication; the [development guide](development.md#release-workflow) owns its commands.

### Change selection

Start with a small explicit policy, not an elaborate path-selection engine. Select the union for mixed changes; unknown or cross-foundation changes fall back to full core coverage.

| Change | Additional PR/local selection |
| --- | --- |
| Documentation only | Documentation links/ownership checker; runtime build may be omitted |
| C++ authoring/widget | Authoring, affected widget contract and one representative consumer |
| Commands/input/reactive | Affected owner contracts, representative integration and replay |
| Render/layout | Affected contracts, a CPU fixture and representative consumer |
| CMake/import configuration | At least one independent consumer import; both modes for broad import changes |
| Vulkan/fonts/native host | Affected module build and dedicated rendering/font/platform checks under their environment requirements |
| Release metadata/workflow | Metadata validation and the minimum affected build configuration; broader release checks at Tier 3 |

Debug/Release and both import modes are wider integration/release obligations, not a new full matrix for every UI operation. Core fast contracts belong in the PR gate; full core coverage also belongs there for high-risk or uncertain changes. Source imports run for relevant build changes and at wider tiers. Replay/CPU checks run for affected runtime/render/replay changes and at wider tiers. Required checks must complete even for documentation-only PRs; put any skip inside a reporting job instead of leaving a path-filtered required workflow pending. If an automated matcher is introduced, test it, use a conservative fallback and review misses regularly.

### Measurement and safe reduction

Before reducing protection, inventory CTests and workflows, identify each guarantee's owner and classify checks by L1-L4. Measure configure, build and test time separately in Debug/Release; distinguish queue time from job time. Use JUnit and CTest cost data to identify slow tests. Collect 20-30 CI runs where available, with conditions, sample count, median/p95, longest job, failure/flaky rerun rates, cache effects and duplicate-coverage candidates. Missing observations remain explicit gaps. [Development](development.md#focused-checks-and-timing) owns the local collection commands; support owns the baseline and artifacts.

Introduce a few contract/smoke/import labels before using them to exclude tests. Labels alone do not reduce compilation time. Measure safe test parallelism, especially independent consumer configure/build jobs, before increasing it. Add cache or build-target separation only for a measured bottleneck; cache keys must include toolchain and dependency identity. Do not delete tests during classification.

Run a small PR workflow and a full integration/periodic workflow alongside the old gate before changing required status names. Check branch protection/rulesets, runner capacity and cost first, then coordinate the required-check transition. Preserve the release workflow's FetchContent exclusion during initial measurement; wider coverage must exercise both imports before release acceptance.

Reduce a duplicate assertion only when the change records the equivalent owner test, applicable assumptions and preserved diagnostic value. Keep slow checks in wider tiers. Observe Tier 2 failures closely for two weeks after a gate/reduction change. Any temporary flaky-check quarantine needs a reason, owner and expiry. Report failed test names, durations and artifact links concisely. Judge the change by PR p95, unrelated work, reruns, post-merge regressions, triage time and consumer-operation lead time; test count and test/production line ratios have no universal target. Stop CI expansion when feedback is small and actionable, owner coverage is preserved and wider validation catches regressions without obstructing UI authoring.

## High-value checks

| Subsystem | Primary checks |
| --- | --- |
| [UI model](../architecture/ui-model.md) | Semantic round trip, stable ordering, schema/type failures, invalid IDs/references/versions, keyed instance reuse and disposal, async attempt/lifetime rejection and stale-value preservation, typed binding candidate rejection and full-tree parity |
| [Reactive runtime](../architecture/reactive-runtime.md) | Chain/diamond coherence, dynamic edges, equality suppression, batching, cycle/reentrancy failures, fault-boundary containment, disposal and effect phases |
| [Layout](../architecture/layout.md) | Fixed, stack/flex, constraints, nested spacing, scroll extents/offsets; later absolute/grid/intrinsic geometry |
| [Styling](../architecture/styling.md) | Selector matching, layer/source-order ties, inheritance, overrides, pseudo-state invalidation |
| [Input](../architecture/input.md) | Hit testing, clip/transform agreement, cancellation, propagation, focus/navigation recovery, typed drop eligibility |
| [Commands](../architecture/commands.md) | Equal invocation and eligibility across input sources, argument validation, disabled agreement with semantics, one transaction per gesture |
| [Text](../architecture/text.md) | Shaping/measurement agreement, UTF-8 errors, fallback, wrapping, cache invalidation |
| [Localization](../architecture/localization.md) | Locale/argument validation, fallback and missing-key distinctions, formatting, atomic locale/catalog changes, logical direction and pseudo locales |
| [Rendering](../architecture/rendering.md) | Paint order, stack validity, resource lifetime, a few primitive/glyph images |
| [Semantics](../architecture/semantics.md) | Roles/names/relationships, state/action consistency, stale identities and adapter boundaries |
| [Replay](../architecture/replay.md) | Controlled time/readiness, deterministic outputs, malformed recording/version failures |
| [Editor bridge](../integrations/path-finder.md) | Shared documents, metadata preservation, rejected reload, compatible state and cleanup |
| [Inspection](../architecture/inspection.md) | Coherent generations, target/source mapping, state-slot validation, invariant results, snapshot/diff bounds and production boundary |
| [Web host](../proposals/web-host.md) | Native/Web parity, semantic DOM/focus, browser editing, JS/WASM ownership and teardown |

Use synthetic inputs and numeric output wherever possible. Golden serialized fixtures must assert semantics as well as deterministic formatting. Later dirty-update paths should agree with the full-tree reference calculation.

[CMakeLists.txt](../../CMakeLists.txt) owns test registration. Subsystem pages link their focused checks; the [development guide](development.md) owns execution commands, and [support](../reference/support-matrix.md) owns dated outcomes/configurations. Checks should remain active in Release as well as Debug.

## Replay fixtures

Use the [Replay contract](../architecture/replay.md) for fixture inputs and observable outputs. Start with numeric geometry, semantic snapshots, expected action order, and owned paint commands. Add GPU images only for rendering behavior that numeric/command fixtures cannot prove.

Each milestone should retain the artifacts relevant to its capability: unit/boundary checks, serialized documents, numeric fixtures, replay/expected-action fixtures when interaction tooling exists, and selective runtime images when rendering exists. Record unsupported-feature diagnostics and the tested environment. Do not require GPU screenshots for a core-only foundation or claim a proposed replay tool ran.

## Evidence by capability kind

- Data model and serialization: tree creation/inspection/round trips without any GPU SDK.
- Geometry: exact or explicitly tolerance-bounded `LayoutBox` fixtures.
- GPU backends: actual pixels demonstrate order, clipping, and transforms; shader compilation alone is insufficient.
- Interaction: synthetic dispatch checks plus a native host smoke; physical-device sessions are separate evidence.
- Text: fixed fonts and representative Latin/Japanese fixtures with measurement and image agreement.
- Reference rendering: CPU reference images, identical across runs and thread counts in deterministic mode, with GPU agreement within declared tolerances.
- Components and reload: identity, cleanup, resource retirement, error containment, and repeated reload behavior.
- Source consumption: configure a separate host through each import path, build/link only public target aliases, and execute a compact API fixture. Check transitive language/include/link requirements, host testing ownership, embedded defaults, and core-only exclusion of optional discovery. Optional backend linkage checks do not establish GPU execution.

The [roadmap](../roadmap/README.md) decides when each capability is scheduled; this list only states what kind of evidence it needs.

## Inspection and snapshot fixtures

As the [inspection proposal](../architecture/inspection.md) is implemented, use in-process fixtures before testing CLI or transport wrappers. Verify missing/ambiguous/scoped selectors, stale generation rejection after reload, source origins through lowering, and semantic versus raw-input action behavior. Reject undeclared/invalid state slots and restore state only at defined update points.

Capture tree, geometry, diagnostics, render counters, and optional image from one generation. Prefer [UI invariants](../architecture/inspection.md#proposed-ui-invariants) and semantic/layout comparisons over image comparison; an invariant with an ambiguous or missing target must fail. Test malformed/oversized bundles, unsupported versions, missing artifacts, and incompatible comparison environments. Match stable identities and assert semantic/layout changes directly. Reuse a small visual fixture set; multiple selected viewports should reveal layout/clip/focus issues without creating a widget screenshot catalog.

Check a core-only observation path with no renderer, a window-free CPU reference capture path, and a window-free GPU capture path with explicit device/completion requirements. Never silently substitute the CPU backend for a failing GPU configuration. Future CI artifacts may include actual/expected/diff images, a snapshot bundle, and a machine-readable report. Reports must distinguish unavailable observations from successful zero-valued metrics.

Development transports require bounded input, ordinary action eligibility, allowed-state enforcement, cancellation/disposal, and production exclusion fixtures. Browser adapters require their own focus/IME/accessibility and lifecycle checks under [Web host](../proposals/web-host.md#verification); native evidence does not establish browser behavior.

## Localization fixtures

As the [localization proposal](../architecture/localization.md) is implemented, verify locale normalization/rejection, script-preserving fallback, duplicate/missing message IDs, argument types, plural/select branches, number/date/time/unit formatting, and unavailable data with fixed catalog/formatter inputs. Distinguish fallback hits from missing messages and formatting failures. Cover rejected catalog reload, late resource completion after provider disposal, nested locale providers and stable component/focus/edit state across switches. Incremental results must equal full resolution and presentation.

Use a compact consumer fixture in its declared default locale, an expanded pseudo locale and an RTL pseudo locale. Pseudo transformations operate on message literal content while preserving IDs, argument types and formatting structure; make expansion and direction settings deterministic. Assert expected message coverage/markers, geometry, wrapping/clipping, logical edge/alignment mapping, nested direction overrides, semantic names, and directional icons. Check pointer targeting and keyboard/gamepad navigation against the resolved geometry. Intentional clipping/scrolling follows the fixture's declared overflow policy. Pseudo RTL stresses UI direction but does not replace actual mixed-direction shaping fixtures.

Text fixtures introduce real script/BiDi and language-aware fallback cases under the [text contract](../architecture/text.md#proposed-international-text-layout). Fix locale-data identity, catalog revisions, formatting preferences/time zone, fonts and backend in [replay](../architecture/replay.md#proposed-localization-inputs) and capture inputs. Pair numeric/semantic checks with a few CPU-reference images when that capture path is available, plus declared GPU comparisons for render behavior. Avoid locale-by-widget screenshot multiplication. CI/agent reports distinguish missing translation, fallback, missing glyph and overflow and identify resource/node source locations; these are procedures, not CLI flags or executed-check evidence.

## Image regression and performance

Keep a small reference set for overlapping rectangles, clips, transforms, borders, alpha, images, and glyphs as each capability arrives. Declare target size/scale, color format, font versions, pixel tolerances, edge/subpixel conditions, and backend/GPU configuration. The [CPU reference backend](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) provides deterministic reference output: GPU candidates are compared with it under declared tolerances, and optimized CPU paths with its scalar result. Do not use images to prove behavior better established by geometry or event assertions.

Performance checks follow a correct baseline. Record UI size, changed-node count, layout/paint CPU cost, uploads, batches/submissions, and GPU cost when useful. Hardware timing is an observation with conditions, not a universal pass threshold. Avoid premature optimization gates.

Use representative dense lists/inspectors, deep composition, animation, and frequent property updates when a consumer needs them. State hardware/build/backend/fonts, warmup, samples, and tolerances; keep counter regressions separate from timing noise. Set budgets from measured consumer workloads rather than framework-speedup estimates. Include source-edit-to-verified-snapshot latency and its stage breakdown when reload/capture tooling exists. Incremental/reactive and retained-GPU optimizations must preserve full-tree/reference output before their performance matters.

## Documentation verification

For Markdown-only changes, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1
```

The [checker](../../scripts/check-docs.ps1) validates relative file/directory links, Markdown heading fragments, code fences, canonical ownership links, and misplaced rolling status/checklists. It also checks architecture invariants, proposal authority/promotion markers and obsolete documentation paths, and rejects progress wording, misplaced version labels and planned-only support rows under the [status and wording rules](../README.md#status-and-wording-rules). External links are left to a separate network check when needed; local verification requires no network.

Review [canonical ownership and change routing](../README.md#canonical-owners): technical details belong in one subsystem page, active scope in current, future scope in backlog, delivery history in changelog, and live capability/configuration evidence in support. A proposed API/schema must be labeled and must not silently change JSON v1.

Automated checks cannot establish semantic uniqueness or implementation truth. Review those manually against code and the owner-provided direction. Do not report runtime tests for a documentation-only check.
