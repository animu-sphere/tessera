# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Review the [combined Settings workflow](../../examples/settings/main.cpp) through the [form authoring contract](../architecture/ui-model.md#implemented-c-form-authoring) and [consumer boundary](../integrations/settings-app.md#implemented-form-skeleton-boundary), then define the next bounded operation: select a settings section from a small repeated list and project its conditional content while retaining the editable draft and saved result.

```text
compose settings-section choices -> select by normalized input
-> show selected content -> retain draft / saved result / stable identities
-> inspect -> capture -> replay / verify
```

Minimum scope:

- Review composition and host scaffolding separately. Try ordinary C++ repeated composition, explicit IDs, state/computed and Button commands before promoting a public convenience or dynamic component mechanism.
- Define the section IDs, initial/order/reorder/removal cases, selected value, conditional content, exact normalized selection sequence and focus-recovery expectations before implementation. Application section selection must remain distinct from runtime handles and username byte selection.
- Preserve the host-owned username/feature draft and saved settings/result across section selection and compatible reorder. Define whether hidden content stays authored or is removed, and name any local state whose lifetime requires keyed component ownership rather than inferring that mechanism from a repeated list.
- Route keyboard/gamepad logical activation, pointer release and semantic section requests through common eligibility and execution. Disabled/stale requests and invalid candidates must preserve the accepted selection, draft, save observations and presentation, with located diagnostics.
- Record a demonstrated fixture blocker and bounded regression before extending a subsystem. Preserve full-tree preparation/publication and repeated CPU/replay parity; scrolling, new editing behavior and generalized lifecycle mechanisms need their own operation.

Slice acceptance: first specify a bounded section-selection fixture and observable retention/rejection criteria. Then author its repeated choices and conditional presentation through the ordinary runtime, keeping stable section identities, draft/saved ownership, focus, inspection and deterministic CPU/replay observations coherent. Verify affected boundaries in Debug and Release and record evidence only in [support](../reference/support-matrix.md).

Stop this slice when its declared selection, conditional/repeated content and retention invariants pass. Return to the application before selecting scrolling or style conveniences; clipboard, undo/redo, multiline, IME and platform hardening need separate consumer scope. Do not generalize widgets, lifecycle, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

1. Extend the [Settings workflow](../integrations/settings-app.md) with conditional and repeated UI, stable section identity/selection and scrolling, retaining editing and host-owned Apply. Define the exact normalized input and retained save observations before each operation. Extend field editing only as those operations demand; full native text integration is not a release gate.
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
