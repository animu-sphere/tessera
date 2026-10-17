# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from real applications: the [Settings application](../integrations/settings-app.md) and a [renderer diagnostics panel](../integrations/renderer-diagnostics.md) modeled on the hydra-toon viewport's ImGui UI. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Extend the copyable [Settings edit/Apply starter](../guides/settings-starter.md) with the bounded [section-selection operation](../integrations/settings-app.md#implemented-section-selection-boundary): repeated Account/Feature/About choices and one conditional branch, retaining the same draft/edit/save host. The [source review](../reference/support-matrix.md#settings-starter-section-and-scroll-review--2026-10-17) owns the onboarding findings and limits. Use existing public APIs and extend the two-file starter and its walkthrough rather than requiring readers to extract the regression fixture's host.

Minimum scope:

- Compose the fixed ordered `account`, `feature`, `about` choices as ordinary Buttons with stable `section-<id>` identities and a visible selected marker/name. Account contains username, Feature contains Checkbox, About contains help; summary, Apply and saved result stay outside the branch. Reuse the application classes and typed Field composition.
- Keep selected section, the accepted edit owner/revision/byte selection, draft and owned save copies in the host/session lifetime. Explicitly recompose the Element tree for a candidate branch before ordinary full-tree preparation under the [form contract](../architecture/ui-model.md#implemented-c-form-authoring). Stage selection with the existing observation restoration and accept/publish boundary. Section changes must not save or alter the draft.
- Route normalized committed text and supported editing keys only when a live eligible username owner exists and is focused; ignore raw editing input while Account is absent. Keep editing-first consumption, ordinary command eligibility/execution, increasing sequences and author-ID focus recovery. Use a fixed viewport large enough to show each branch and its controls without scrolling.
- Extend the walkthrough's copy/build/run procedure and expected observations. Reuse both independent copied-source import checks and the existing Settings regression for fault injection, stale owners, disabled commands and replay; avoid duplicating its regression harness in the starter.

Observable acceptance:

- From Account, navigate to username and insert `本` after `Ada`, observing byte selection 6:6. Navigate to Feature and activate its choice by keyboard; toggle true, reach Apply and activate by the logical gamepad route. Exactly one save owns `{Ada本, true}`.
- Select About by logical gamepad activation, then return to Account through ordinary navigation/activation. Each selected choice retains focus across its replacement, only the selected branch appears, and returning to Account exposes the exact retained edit owner/revision/selection. Editing input while Account is absent changes no draft, edit or save observation and executes no edit command.
- Insert `!` after returning to username. The draft/summary become `Ada本!` / true while the first save and displayed `Saved 1` remain unchanged. Keyboard Apply appends exactly one second owned copy `{Ada本!, true}`. Section changes themselves append none.
- Accepted author-ID inspection agrees with selected marker/name, conditional branch, username/Checkbox when present, summary and save result. Every control is reachable by logical navigation, with visible geometry inside the declared viewport. Repeated synthetic CPU captures reproduce each accepted checkpoint; branch and draft changes are observable. Both copied-source imports build/run in Debug and Release; affected existing contract checks and documentation checks pass, with results only in support.

Stop when this section/edit/Apply path is copyable and its acceptance holds. Constrained-scrolling onboarding needs a separate consumer review; dynamic section-list mutation, new Tabs/RadioGroup/ScrollView APIs, style/lifecycle frameworks, native adapters and persistence are outside this slice. The [starter walkthrough](../guides/settings-starter.md) owns the procedure and the [consumer contract](../integrations/settings-app.md) owns behavior/lifetimes.

### Remaining milestone scope

Extend Settings in consumer order, then build the renderer diagnostics reference panel below, with a bounded acceptance condition before each implementation slice:

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Renderer diagnostics reference panel

Include the ability to author and operate a practical renderer diagnostics UI through Tessera in this milestone. The [consumer design](../integrations/renderer-diagnostics.md) owns the data/command boundary and the reference inventory; the [source review](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17) owns provenance and evidence limits. Keep the section starter as the single immediate slice; sequence the following bounded panel work after it rather than implementing the entire external overlay inventory at once.

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
