# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Review the [scrolling Settings workflow](../../examples/settings/main.cpp) through the [form authoring contract](../architecture/ui-model.md#implemented-c-form-authoring) and [consumer boundary](../integrations/settings-app.md#implemented-constrained-scrolling-boundary), then define a bounded reusable-style operation for the same application. Identify concrete repetition or friction in control/text/section styling before selecting an authoring convenience.

```text
identify repeated Settings styling -> compose reusable control/text styles
-> change a declared style/theme input -> retain scrolling / focus / draft / saves
-> inspect resolved values / bounds -> capture -> replay / verify
```

Minimum scope:

- Review ordinary application composition separately from command/capture/scroll host scaffolding. Try existing `Props::style`, classes, style sheets and theme variables before promoting public API additions from the [authoring proposal](../proposals/cpp-authoring.md).
- Select the smallest application operation requiring reusable styling and specify exact control roles, resolved values, precedence and host style/theme input. Keep the [shared cascade/theme contract](../architecture/styling.md) authoritative; do not add a second style system.
- Preserve username edit owner/revision/selection, feature draft, section selection, saved copies/result and focus. Define offset retention/clamping and focus reveal if typography or spacing changes content geometry; verify clips, hit testing and semantic observations.
- Route activation through the ordinary command host. Invalid style/theme or rejected presentation candidates must preserve accepted values and observations with actionable diagnostics; disabled/stale guards must continue to agree with visible controls.
- Record demonstrated authoring friction and a bounded acceptance condition before extending the public surface. Preserve full-tree publication and repeated CPU/replay parity; implement only the convenience required by the selected operation.

Slice acceptance: first specify a bounded style fixture and observable resolved-value, geometry, retention and rejection criteria. Then express its repeated styles through the smallest ordinary authoring surface required by that operation. Verify affected boundaries in Debug and Release and record evidence only in [support](../reference/support-matrix.md).

Stop this slice when its declared authoring, style and retention invariants pass. Return to the application before selecting more widgets or styling mechanisms; clipboard, undo/redo, multiline, IME and platform hardening need separate consumer scope. Do not generalize widgets, lifecycle, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

1. Add style conveniences needed by the [Settings workflow](../integrations/settings-app.md): reusable classes/objects, theme variables, hover/focus/pressed/disabled states, typography, spacing, border, background, sizing and alignment, all through the [ordinary style resolver](../architecture/styling.md). Strengthen Row/Column, min/max or intrinsic sizing, overflow, scrolling and text-driven sizing only for demonstrated form requirements. Extend field editing only as application operations demand; full native text integration is not a release gate.
2. Turn the example into a copyable starting point and document the actual supported API, beginning with Getting Started and a first app under the [user documentation intent](../product/use-cases.md#learning-and-example-intent). Avoid publishing proposed syntax as a working guide.
3. Review authoring friction and grow widgets only from the application's needs. Async observation, named slots, SplitPane, Stack/Overlay or bounded absolute positioning require their own consumer operation before becoming active work.

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
