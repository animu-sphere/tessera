# Settings reference application

This consumer design is **Proposed — Direction**. It defines an application workflow, not a public API or support claim. [Current work](../roadmap/current.md) owns the active subset; broader requirements need explicit promotion under the [agent policy](../agent-policy.md).

## Proposed application workflow

Use a small Settings editor as the reference for ordinary UI authoring. A developer should build it from the [C++ authoring surface](../proposals/cpp-authoring.md) and reusable components without assembling graph nodes, mounting records or renderer internals.

The application presents settings sections, a username text field, an enable-feature checkbox, derived status text and an Apply command. A reusable settings-row component accepts typed props and child content. Conditional help/status and a small keyed list of settings sections exercise composition, repeated UI and stable selection. A bounded viewport requires scrolling to reach part of the form. The host owns the settings data and the save/apply operation; an injected save service or observable host result is sufficient before filesystem persistence is justified.

Begin with Text, Row, Column, Button, state and a derived summary. Add Checkbox, bounded TextInput and style conveniences from this same workflow. Each extension must name the application operation it enables and its stop condition. Additional controls, platform adapters and application shells are separate consumer choices.

## Proposed interaction and observation

- Edit the username through normalized input, move the caret, extend selection and replace selected text. Text editing and later clipboard/composition behavior follow [input](../architecture/input.md#proposed-editing-ime-and-clipboard-boundary), with typography behind the [text interface](../architecture/text.md).
- Toggle the feature, update its derived summary and conditional content, and retain compatible local state/focus while keyed settings sections reorder. Application selection is distinct from text selection and runtime handles.
- Scroll the constrained form and keep visible paint, hit geometry, semantic bounds and navigation consistent. Use existing [layout](../architecture/layout.md) before proposing a new algorithm.
- Invoke Apply through ordinary commands and observe the host's accepted settings/result. Pointer, keyboard/gamepad and semantic requests share eligibility and command execution; define the fixture's exact normalized mappings before implementation.
- Apply reusable styles and theme variables, including hover, focus, pressed and disabled states, through [styling](../architecture/styling.md), then inspect the same accepted presentation used for capture and replay.

## Verification requirements

Declare viewport/scale, initial state, normalized input, time, fonts/shaper, assets, save-service results and backend configuration. Verify state/derived-state agreement, text revision/selection, keyed identity, command results and save observations numerically or semantically. Repeated [replay](../architecture/replay.md) and CPU-reference captures must reproduce accepted observations; use selective images for paint behavior rather than per-widget screenshot suites.

Invalid props, bindings, styles or replacement candidates must report actionable source diagnostics and preserve the accepted presentation. Test lifetime cleanup and stale requests at affected boundaries under the [testing guide](../guides/testing.md). Native IME/clipboard, real-font and GPU claims require their own declared configuration evidence in [support](../reference/support-matrix.md).

## Authoring feedback and onboarding

Treat this example as a copyable starting point. Record friction from composing fields, changing state, binding commands, styling, reading diagnostics and verifying the result; use it to choose the smallest API/runtime change. Async-data, custom-component, styling, inspector, asset-browser and hosting examples should each answer one developer question when their consumer scope is selected. A broad example directory tree is not an implementation requirement.

Stop expanding the consumer when its promoted application operations and required invariants pass. A complete visual editor, generalized portal system, broad widget catalog and native platform integration suite are not implicit dependencies.
