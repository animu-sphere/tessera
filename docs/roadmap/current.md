# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Review and select **one remaining renderer-control consumer operation** for the copyable [panel/host](../guides/renderer-diagnostics.md). Use the [representative control inventory](../integrations/renderer-diagnostics.md#proposed-representative-control-boundary), [ambient-strength boundary](../integrations/renderer-diagnostics.md#implemented-ambient-strength-control-boundary) and [support evidence](../reference/support-matrix.md#renderer-ambient-strength-evidence--2026-10-26) to compare key-direction components, material-view selection and reset without activating all three.

Bounded review acceptance under the [four-item rule](../agent-policy.md#verification-ownership-and-bounded-acceptance):

- **Minimum observable operation:** choose one user operation, identify demonstrated authoring friction and define its smallest successful public-API flow in the consumer owner.
- **New risks:** declare only the selected operation's identities, validation and retained state. Reset needs an atomic all-setting/draft policy; vector editing needs component ownership/bounds without inferred renderer normalization. Add representative routing or staging rejection checks where that wiring changes.
- **Inherited guarantees:** cite the applicable [owner tests](../guides/testing.md#verification-ownership) and assumptions. Separate new consumer assertions from reused sequence, editing, focus, scroll and rendering guarantees; select affected checks under the tier policy. Keep review findings and execution evidence distinct in support.
- **Stop:** select one bounded implementation slice here, with detailed behavior in its consumer owner. Native renderer integration, generic numeric/input frameworks, tables/plots and Settings-starter scrolling onboarding need separate selection.

### Bounded validation supporting work

- **Minimum observable operation:** produce a reproducible CI/CTest baseline using the [measurement procedure](../guides/testing.md#measurement-and-safe-reduction), without changing runtime behavior or removing checks.
- **New risks:** distinguish configure/build/test/queue time, local versus hosted conditions, actual failures versus reruns, and missing historical or required-check data. Classify owner coverage and design a few contract/smoke/import labels before using exclusions.
- **Inherited guarantees:** preserve registered tests and the release gate while measuring; retain its FetchContent filter and include both import modes in the baseline inventory. Record measurements, source review and limits only in support.
- **Stop:** baseline conditions/artifacts, owner inventory, missing observations and label design are reviewable. Workflow separation, gate migration and duplicate-test reduction stay in [backlog](backlog.md#validation-and-ci-follow-ups) until their prerequisites justify the next bounded change; this work must not block the selected UI operation.

### Remaining milestone scope

Continue reference-application authoring in consumer order, including the renderer diagnostics panel below, with a bounded acceptance condition before each implementation slice:

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Renderer diagnostics reference panel

Include the ability to author and operate a practical renderer diagnostics UI through Tessera in this milestone. The [consumer design](../integrations/renderer-diagnostics.md) owns the data/command boundary and the reference inventory; the [source review](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17) owns provenance and evidence limits. Use the telemetry consumer boundary for the following bounded panel work without implementing the entire external overlay inventory at once.

- **Representative controls:** promote one remaining control from the [consumer inventory](../integrations/renderer-diagnostics.md#proposed-settings-and-capability-observations) at a time. Define its minimum entry/selection, validation and commit/cancellation behavior in that owner. Prove one successful flow and its new routing, identity, independent-state or staging risk; inherit unchanged runtime guarantees with owner-test references rather than repeating all failures for all input routes.
- **Copyable control integration:** extend the synthetic host and working walkthrough through public APIs. Keep renderer sampling, aggregation, GPU objects, scene data and execution host-owned. Run affected consumer checks per the [tier policy](../guides/testing.md#validation-tiers), reserving full configurations and both imports for wider integration/release validation unless the change affects those boundaries. Record exact executed and inherited evidence in support.

Stop when these diagnostics and representative controls can be expressed, operated and verified through the same authoring/runtime model as Settings. A live hydra-toon migration, full material/morph/playback/skeleton/avatar editors, scene-space bone drawing, native dialogs, docking, general table/plot/virtualization frameworks and complete ImGui parity are separate integration scope. Promote only a demonstrated minimum widget or paint change needed by the selected panel operation; numerical telemetry in the fixture is not a renderer performance claim.

### Release exit criteria

- A developer can create a small application through a high-level C++ API without low-level runtime assembly for ordinary UI composition; permanent syntax stability is not required.
- A basic component/widget library supports the reference application, including Text, Button, Row, Column, Checkbox and bounded TextInput. A complete widget inventory is not required.
- State and derived state have a simple public interface; reusable components support typed props/defaults, children, local state, command output, style attachment and stable identity where used. Conditional and repeated UI use the same runtime model.
- Common application styles are expressed without manually constructing low-level style machinery, and still resolve through the shared cascade/theme model.
- At least one application-shaped, copyable example exercises editing, scrolling, selection, styling, layout and host interaction through ordinary normalized input. Keyboard/gamepad navigation and command eligibility follow the common input boundary; platform claims require their own evidence.
- A copyable renderer diagnostics panel meets the bounded telemetry, history-plot, representative-control and independent host acceptance above. Host snapshot refresh and commands share coherent full-tree publication, retained interaction state and common input eligibility; renderer SDKs remain outside core.
- Deterministic replay, inspection and CPU-reference validation reproduce the application's accepted state, semantic observations, geometry and paint under declared inputs. Existing contract regressions remain covered.
- The runtime has one execution model for C++, documents, editor preview and automation. At integration/release, run full core Debug/Release, both independent consumer import modes, representative Settings/Diagnostics flows, deterministic CPU/replay checks and documentation checks under the [validation tiers](../guides/testing.md#validation-tiers). Optional/native claims require their declared platform evidence; persistent wider-suite failures block release. Put configurations/results only in support and delivery history in the [changelog](../../CHANGELOG.md).

### Non-goals and stop condition

Do not make generalized lifecycle redesign, a new reactive graph, arbitrary reload migration, full reflection, generalized resource providers, complete Portal/OverlayRoot, full CSS/Grid, retained/incremental rendering, generalized multithreading, stable C ABI, Python/Rust/Go bindings, browser/WebGPU hosting, plugin frameworks, complete native accessibility, IDE tooling or a visual editor prerequisites for this release.

Stop milestone expansion when ordinary application authoring and the reference workflow meet these exit criteria. The [broader widget candidates](backlog.md#widget-candidates-beyond-the-reference-application), [editor extensions](backlog.md#unscheduled-editor-extensions), other language/platform work and performance candidates stay inactive until a concrete consumer promotes a bounded operation.
