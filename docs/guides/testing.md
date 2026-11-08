# Testing strategy

Test algorithms and boundaries heavily; appearance selectively. Prefer deterministic numeric/event assertions to a screenshot catalog. Commands are in [development](development.md); executed results belong in PR/CI, current scope and limits in [support](../reference/support-matrix.md).

## Verification ownership

Run the owner tests for changed guarantees and one consumer smoke for the application operation. Do not duplicate unchanged runtime proofs in every consumer.

| Property | Owner tests | Consumer check |
| --- | --- | --- |
| Once-only consumption and stale commands | [commands](../../tests/commands/commands_tests.cpp) | Intended handler runs once |
| Draft/selection and revision rejection | [text edit](../../tests/input/text_edit_tests.cpp) | Field routing and independent-field retention |
| Focus, pointer cancellation and scroll | [focus](../../tests/input/focus_tests.cpp), [pointer](../../tests/input/pointer_tests.cpp), [scroll](../../tests/input/scroll_tests.cpp), [scrollbar](../../tests/input/scrollbar_tests.cpp) | Reachable application operation |
| Modal scope and bounded dialog geometry | [modal](../../tests/input/modal_tests.cpp) | [Reset confirmation](../../examples/modal-dialog/main.cpp) |
| Collection order and logical ancestry | [collection](../../tests/input/collection_tests.cpp) | [Browser](../../examples/tree-list/main.cpp) selection/details |
| Menu triggers, placement and dismissal | [menu](../../tests/input/menu_tests.cpp), [popup](../../tests/input/popup_tests.cpp) | [Asset actions](../../examples/menu-context/main.cpp) |
| Accepted settings after rejected staging | [Diagnostics](../../tests/integrations/renderer_diagnostics_tests.cpp), [Settings](../../examples/settings/main.cpp) | New branch and retained unrelated state |
| CPU pixels and replay | [CPU](../../tests/render/cpu_renderer_tests.cpp), [replay](../../tests/replay/replay_tests.cpp), [runner](../../tests/inspection/offscreen_runner_tests.cpp) | Images only for changed drawing behavior |

Add tests for new failure modes, state/command paths and actual regressions. One representative pointer versus keyboard/gamepad parity check per consumer is enough when both share ordinary commands.

## Validation tiers

| Tier | Scope |
| --- | --- |
| Local | Affected targets, owner tests and consumer smoke; docs-only changes use the docs checker |
| PR | Representative core Debug contracts, examples OFF; API/build/unknown changes add the minimal independent core import |
| Integration/periodic | Full core Debug/Release, examples/fixtures, add_subdirectory imports and CPU/replay |
| Release/platform | Metadata consistency and full Debug/Release, plus optional module/GPU/font/native evidence needed by the release claims |

Repeated integration failure takes repair priority before the next release. Unavailable hardware checks are unvalidated, not replaced by compile success. FetchContent is manual when its import path changes or is recertified.

### Change selection

| Change | Local selection |
| --- | --- |
| Documentation | [Documentation checks](development.md#documentation-checks) |
| Authoring/widgets | Authoring, affected widget owner and one consumer |
| Commands/input/reactive | Affected owners, representative integration and replay |
| Render/layout | Affected owners, CPU fixture and one consumer |
| Examples | Affected smoke/fixtures; PR CI remains core-only |
| Public headers / CMake / imports | Core contracts and minimal independent host; manually exercise changed FetchContent paths |
| Vulkan/fonts/native host | Affected module and rendering/font/platform checks |
| Release metadata/workflow | Metadata and affected configuration |

[Runtime registration](../../cmake/TesseraTesting.cmake) and [consumer registration](../../tests/cmake/register.cmake) own membership. [Development](development.md#ci-scopes-and-reproducible-measurements) owns labels, suite selection and CI commands. Copied applications run in integration rather than PR CI; example-only regressions may first appear after merge.

The opt-in Settings `native` smoke requires a desktop and verifies injected message routing, not physical-device/IME behavior. See the [starter procedure](settings-starter.md#run-in-a-windows-window).

The [Hierarchy / Inspector](inspector-hierarchy.md) operation has one window-free consumer smoke, `inspector_hierarchy`, plus the opt-in `inspector_hierarchy_native` injected-message check. Appearance coverage compares actual decoration pixels for hover, press, selection and focus at four scales, checks stable row geometry, cancellation/once-only selection and stationary Slider feedback, and verifies that the full track fits the minimum pane width. Glyph commands are omitted only in these decoration comparisons; the native smoke presents the complete form. Name-input rejection coverage checks retained state/focus/inspection, text/deletion/selection staging failures and retry; the native check verifies caption feedback and recovery at the input limit. Clipboard coverage uses injected host services for selection/routing, rejection/retry and reentrant target changes, including cut write failures and rejected deletion after successful copying; native coverage checks Ctrl+C/X/V and strict Unicode conversion without changing the live OS clipboard. The two application native checks run serially because they share desktop focus and OS pointer capture. Existing widget owners retain the runtime proofs; the consumer checks retained per-item edits, actual scrolling/resizing, command wiring and shared observations rather than duplicating those contracts.

### Measurement and safe reduction

Measure configure, compile/link and CTest separately; distinguish fresh/reused builds, local/hosted execution, configurations and nested consumer builds. Keep logs, JUnit and command/exit-code evidence in PR/CI. A timeout is not an elapsed measurement.

Before removing redundant tests or changing required checks, identify equivalent owner coverage and verify replacement detection. Keep substantive slow checks in wider tiers. Quarantine needs a reason, owner and return condition. No fixed observation period or merge-time budget is imposed.

Profile expensive fixture setup before reducing observations; prefer test-side reuse over runtime caches. Evaluate consumer deduplication separately from concurrency, retaining `RUN_SERIAL` until directories, caches, processes and resources are independent. Refine file-level selection only when measurements justify it, with conservative fallback.

## High-value checks

Choose boundary and rejection cases from the affected [subsystem contract](../README.md#architecture) and its source-linked tests. Keep checks active in Release as well as Debug. Serialized fixtures assert semantics, not just formatting; incremental paths compare with full-tree references.

## Replay fixtures

Use the [replay contract](../architecture/replay.md) for controlled time/readiness and observable outputs. Start with geometry, semantics, actions and paint commands; use GPU images only for behavior those cannot prove.

## Evidence by capability kind

- Data/serialization: round trips, bounds and located rejection without a GPU SDK.
- Layout/input: numeric geometry, eligibility, lifecycle and normalized dispatch.
- Rendering: actual pixels with declared backend/device/font/tolerance; shader compilation is insufficient.
- Native adapters: their own platform smoke; injected and physical input are distinct.
- Source consumption: independent public-alias configure/build/link/run, host testing ownership and core-only dependency exclusion. Optional linkage is not GPU execution.

## Inspection and snapshot fixtures

Use in-process [inspection](../architecture/inspection.md) before transport wrappers. Select scoped/stale/ambiguous targets, coherent-generation observations and rejected state injection. Prefer numeric invariants over images; unavailable observations must not look successful. Bundle/transport/browser fixtures become relevant when those mechanisms are implemented.

## Localization fixtures

Select cases from [localization](../architecture/localization.md) and [text](../architecture/text.md): invalid locale/catalog inputs, fallback/missing distinctions, atomic switching and retained edits/focus. Fixed default, expanded and RTL pseudo locales cover geometry/direction without multiplying screenshots per widget; pseudo RTL does not replace mixed-script shaping tests. Fix locale data, fonts and formatting inputs for replay.

## Image regression and performance

Use a small primitive/glyph reference set with explicit size, scale, fonts, backend and tolerances. Compare GPU paths against the scalar CPU reference where applicable, and optimized CPU paths against scalar output. Do not silently substitute CPU for failing GPU execution.

The [latest-UI GPU procedure](development.md#latest-ui-gpu-regression) owns the application-state inventory, pixel oracle and local driver; its placeholder offscreen captures do not establish native input or real-font coverage.

Performance follows correctness and a representative application bottleneck. Report workload, changed work versus total size, build/hardware/backend/fonts, warmup/samples and timing noise; separate deterministic counters from timing. Optimization must retain reference output, not merely improve a benchmark.

### Look and effect verification

For [Look extensions](../proposals/look.md), select the owners of the changed guarantees and one representative consumer:

- Core validation: finite geometry, gradient stops, parameter types/bounds and ordered clip/transform behavior.
- Built-in CPU/reference: coverage, thin strokes, small sizes and alpha, including declared gradient interpolation.
- Analytic fill owners: `style_sheet`/`theme` for rectangle/ellipse, noninheritance, typed shape/boolean tokens and explicit default replacement; `draw_list`/`paint` for invalid enums, owned shape/opacity, gradients and unchanged layout/borders; `cpu_renderer` for exact circle and half/quarter-pixel coverage, clamped radii, empty boxes, transparent gradients, whole-pixel clips and independent four-scale ellipse equations; `replay_serialization` for initial/reloaded v1/v2 styles, malformed flags and unchanged default encodings. `inspector_hierarchy` checks smooth card/status decoration pixels and accepted Enabled updates with stable geometry. `vulkan` compares reference/batch/repeat with CPU at four scales within unchanged 2/255 linear-channel error, including sample grids, gradients, hard ellipse center reconstruction, mirror/skew/rotation, precision rejection/retry and retirement.
- Procedural fill owners: `style_sheet` for noninheritance, explicit clearing and located validation; `draw_list`/`paint` for identity/parameter/time/rate/opacity bounds, one command carrying shape, coverage, faded fallback and multiplied opacity, omission of invisible requests, phase + rate * host-time evaluation with one rounding, static fills ignoring host time, unchanged layout/geometry, `time_overflow`/nonfinite host-time rejection and `time_dependent`; `cpu_renderer` for fallback pixels identical to the plain color/gradient fill and the fallback count; `replay_serialization` for typed v1/v2 round trips including time rates, unknown kinds and unchanged legacy and static-fill bytes. `vulkan` registers build-time fills on reference and batch paths: constant-fill pixels within 1/255 of solid fills across shapes, coverage, clip/rotation and opacity at four scales; numeric ABI probes; fallback/omission/gradient fallback with counters; registration, kind/count and identity rejection; batching and retirement. `vulkan_menu_smoke` compares the moving aurora backdrop with an independent evaluation at each capture's evaluated time from a scripted clock, rejects fallback and unchanged motion between captures, and checks that a minimized window presents nothing and pauses its clock.
- Stroke owners: `style_sheet`/`theme` for local outlines, typed tokens and clearing; `draw_list`/`paint` for validation, outside expansion/order/opacity and unchanged layout; `cpu_renderer` for exact half/quarter-pixel and sample-boundary coverage, clamped/collapsed corners, continuous dash phase, clipping/mirroring and alpha; `replay_serialization` for initial/reloaded v1/v2 styles and unchanged legacy bytes. `inspector_hierarchy` checks solid control and dashed row focus pixels, stable geometry/data and blur removal. `vulkan` compares reference/batch/repeat with CPU at four scales within the unchanged 2/255 linear-channel tolerance, including exact sample-grid edges, rotation/skew, shader-precision rejection and frame retirement.
- Gradient owners: `style_sheet`/`theme` for linear/radial replacement, cascade, owned stops, clearing and invalid values; `draw_list`/`paint` for geometry, stop ordering/ranges, replacement/order and per-stop opacity; `cpu_renderer` for exact linear/premultiplied sampling, elliptical radial distance, nonuniform/duplicate offsets, clamping, rounded coverage and transforms/clips; `replay_serialization` for both kinds in v1/v2 and unchanged legacy bytes. `inspector_hierarchy` verifies radial card pixels against two-color and linear substitutes; `vulkan` compares reference/batch/repeat with CPU at four scales without weakening its 2/255 linear-channel tolerance, including 65-stop and exact-boundary hard transitions, rotated/skewed radial fills, precision rejection and partial/full buffer retirement.
- Backend: actual pixels for rounded/clip edges, color/opacity and fractional device scale; shader diagnostics, capability/fallback behavior and completion-safe resource lifetime.
- Shadow owners: `style_sheet`/`theme` for noninheritance, outer/inset replacement/clearing and typed/invalid tokens; `draw_list`/`paint` for outer/inset bounds, cutout offsets, fill/shadow/border order, multiplied opacity, clips and unchanged allocation; `cpu_renderer` for exact finite outer/inset distance falloff, hard/clamped/empty boxes, distant cutouts, alpha, clipping and transforms; `replay_serialization` for initial/reloaded v1/v2 styles, boolean inset validation and unchanged legacy outer bytes. `inspector_hierarchy` checks visible pixels below the card and inside the white name input, with retained outside/center pixels and focus/hover geometry. `vulkan` compares reference/batch/repeat with CPU at four scales within 2/255, including clipping, mirror/skew/rotation, rejection/retry and batch retirement; the inset fixture uses byte-aligned midtone fills under the [declared quantization conditions](../architecture/rendering.md#implemented-box-shadows).
- Host/replay: explicit evaluation time, seed, uniform values and shader/asset identity for repeatable state and draw data. Add the necessary adapter coverage when those inputs become supported.

Compare CPU/GPU output where both implement the feature, using declared tolerances; a GPU-only shader does not require arbitrary CPU Slang execution. Check a fallback as a fallback, not as proof that the requested effect rendered. Prefer numeric SDF/coverage cases and a small image set over snapshots per widget or effect.

Use the existing validation tiers for visual showcases and GPU execution. Expand PR coverage or add performance thresholds when measured risk justifies them; unavailable GPU checks remain unvalidated, not replaced by compile-only success or a mandatory all-backend rollout.

## Documentation verification

Use the [documentation command](development.md#documentation-checks). The [checker](../../scripts/check-docs.ps1) checks local links/anchors, fences, reachability from the docs index and architecture invariants; external URLs are not checked. Docs-only changes need no runtime tests.
