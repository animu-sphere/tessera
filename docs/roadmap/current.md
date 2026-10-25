# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Implement **one ambient-strength entry/adjustment/Apply/Cancel operation** in the copyable [panel/host](../guides/renderer-diagnostics.md), following the [Proposed ambient-strength boundary](../integrations/renderer-diagnostics.md#proposed-ambient-strength-control-boundary). Use ordinary numeric composition and shared commands; the [source review](../reference/support-matrix.md#renderer-ambient-strength-control-review--2026-10-25) identifies the consumer connections and evidence limits.

Bounded implementation acceptance:

- Compose the second named numeric field with accepted value, retained draft and independent eligibility. Follow the consumer owner's initial value, decimal grammar, bounds, step, canonicalization and cancellation policy. Preserve direct-strength source use while making composition, editing requests, command handling and focused input routing distinguish the two fields explicitly.
- Pointer entry/Apply/adjustment, raw keyboard entry/Enter/Escape, logical keyboard/gamepad traversal or Navigate/Activate/Cancel and semantic revisioned editing/activation reach the selected field through one executor and request sequence stream. Typed node-free sets validate exact field identity and numeric type, finite bounds and canonical byte limits. Reject cross-field owners even when revisions match, stale/foreign presentations, hidden/disabled fields, invalid drafts, wrong types and duplicate sequences.
- Exercise both fields with different incomplete drafts and reverse selections. Changing or cancelling either field preserves the other's exact accepted value, draft bytes, selection, lifetime owner, revision and eligibility. MSAA/capability and either Boolean operation preserve both fields. Telemetry A/B/C, independent disable/re-enable and controls collapse/re-expansion retain both drafts; collapse from either numeric field or its operation Buttons recovers/reveals the controls header. Preserve eligible author-ID focus and reveal lower operations through ordinary scrolling.
- Verify lower/upper clamping, dirty-draft adjustment rejection, equal-value Apply/set revision and attempt/change accounting, and cancellation without a setting request. Inject measurement/paint failure into ambient editing, Apply, adjustment, Cancel and eligibility staging: retain both fields, unrelated settings, disclosure, accepted command generation, focus/offset, document/styles, inspection, geometry and paint. An entered failed command consumes its sequence; a fresh request retries once. Diagnostics are bounded and field-located.
- Assert synthetic panel/camera consumption separately from accepted-view replay, including invalid Apply, disabled hits and claimed pointer streams through replacement/release/cancel. Extend deterministic semantic/layout/paint and repeated CPU/replay comparisons to both numeric observations; no consumed input produces a camera delivery or a second logical action.
- Extend the working copied main and guide together, build/run both local core-only imports in Debug and Release, and run affected authoring/editing/input/command/scroll/inspection/CPU/replay checks plus documentation checks. Record configuration, results and synthetic evidence limits only in support; update the consumer owner's maturity with implementation.

Stop when this one operation and two-field independence satisfy the acceptance above, then return to selecting one remaining consumer operation. Key-direction components, material-view selection, reset, native renderer integration, generalized numeric/slider/input frameworks, tables/plots and Settings-starter scrolling onboarding require their own selection.

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
