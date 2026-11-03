# Testing strategy

Test algorithms and boundaries heavily; test appearance selectively. Keep tests deterministic, compact, and meaningful. Do not build exhaustive screenshot coverage for every widget.

## Verification ownership

Guarantees belong at the boundary that implements them; consumers verify their use. When a change touches a subsystem, run that subsystem's owner tests; when it adds an application operation, test the operation through the consumer fixture. Unchanged runtime guarantees are covered by the owner tests below and need no new consumer proof.

| Property | Owner test | Consumer check |
| --- | --- | --- |
| Once-only sequence consumption and stale command/activation rejection | [commands](../../tests/commands/commands_tests.cpp) | The intended command reaches the handler once |
| Logical text draft/selection and edit owner/revision rejection | [text_edit](../../tests/input/text_edit_tests.cpp) | Correct field routing and one independent-field retention case |
| Focus recovery, pointer cancellation and scroll geometry | [focus](../../tests/input/focus_tests.cpp), [pointer](../../tests/input/pointer_tests.cpp), [scroll](../../tests/input/scroll_tests.cpp) | A representative reachable operation |
| Accepted settings/view retention after rejected staging | [Diagnostics fixture](../../tests/integrations/renderer_diagnostics_tests.cpp), [Settings fixture](../../examples/settings/main.cpp) | A new staging branch and retained unrelated state |
| Deterministic CPU pixels and replay processing | [cpu_renderer](../../tests/render/cpu_renderer_tests.cpp), [replay](../../tests/replay/replay_tests.cpp), [offscreen_runner](../../tests/inspection/offscreen_runner_tests.cpp) | Image comparisons only for changed drawing behavior |

Add a test for a new failure mode, a new state/command path or an actual regression. Pointer and keyboard/gamepad routing share ordinary commands, so one representative parity check per consumer is enough.

## Validation tiers

| Tier | Scope |
| --- | --- |
| 0 Local | Build affected targets; run the affected owner tests and one consumer smoke. Documentation-only changes use the documentation checker. |
| 1 PR | One representative core environment, runtime/widget contracts with examples OFF. API/build/unknown changes add only the minimal independent core consumer, never copied examples. No example-specific change detection or PR example checks. |
| 2 Integration/periodic | Full core Debug/Release with examples ON, all example smokes and Diagnostics/Settings fixtures, independent add_subdirectory imports, deterministic CPU/replay checks. Runs after main integration or on a schedule. |
| 3 Release/platform | Version/tag/changelog consistency, full Debug/Release and add_subdirectory imports, plus optional module, GPU/font/native-host evidence for the supported configuration. FetchContent is manual when its path is affected or recertified. Unavailable hardware checks are unvalidated, never replaced by compile success. |

Repeated Tier 2 failure blocks the next release and takes repair priority. The [development guide](development.md#release-workflow) owns release commands.

### Change selection

| Change | PR/local selection |
| --- | --- |
| Documentation only | Documentation checker |
| C++ authoring/widget | Authoring, affected widget contract and one consumer |
| Commands/input/reactive | Affected owner contracts, representative integration and replay |
| Render/layout | Affected contracts, a CPU fixture and one consumer |
| Example sources/fixtures | Local: affected example smoke/fixtures. PR: core contracts only; examples are checked after main integration |
| Public headers | Core contracts and minimal independent add_subdirectory consumer; application API-usage regressions are detected by integration |
| CMake/import configuration | Core contracts and minimal independent add_subdirectory consumer, not copied applications; manually check FetchContent when its import path is affected |
| Vulkan/fonts/native host | Affected module build and dedicated rendering/font/platform checks |
| Release metadata/workflow | Metadata validation and the minimum affected build configuration |

Core CTest labels: `contract` for runtime/widget owner checks, `smoke` for quick application/consumer runs, `fixture` for the long Diagnostics/Settings fixtures, and `import` for independent host configure/build/link/run checks. [Runtime registration](../../cmake/TesseraTesting.cmake) and [consumer registration](../../tests/cmake/register.cmake) own membership; the [CI procedure](development.md#ci-scopes-and-reproducible-measurements) selects matching build groups and labels without removing tests. The standard PR `core` suite runs only `contract`; API/build/unknown changes and missing diffs add the `consumer` suite, selecting only `source_consumer_add_subdirectory`. Both keep examples OFF. All example smokes/fixtures and copied application imports run in Debug/Release integration even when those files changed in the PR; discovering those regressions after merge is an intentional trade-off. Documentation-only PRs keep the reporting jobs but skip native build/test phases. Existing manual suites remain unchanged.

Automatic import checks cover add_subdirectory only. FetchContent remains supported by the source projects and manual [consumer procedure](development.md#source-consumption-workflow), but is not registered even in an unfiltered local or integration CTest run. Historical evidence for both modes is not continuous regression coverage.

The Settings Windows host adds an opt-in `native` label when `TESSERA_TEST_NATIVE_HOST=ON`. Its smoke creates and presents a desktop window, routes window messages and checks accepted application state; run it in a desktop session under the [starter procedure](settings-starter.md#run-in-a-windows-window). Default headless/import runs keep the no-argument consumer check. Injected window messages establish host wiring; physical-device and IME evidence remain separate.

### Measurement and safe reduction

Before changing required checks or removing a duplicate test, compare available CI durations, identify equivalent owner coverage and verify the replacement detects the regressions the old check did. Keep slow substantive checks in wider tiers; a temporary flaky-check quarantine needs a reason, owner and return condition. [Development](development.md#focused-checks-and-timing) owns collection commands; support owns material baselines.

Reduce redundant work before reducing coverage: measure configure, compile/link and CTest separately, and distinguish nested consumer builds, configurations, fresh/reused builds, local/hosted execution and parallelism. Profile expensive fixture host rebuilds before changing observation frequency; prefer test-side reuse of one accepted view and representative CPU/replay observations over test-only runtime caches. Keep independent scenarios separately schedulable with cost hints. Do not remove import `RUN_SERIAL` without verifying isolated directories, shared caches, build processes and resource usage; evaluate consumer deduplication separately from concurrency. Keep Debug PR checks and full Debug/Release integration until equivalent detection is established. Refine file-level selection only if measurements still justify its maintenance cost; preserve conservative fallback and avoid new fixed timing gates.

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

[Runtime registration](../../cmake/TesseraTesting.cmake) and [consumer registration](../../tests/cmake/register.cmake) own core checks. Subsystem pages link their focused checks; the [development guide](development.md) owns execution commands, and [support](../reference/support-matrix.md) owns dated outcomes/configurations. Checks should remain active in Release as well as Debug.

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

The [checker](../../scripts/check-docs.ps1) validates relative file/directory links, Markdown heading fragments, code fences, canonical ownership links and architecture core-invariant sections. External links are not checked. Runtime tests are unnecessary for documentation-only changes.

Keep technical details in one subsystem page, active scope in current, delivery history in changelog, and capability/configuration evidence in support ([canonical owners](../README.md#canonical-owners)).
