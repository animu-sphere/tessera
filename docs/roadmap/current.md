# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md).

## v0.4.0 — Practical UI Authoring

Objective: **Tessera で普通に UI を書ける。** Prove that a developer can build a small application through a high-level C++ API and reusable widgets, without assembling reactive graph nodes, component mounting records, owner registrations, reconciliation tables or renderer internals for ordinary UI composition.

The runtime is the deterministic foundation. This milestone prioritizes authoring experience, composition, practical widgets and feedback from a real application. The [authoring proposal](../proposals/cpp-authoring.md) owns possible API mechanisms; the [Settings application design](../integrations/settings-app.md) owns the consumer workflow. Exact public syntax is open, and every convenience must lower into the ordinary runtime.

### Current slice

Review the [reusable-style Settings workflow](../integrations/settings-app.md#implemented-reusable-style-boundary) and select a bounded operation for visible focus, hover, pressed and disabled feedback. Use the existing [interaction state and cascade](../architecture/styling.md#interaction-states) before selecting any public convenience. Separate application class composition from host pointer/focus refresh and candidate publication.

```text
identify interaction feedback -> declare exact control/state styles
-> normalize pointer / focus / eligibility -> retain scrolling / draft / saves
-> inspect resolved values / bounds -> capture -> replay / verify
```

Minimum scope:

- Specify control targets, normalized events, exact state values, precedence and host interaction inputs before implementation. Account for generated Text targets, ancestor hover/active matching, focused-owner matching and inherited disabled state under the shared resolver.
- Preserve username edit owner/revision/selection, feature draft, section selection, saved copies/result, theme and offsets. Keep clips, hit testing, semantic eligibility and any required focus reveal coherent with accepted styles.
- Route activation through the ordinary command host. Rejected presentation candidates must retain accepted observations with actionable diagnostics. Refresh or clear stale pointer/focus handles on replacement according to existing input contracts; styling cannot bypass command-disabled guards.
- Try existing classes, declarations and host-supplied `InteractionState`; record demonstrated friction and a bounded acceptance condition before extending the public surface. Preserve full-tree publication and repeated CPU/replay parity.

Slice acceptance: first specify a bounded interaction-feedback fixture and observable resolved-value, input-routing, geometry, retention and rejection criteria. Then express its styles through the smallest ordinary authoring surface required by that operation. Verify affected boundaries in Debug and Release and record evidence only in [support](../reference/support-matrix.md).

Stop this slice when its declared authoring, style and retention invariants pass. Return to the application before selecting more widgets or styling mechanisms; clipboard, undo/redo, multiline, IME and platform hardening need separate consumer scope. Do not generalize widgets, lifecycle, layout or rendering to complete a proposal.

### Remaining milestone scope

Extend that same application in consumer order, with a bounded acceptance condition before each implementation slice:

1. Connect hover/focus/pressed/disabled feedback needed by the [Settings workflow](../integrations/settings-app.md) through the [ordinary style resolver](../architecture/styling.md). Add style conveniences or strengthen Row/Column, sizing, alignment, overflow and text-driven geometry only for demonstrated form requirements. Extend field editing only as application operations demand; full native text integration is not a release gate.
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
