# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Create a copyable Settings edit/Apply starter that extends the [first app](../guides/first-app.md) through the [implemented form API](../architecture/ui-model.md#implemented-c-form-authoring). The developer operation is: add a username field, edit its draft, toggle the feature and Apply an owned copy, then observe that later edits leave the saved result unchanged until Apply runs again. The selection evidence and source-review limits are owned by [support](../reference/support-matrix.md#first-app-to-settings-authoring-review--2026-10-17); the [Settings consumer](../integrations/settings-app.md#implemented-form-skeleton-boundary) owns the workflow contract.

```text
copy starter -> edit username -> toggle feature -> Apply
-> edit draft again -> inspect unchanged saved copy -> Apply again
```

Minimum scope:

- Supply a small standalone source/CMake starting point and a linked walkthrough from first-app. Compose username TextInput, Checkbox, derived summary, Apply and saved-result Text through ordinary typed functions, explicit IDs and one session. Keep the original first-app focused on its boolean operation.
- Keep the accepted draft, edit owner/revision/byte selection, owned save copies and command execution in the host. Route committed normalized text and the Settings mapping's supported editing keys before logical navigation; keyboard/gamepad logical activation requests the same Apply command after dispatch. Use an in-memory synchronous save observation.
- Separate reusable application style declarations from composition through existing typed props/classes and the shared resolver. A fixed form palette is sufficient for this operation; theme switching, interaction-state feedback, section selection and constrained scrolling stay in the linked Settings fixture.
- Stage full-tree eligibility/style/layout/inspection/paint before accepting the host draft or a save copy and publishing the session. Explain bounded observation restoration on a returned staging failure and the lifetimes of borrowed inputs; keep rejection/stale-owner fault injection in the existing Settings regression fixture.
- Verify the copied project through both local source import forms in Debug and Release using only `tessera::core`. Use a compact successful edit/toggle/Apply/edit/Apply sequence to inspect draft/summary/saved-result behavior, retained focus and repeated synthetic CPU captures. Retain affected existing boundary checks and run documentation checks; record actual configuration/results only in [support](../reference/support-matrix.md).

Slice acceptance: a developer can copy/build/run the starter independently and follow the walkthrough without extracting host code from the regression fixture. The sequence saves the first draft once, preserves it during a later edit, then saves the changed draft once; accepted inspection and repeated CPU captures agree, and focus survives replacement by author ID. The walkthrough distinguishes composition, normalized input, host command/save ownership and publication. Source review alone does not establish this executed acceptance.

Stop after the copyable edit/Apply operation and its affected boundaries pass. Reuse existing APIs; no runtime or public style convenience is promoted by this slice. Further widgets, theme/style conveniences, selection/scrolling onboarding, clipboard, undo/redo, multiline, IME and platform hardening need separate consumer scope. Do not generalize widgets, lifecycle, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

Review authoring friction and grow widgets only from the application's needs. Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work. Extend field editing only as application operations demand; full native text integration is not a release gate. Keep working source and [onboarding](../guides/getting-started.md) aligned as each operation is added; avoid publishing proposed syntax as a working guide.

### Release exit criteria

- A developer can create a small application through a high-level C++ API without low-level runtime assembly for ordinary UI composition; permanent syntax stability is not required.
- A basic component/widget library supports the reference application, including Text, Button, Row, Column, Checkbox and bounded TextInput. A complete widget inventory is not required.
- State and derived state have a simple public interface; reusable components support typed props/defaults, children, local state, command output, style attachment and stable identity where used. Conditional and repeated UI use the same runtime model.
- Common application styles are expressed without manually constructing low-level style machinery, and still resolve through the shared cascade/theme model.
- At least one application-shaped, copyable example exercises editing, scrolling, selection, styling, layout and host interaction through ordinary normalized input. Keyboard/gamepad navigation and command eligibility follow the common input boundary; platform claims require their own evidence.
- Deterministic replay, inspection and CPU-reference validation reproduce the application's accepted state, semantic observations, geometry and paint under declared inputs. Existing contract regressions remain covered.
- The runtime has one execution model for C++, documents, editor preview and automation. Run affected checks in Debug and Release and documentation checks; put configuration/results only in support and delivery history in the [changelog](../../CHANGELOG.md).

### Non-goals and stop condition

Do not make generalized lifecycle redesign, a new reactive graph, arbitrary reload migration, full reflection, generalized resource providers, complete Portal/OverlayRoot, full CSS/Grid, retained/incremental rendering, generalized multithreading, stable C ABI, Python/Rust/Go bindings, browser/WebGPU hosting, plugin frameworks, complete native accessibility, IDE tooling or a visual editor prerequisites for this release.

Stop milestone expansion when ordinary application authoring and the reference workflow meet these exit criteria. The [broader widget candidates](backlog.md#widget-candidates-beyond-the-reference-application), [editor extensions](backlog.md#unscheduled-editor-extensions), other language/platform work and performance candidates stay inactive until a concrete consumer promotes a bounded operation.
