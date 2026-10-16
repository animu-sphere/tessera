# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Review the [Settings form](../../examples/settings/main.cpp) through the [form authoring contract](../architecture/ui-model.md#implemented-c-form-authoring), then define the next bounded operation: apply the edited username and feature setting through one host-owned command and observe its saved result.

```text
compose the editable form and Apply button -> edit username / toggle feature
-> invoke Apply -> observe owned settings / save result
-> inspect -> capture -> replay / verify
```

Minimum scope:

- Review composition and host scaffolding separately; preserve typed props/defaults, ordered children, explicit identity, state/computed, source diagnostics and the bounded field editing contract. Use the ordinary Button and command interfaces before promoting conveniences.
- Compose username, Checkbox, derived summary and Apply in one host-owned settings workflow. Define the exact normalized editing/toggle/focus/activation sequence, the copied settings payload, save counter and visible result before implementation. An injected synchronous host save observation suffices; filesystem persistence is outside this slice.
- Route keyboard/gamepad logical activation, pointer release and semantic Apply requests through one command descriptor/executor. Disabled or stale owners/invocations must change neither settings nor save observations; one accepted activation must call the host operation once.
- Preserve host-controlled full-tree preparation/publication, source-aware invalid-candidate rejection and accepted-snapshot ownership. Record a demonstrated blocker and bounded regression before extending a subsystem; do not introduce dynamic mounting or lifecycle mechanisms for Apply.

Slice acceptance: author the editable form and Apply through the form surface, edit/toggle through declared normalized input, and invoke one host save operation with an owned copy of the same accepted settings. Inspection, visible result and repeated CPU capture/replay must agree under fixed inputs; stale/disabled requests preserve host state and invalid candidates preserve the accepted presentation with located diagnostics. Verify affected boundaries in Debug and Release and record evidence only in [support](../reference/support-matrix.md).

Stop this slice when editing, toggle, Apply and its shared eligibility/owned-result invariants pass. Return to the application before selecting conditional/repeated sections or scrolling; clipboard, undo/redo, multiline, IME and platform hardening need separate consumer scope. Do not generalize widgets, lifecycle, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

1. Complete the [Settings workflow](../integrations/settings-app.md): multiple widgets, editable settings, conditional and repeated UI with stable identity, a selectable settings section, scrolling, commands and a host save/apply interaction. Define the exact normalized input and save observations before implementation. Extend field editing only as those operations demand; full native text integration is not a release gate.
2. Add style conveniences needed by that application: reusable classes/objects, theme variables, hover/focus/pressed/disabled states, typography, spacing, border, background, sizing and alignment, all through the [ordinary style resolver](../architecture/styling.md). Strengthen Row/Column, min/max or intrinsic sizing, overflow, scrolling and text-driven sizing only for demonstrated form requirements.
3. Turn the example into a copyable starting point and document the actual supported API, beginning with Getting Started and a first app under the [user documentation intent](../product/use-cases.md#learning-and-example-intent). Avoid publishing proposed syntax as a working guide.
4. Review authoring friction and grow widgets only from the application's needs. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work.

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
