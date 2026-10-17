# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Implement one outlines Boolean operation in the copyable [panel/host](../guides/renderer-diagnostics.md), following the [Proposed outlines policy](../integrations/renderer-diagnostics.md#proposed-outlines-control-boundary). The [source review](../reference/support-matrix.md#renderer-outlines-control-review--2026-10-17) selects the minimum host connection through the existing controlled Checkbox, command executor and full-tree publication boundaries; it identifies no required core extension.

Bounded implementation acceptance:

- Compose one stable outlines Checkbox under controls disclosure, with host-owned accepted Boolean initially false and independent eligibility. Parameter-free activation inverts the accepted value; an exact Boolean node-free request sets only outlines. Use the shared executor and sequence stream, with no new widget, semantic role, serialized field or renderer dependency.
- Prove pointer release, normalized keyboard traversal/activation, logical gamepad navigation/activation and semantic activation reach the same accepted value with one handler attempt and one accepted change from the same initial state. Typed equal-value requests accept a presentation with one attempt and zero changes; reject wrong types, field identity, extra activation arguments, stale/foreign owners or presentations, hidden/disabled targets and consumed sequences before handler entry.
- Preserve exact pending direct-strength draft bytes, selection, owner/revision, accepted strength and MSAA state across outlines changes, telemetry refresh and outlines eligibility changes. Eligibility is independent in both directions. Retain eligible focus by author ID, recover collapsed outlines focus to the controls header, reveal recovered focus and clamp scroll through ordinary geometry.
- Inject measurement and paint failures for toggle, typed set and eligibility candidates. Preserve every accepted setting, eligibility, draft, disclosure, command generation, document, focus/offset, inspection, geometry and paint; bound located diagnostics. An entered failed setting handler counts one attempt, consumes its sequence and makes no accepted change; a fresh request retries successfully. Eligibility, refresh and disclosure issue no setting request.
- Verify panel/camera arbitration once per input, including disabled hits, raw activation without duplicate logical activation, press/replacement/release or cancel, and consumed preparation errors. Extend stable observations and repeated CPU/replay comparisons in the existing fixture; keep host command/camera assertions separate from accepted-view replay reconstruction.
- Update the exact reusable sources, copied main and working guide together. Build/run both independent core-only imports and affected authoring, commands, input/focus/scroll, layout, inspection, paint, CPU and replay checks in Debug and Release; record configuration and synthetic evidence limits only in support.

Stop when the one outlines operation and copyable host meet these observable criteria. Return to consumer review before selecting scene lights, ambient/vector strength, material view or reset. Native renderer integration, generalized numeric/slider/input frameworks, tables/plots and Settings-starter scrolling onboarding require their own selection.

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
