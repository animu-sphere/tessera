# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Connect one **scene-lights Boolean operation** in the copyable [panel/host](../guides/renderer-diagnostics.md), using the [Proposed consumer policy](../integrations/renderer-diagnostics.md#proposed-scene-lights-control-boundary) and [source review](../reference/support-matrix.md#renderer-scene-lights-control-review--2026-10-17). Prove that a second controlled toggle can change independently of outlines, MSAA and a pending direct-strength draft through ordinary composition and commands.

Bounded implementation acceptance:

- Compose one controlled Scene lights Checkbox with a stable author identity under controls disclosure. Add owned accepted/eligibility observations, parameter-free toggle and exact-field Boolean set commands; reject numeric/string coercion, extra activation arguments, wrong fields, hidden/disabled owners, stale or foreign presentations and consumed sequences before another handler entry. Use existing authoring/runtime APIs.
- From identical initial settings, pointer release, raw keyboard navigation/activation, logical gamepad Navigate/Activate and semantic activation must each accept false with one setting attempt/change and no camera delivery. Equal-value set must publish one presentation with one attempt and no accepted value change. Prove scene lights and outlines eligibility in both directions independently of numeric/MSAA eligibility; changing either toggle must preserve every unrelated setting.
- Retain exact pending direct-strength bytes (including invalid `2.`), reverse selection, owner/revision, numeric accepted value, MSAA capabilities/eligibility and outlines state across toggles, A/B/C telemetry refresh, disable/re-enable and collapse/re-expansion. Preserve eligible focus by author ID; collapse from the new control must recover/reveal the controls header, with ordinary clamping and navigation.
- Inject measurement/paint failures for toggle, node-free set and both eligibility candidates. Preserve all accepted settings, eligibility, drafts, disclosure, command generation, document/styles, inspection, geometry, paint and focus/offset. An entered setting failure counts one attempt and consumes its sequence; a fresh request retries once. Eligibility and read-only refresh issue no setting request/change.
- Extend synthetic panel/camera assertions for disabled hits, pointer replacement through release/cancel and consumed raw/pointer preparation errors. Reuse accepted-view semantic/geometry/paint and repeated CPU/replay comparisons; command execution and camera accounting require separate host assertions.
- Extend the exact reusable sources, small copied main and working guide together; run both core-only local source imports and affected checks in Debug/Release plus documentation checks. Record configuration, outcomes and synthetic evidence limits only in support and delivery in the changelog.

Stop after this one scene-lights connection and return to review of the next consumer operation. Ambient/vector-strength, material-view, reset, native renderer integration, generalized numeric/slider/input frameworks, tables/plots and Settings-starter scrolling onboarding require their own selection.

### Remaining milestone scope

Continue reference-application authoring in consumer order, including the renderer diagnostics panel below, with a bounded acceptance condition before each implementation slice:

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Renderer diagnostics reference panel

Include the ability to author and operate a practical renderer diagnostics UI through Tessera in this milestone. The [consumer design](../integrations/renderer-diagnostics.md) owns the data/command boundary and the reference inventory; the [source review](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17) owns provenance and evidence limits. Use the telemetry consumer boundary for the following bounded panel work without implementing the entire external overlay inventory at once.

- **Representative controls:** connect supported MSAA choices, outlines and scene-light toggles, direct/ambient strength, key-direction components, material-view selection and reset to a synthetic renderer host through ordinary commands. Define the minimum numeric control's entry/adjustment, step, bounds, commit and cancellation behavior before implementation. Acceptance: pointer, keyboard/gamepad and semantic operation produce equal accepted host values with once-only requests; invalid numeric text/non-finite/out-of-range values, unsupported choices and disabled/stale owners reject. Telemetry refresh retains pending numeric input under the declared policy; rejected staging retains accepted settings/presentation. Synthetic panel/camera arbitration consumes each input once.
- **Copyable control integration:** extend the panel's synthetic host and walkthrough with the selected controls, building/running the copied core-only consumer in Debug and Release. Keep renderer sampling, aggregation, GPU objects, scene data and execution host-owned; no Hydra/OpenUSD/ImGui dependency enters Tessera core. Record affected contract checks, deterministic observations and the exact synthetic/native evidence scope in support.

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
