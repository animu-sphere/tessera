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

## Documentation verification

Use the [documentation command](development.md#documentation-checks). The [checker](../../scripts/check-docs.ps1) checks local links/anchors, fences, reachability from the docs index and architecture invariants; external URLs are not checked. Docs-only changes need no runtime tests.
