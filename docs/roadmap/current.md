# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Release convergence

v0.4.0 is released by fixing its exit criteria and producing the minimum evidence that each holds, not by adding features until it feels complete. A good v0.4.0 is a bounded, usable UI framework shipped with evidence, not a complete UI runtime.

- **Must:** the [release exit criteria](#release-exit-criteria). Do not delete, relax or mark one as met without linked evidence or an explicitly reviewed scope change.
- **Should, only when a Must flow is obstructed:** less authoring boilerplate, Row/Column sizing/alignment, a widget improvement an operation needs, simpler error messages and developer diagnostics, and small PR-speed fixes for a measured bottleneck.
- **Won't:** the [non-goals](#non-goals-and-stop-condition) below.

Until release, change code only for a reference-application user operation, a reproducible defect or a release blocker. New runtime/widget generalization is frozen; record noticed improvements in [backlog](backlog.md). Before any further implementation, state what a v0.4.0 user could not do without it; if that cannot be stated, defer it.

### Current slice

The [release gap audit](../reference/support-matrix.md#v040-release-gap-audit--2026-10-27) classifies every exit criterion. Remaining v0.4.0 work:

- **Must blocker:** the renderer diagnostics [reset](#reset) operation, as one minimum implementation.
- **Release-candidate checks:** the Tier 3 release run, and alignment of `VERSION`, the changelog and onboarding with the released source.

Classify a newly found defect with the same PASS / EVIDENCE GAP / BLOCKED / DEFER PROPOSED vocabulary before adding it here. **PASS** cites dated support evidence. An **EVIDENCE GAP** receives the minimum check, not new features. A **BLOCKED** item names the operation or required check that cannot proceed. **DEFER PROPOSED** requires explicit scope-change approval.

### Bounded validation supporting work

- **Minimum observable operation:** confirm that the latest main integration run completes, and compare hosted PR and full integration durations from the [PR and full integration procedures](../guides/development.md#ci-scopes-and-reproducible-measurements) with the [recorded baseline](../reference/support-matrix.md#ci-scope-and-timing-evidence--2026-10-26), verifying shorter PR feedback and retained Debug/Release/import coverage without changing runtime assertions. Check the provisional ten-minute PR p95 against those samples.
- **New risks:** distinguish configure/build/test/queue time, fresh versus reused builds, local versus hosted conditions and actual failures versus reruns. Check documentation-only reporting, conservative scope fallback and main-run failures. Record missing runner capacity/cost and hosted parallelism observations rather than assuming them.
- **Inherited guarantees:** preserve all registered checks and tag/manual release verification, including its FetchContent filter; full integration must exercise both imports. Keep required-check names/settings unchanged until parallel workflow evidence and protection settings support a coordinated transition; the manual release workflow supplies the old comparison path.
- **Stop:** record hosted scope/results in support and follow the two-week Tier 2 observation policy. A failing main integration is a release blocker. CI cleanup is not a v0.4.0 theme: caching, parallelism, target separation, gate/ruleset changes, daily scheduling and duplicate-test reduction require their own measured justification in [backlog](backlog.md#validation-and-ci-follow-ups). Rollout observation must not block the selected UI operation or delay a release without failures.

### Release sequence after the audit

1. **Minimum implementation:** resolve Must blockers in this order: reference-application completion, first-app authoring, regressions, then others. Each change covers one user operation or one defect under the [agent policy](../agent-policy.md#verification-ownership-and-bounded-acceptance), together with its minimum regression. Leave the runtime unchanged unless a defect is demonstrated. Ends when no Must blocker is open.
2. **Release candidate:** run the copyable first app and representative Settings/Diagnostics flows under conditions close to a fresh environment, run the Tier 3 checks in the [validation tiers](../guides/testing.md#validation-tiers), and record configurations/results in support. Align `VERSION`, the [changelog](../../CHANGELOG.md) and tag/release notes. Only release-blocker fixes enter during the candidate. Ends when every exit criterion is PASS and unverifiable items are stated without overclaiming support.
3. **Publish and observe:** publish v0.4.0 and continue the Tier 2 observation. Post-release scope follows [backlog](backlog.md#after-v040).

Report these convergence signals briefly with each audit, rollout or release-candidate record in support; they are signals, not exit criteria, and test counts or documentation volume are not progress: open Must blockers, per-operation lead time from selection to merge, PR CI p50/p95, runtime files touched per UI operation, completed reference-application operations, and main integration failures/reruns.

### Remaining milestone scope

Continue reference-application authoring only for operations the audit leaves as Must blockers, in consumer order, with a bounded acceptance condition before each implementation slice.

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Renderer diagnostics reference panel

Include the ability to author and operate a practical renderer diagnostics UI through Tessera in this milestone. The [consumer design](../integrations/renderer-diagnostics.md) owns the data/command boundary and the reference inventory; the [source review](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17) owns provenance and evidence limits. Use the telemetry consumer boundary for the following bounded panel work without implementing the entire external overlay inventory at once.

- **Representative controls:** the release gap audit leaves reset as the only remaining Must operation; decide each operation individually from the panel's need rather than through a shared framework. Define each promoted operation's minimum entry/selection, validation and commit/cancellation behavior in the [consumer inventory](../integrations/renderer-diagnostics.md#proposed-settings-and-capability-observations) owner. Prove one successful flow and its new routing, identity, independent-state or staging risk; inherit unchanged runtime guarantees with owner-test references rather than repeating all failures for all input routes.
- **Copyable control integration:** extend the synthetic host and working walkthrough through public APIs. Keep renderer sampling, aggregation, GPU objects, scene data and execution host-owned. Run affected consumer checks per the [tier policy](../guides/testing.md#validation-tiers), reserving full configurations and both imports for wider integration/release validation unless the change affects those boundaries. Record exact executed and inherited evidence in support.

#### Reset

Implement the **reset** operation in the copyable [panel/host](../guides/renderer-diagnostics.md) as one change, using the consumer inventory and the ordinary composition and host boundaries:

- **Minimum observable operation:** one Button and one ordinary command atomically propose the initial settings and reset all numeric drafts under the [consumer reset policy](../integrations/renderer-diagnostics.md#proposed-settings-and-capability-observations); the accepted observation shows every initial setting and canonical draft text.
- **New risks:** distinguish the all-settings atomic candidate and whole-operation eligibility/rejection from per-control behavior; account for MSAA capability membership of the initial sample count and revision advancement across all five numeric drafts, including pending invalid text. Keep renderer execution and acknowledgement host-owned.
- **Inherited guarantees:** cite relevant command, editing and host-staging owner checks and the implemented per-control consumer coverage. Record executed checks and evidence limits in support.
- **Stop:** one successful reset flow, its whole-operation rejection and one rejected-stage retry pass in the Diagnostics fixture; the copied main and guide show reset. Leave the runtime unchanged unless a defect is demonstrated. A generic reset/selector framework, native renderer integration, generic choice/numeric/vector/input frameworks, tables/plots and Settings-starter scrolling onboarding require separate selection.

Stop when these diagnostics and representative controls can be expressed, operated and verified through the same authoring/runtime model as Settings. A live hydra-toon migration, full material/morph/playback/skeleton/avatar editors, scene-space bone drawing, native dialogs, docking, general table/plot/virtualization frameworks and complete ImGui parity are separate integration scope. Promote only a demonstrated minimum widget or paint change needed by the selected panel operation; numerical telemetry in the fixture is not a renderer performance claim.

### Release exit criteria

- A developer can create a small application through a high-level C++ API without low-level runtime assembly for ordinary UI composition; permanent syntax stability is not required.
- A basic component/widget library supports the reference application, including Text, Button, Row, Column, Checkbox and bounded TextInput. A complete widget inventory is not required.
- State and derived state have a simple public interface; reusable components support typed props/defaults, children, local state, command output, style attachment and stable identity where used. Conditional and repeated UI use the same runtime model.
- Common application styles are expressed without manually constructing low-level style machinery, and still resolve through the shared cascade/theme model.
- At least one application-shaped, copyable example exercises editing, scrolling, selection, styling, layout and host interaction through ordinary normalized input. Keyboard/gamepad navigation and command eligibility follow the common input boundary; platform claims require their own evidence.
- Settings and the renderer diagnostics panel each demonstrate a representative display, input, commit (or cancellation where the application defines it) and redisplay flow through public APIs only.
- A copyable renderer diagnostics panel meets the bounded telemetry, history-plot, representative-control and independent host acceptance above. Host snapshot refresh and commands share coherent full-tree publication, retained interaction state and common input eligibility; renderer SDKs remain outside core.
- Deterministic replay, inspection and CPU-reference validation reproduce the application's accepted state, semantic observations, geometry and paint under declared inputs. Existing contract regressions remain covered.
- The runtime has one execution model for C++, documents, editor preview and automation. At integration/release, run full core Debug/Release, both independent consumer import modes, representative Settings/Diagnostics flows, deterministic CPU/replay checks and documentation checks under the [validation tiers](../guides/testing.md#validation-tiers). Optional/native claims require their declared platform evidence; persistent wider-suite failures block release. Put configurations/results only in support and delivery history in the [changelog](../../CHANGELOG.md).
- [Getting Started](../guides/getting-started.md), the [first-app walkthrough](../guides/first-app.md), support, the changelog and `VERSION` describe the released source consistently.

### Non-goals and stop condition

Do not make generalized lifecycle redesign, a new reactive graph, arbitrary reload migration, full reflection, generalized resource providers, complete Portal/OverlayRoot, full CSS/Grid, retained/incremental rendering, generalized multithreading, stable C ABI, Python/Rust/Go bindings, browser/WebGPU hosting, plugin frameworks, complete native accessibility, IDE tooling or a visual editor prerequisites for this release. Exhaustive async/reload migration, complete ImGui parity, complete table/plot/virtualization, generic selector/reset frameworks, performance optimization ahead of a measured need and native/GPU support claims without platform evidence are also outside v0.4.0.

Stop milestone expansion when ordinary application authoring and the reference workflow meet these exit criteria. The [broader widget candidates](backlog.md#widget-candidates-beyond-the-reference-application), [editor extensions](backlog.md#unscheduled-editor-extensions), other language/platform work and performance candidates stay inactive until a concrete consumer promotes a bounded operation.
