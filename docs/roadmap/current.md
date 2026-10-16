# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Build the first authoring path for the Settings reference application. Start with a small form skeleton, rather than an independent widget catalog:

```text
write a Settings component -> compose Text / Row / Column / Button
-> change state -> observe a derived value -> invoke a command
-> inspect -> capture -> replay / verify
```

Minimum scope:

- Define the smallest typed C++ construction surface needed for this form: primitive construction, ordered children, props/defaults, command binding, reusable composition, style attachment and explicit identity where required. Keep source/inspection visibility and actionable diagnostics.
- Implement `Text`, `Button`, `Row` and `Column` as authoring conveniences/components over the [UI model](../architecture/ui-model.md#primitives-and-components). New privileged node kinds require a demonstrated shared low-level need that ordinary composition cannot express.
- Give the form a simple state and derived-value surface, conceptually `state()` and `computed()`/`bind()`, over the [reactive runtime](../architecture/reactive-runtime.md). An ordinary author should not wire owner registration, binding tables, setup/cleanup or effect delivery by hand. The underlying ownership and host publication rules must remain explicit in the implementation.
- Exercise the API from application code: change a setting through an ordinary command, display its derived summary, and reuse a component with typed props and children. Keep application data and command execution host-owned.
- Build the fixture with existing runtime boundaries first. Before each runtime extension, record the blocked application operation, expected observation, minimum fix and focused regression. Do not infer a missing core feature from an inconvenient wrapper alone.

Slice acceptance: the form skeleton is authored through the high-level surface, a command updates state and its derived presentation coherently, a reusable component composes without manual mounting plumbing, and inspection plus repeated CPU capture/replay agree under declared fixed inputs. Invalid authoring inputs produce located diagnostics without replacing the accepted presentation. Verify the affected boundaries in Debug and Release and record evidence only in [support](../reference/support-matrix.md).

Stop this slice when those operations and lifetime/identity invariants pass. Review the application before expanding the API; do not generalize lifecycle, resources, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

1. Add `Checkbox`, then a bounded `TextInput` for the Settings fields. Prioritize caret, selection and editing, followed by clipboard, undo/redo, multiline, IME and platform hardening as actual application operations demand. Full native text integration is not a release gate.
2. Complete the [Settings workflow](../integrations/settings-app.md): multiple widgets, editable settings, conditional and repeated UI with stable identity, a selectable settings section, scrolling, commands and a host save/apply interaction. Define the exact normalized input and save observations before implementation.
3. Add style conveniences needed by that application: reusable classes/objects, theme variables, hover/focus/pressed/disabled states, typography, spacing, border, background, sizing and alignment, all through the [ordinary style resolver](../architecture/styling.md). Strengthen Row/Column, min/max or intrinsic sizing, overflow, scrolling and text-driven sizing only for demonstrated form requirements.
4. Turn the example into a copyable starting point and document the actual supported API, beginning with Getting Started and a first app under the [user documentation intent](../product/use-cases.md#learning-and-example-intent). Avoid publishing proposed syntax as a working guide.
5. Review authoring friction and grow widgets only from the application's needs. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work.

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
