# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Implement the bounded renderer telemetry presentation under the [reference panel scope](#renderer-diagnostics-reference-panel). The [source review](../reference/support-matrix.md#renderer-telemetry-authoring-review--2026-10-17) selects ordinary composition and a snapshot host; the [Proposed fixture boundary](../integrations/renderer-diagnostics.md#proposed-telemetry-fixture-boundary) owns declared data, identities, history geometry and lifetimes. Use the [Settings starter](../guides/settings-starter.md) for copyable composition and the [Settings scrolling boundary](../integrations/settings-app.md#implemented-constrained-scrolling-boundary) for host offsets and focus reveal.

Implementation scope:

- Provide reusable telemetry composition, owned backend-neutral snapshot input and a small synthetic host. Compose summary, timing/counter rows, a bounded bar history with textual summary and disclosure Buttons through existing Text/Row/Column/styles. Keep sampling, aggregation and renderer SDKs host-owned; introduce no chart/table widget or paint API without a demonstrated fixture blocker.
- Connect disclosure commands, injected refresh, normalized scrolling and keyboard/gamepad focus reveal to one staged full-tree presentation. Retain accepted snapshot, disclosure, focus and applied offsets on rejected preparation; telemetry refresh executes no renderer command.
- Supply copyable panel/host sources and a guide, extending the existing independent consumer driver only as needed to build those exact files through both local import forms using only `tessera::core`.

Acceptance and stop condition:

- Inspect exact values/units for declared snapshots A/B and unavailable-GPU/empty-history C, including measured zero, stable timing/counter/sample identities and history geometry/text from the same accepted snapshot. Reject malformed snapshot candidates before publication with actionable diagnostics.
- Exercise disclosure through pointer, keyboard/gamepad and semantic requests with ordinary once-consumed command eligibility. Refresh while a lower disclosure owner holds focus and an applied offset is nonzero; retain disclosure, author-ID focus and valid offsets, clamping/revealing only when geometry requires it. A collapsed section removes its rows without changing the injected snapshot.
- Verify constrained clipping/hit/inspection agreement, lower-control navigation/reveal, stale/disabled disclosure rejection and injected measurement/paint failure preservation with a fresh successful retry. Repeated CPU captures and accepted-view replay agree in geometry, semantic records, paint and image bytes under the declared fixture inputs. Replay reconstructs accepted views rather than executing the C++ host.
- Build/run affected checks and the copied independent core-only consumer in Debug and Release; link existing authoring/layout/input/command/paint/inspection/replay regressions instead of duplicating their harness. Record exact configuration/results only in support, consumer contracts in the integration owner and copy/run procedure in the guide.

Stop when this telemetry operation and independent consumption meet acceptance, then review the representative-control operation before implementation. Numeric editing, renderer-setting controls, native integration, generalized tables/plots and Settings-starter scrolling onboarding are outside this slice.

### Remaining milestone scope

Continue reference-application authoring in consumer order, including the renderer diagnostics panel below, with a bounded acceptance condition before each implementation slice:

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Renderer diagnostics reference panel

Include the ability to author and operate a practical renderer diagnostics UI through Tessera in this milestone. The [consumer design](../integrations/renderer-diagnostics.md) owns the data/command boundary and the reference inventory; the [source review](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17) owns provenance and evidence limits. Begin with the telemetry slice above; sequence the following bounded panel work without implementing the entire external overlay inventory at once.

- **Telemetry presentation:** build reusable C++ composition with injected backend-neutral snapshots for scene/device/extent, frame time/FPS/p99, CPU/GPU timing rows, draw/upload/session counters and a bounded frame-history plot with textual summary. Add collapsible groups and constrained scrolling through ordinary composition and host state. Acceptance: two declared snapshots and an unavailable-GPU/empty-series case produce exact inspectable values; a refresh preserves disclosure, focus and applied scroll state, issues no renderer command, and publishes coherent geometry/semantics/paint. Keyboard/gamepad navigation reaches and reveals the lower controls; repeated CPU captures and accepted-view replay agree.
- **Representative controls:** connect supported MSAA choices, outlines and scene-light toggles, direct/ambient strength, key-direction components, material-view selection and reset to a synthetic renderer host through ordinary commands. Define the minimum numeric control's entry/adjustment, step, bounds, commit and cancellation behavior before implementation. Acceptance: pointer, keyboard/gamepad and semantic operation produce equal accepted host values with once-only requests; invalid numeric text/non-finite/out-of-range values, unsupported choices and disabled/stale owners reject. Telemetry refresh retains pending numeric input under the declared policy; rejected staging retains accepted settings/presentation. Synthetic panel/camera arbitration consumes each input once.
- **Copyable integration boundary:** provide reusable panel source, a small snapshot/command host example and a walkthrough that an independent core-only consumer builds/runs in Debug and Release. Keep renderer sampling, aggregation, GPU objects, scene data and execution host-owned; no Hydra/OpenUSD/ImGui dependency enters Tessera core. Record affected contract checks, deterministic observations and the exact synthetic/native evidence scope in support.

Stop when these diagnostics and representative controls can be expressed, operated and verified through the same authoring/runtime model as Settings. A live hydra-toon migration, full material/morph/playback/skeleton/avatar editors, scene-space bone drawing, native dialogs, docking, general table/plot/virtualization frameworks and complete ImGui parity are separate integration scope. Promote only a demonstrated minimum widget or paint change needed by the selected panel operation; numerical telemetry in the fixture is not a renderer performance claim.

### Release exit criteria

- A developer can create a small application through a high-level C++ API without low-level runtime assembly for ordinary UI composition; permanent syntax stability is not required.
- A basic component/widget library supports the reference application, including Text, Button, Row, Column, Checkbox and bounded TextInput. A complete widget inventory is not required.
- State and derived state have a simple public interface; reusable components support typed props/defaults, children, local state, command output, style attachment and stable identity where used. Conditional and repeated UI use the same runtime model.
- Common application styles are expressed without manually constructing low-level style machinery, and still resolve through the shared cascade/theme model.
- At least one application-shaped, copyable example exercises editing, scrolling, selection, styling, layout and host interaction through ordinary normalized input. Keyboard/gamepad navigation and command eligibility follow the common input boundary; platform claims require their own evidence.
- A copyable renderer diagnostics panel meets the bounded telemetry, history-plot, representative-control and independent host acceptance above. Host snapshot refresh and commands share coherent full-tree publication, retained interaction state and common input eligibility; renderer SDKs remain outside core.
- Deterministic replay, inspection and CPU-reference validation reproduce the application's accepted state, semantic observations, geometry and paint under declared inputs. Existing contract regressions remain covered.
- The runtime has one execution model for C++, documents, editor preview and automation. Run affected checks in Debug and Release and documentation checks; put configuration/results only in support and delivery history in the [changelog](../../CHANGELOG.md).

### Non-goals and stop condition

Do not make generalized lifecycle redesign, a new reactive graph, arbitrary reload migration, full reflection, generalized resource providers, complete Portal/OverlayRoot, full CSS/Grid, retained/incremental rendering, generalized multithreading, stable C ABI, Python/Rust/Go bindings, browser/WebGPU hosting, plugin frameworks, complete native accessibility, IDE tooling or a visual editor prerequisites for this release.

Stop milestone expansion when ordinary application authoring and the reference workflow meet these exit criteria. The [broader widget candidates](backlog.md#widget-candidates-beyond-the-reference-application), [editor extensions](backlog.md#unscheduled-editor-extensions), other language/platform work and performance candidates stay inactive until a concrete consumer promotes a bounded operation.
