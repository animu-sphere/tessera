# Settings reference application

The Proposed sections define application direction; the Implemented section describes the source-backed consumer boundary. [Current work](../roadmap/current.md) owns the active subset; broader requirements need explicit promotion under the [agent policy](../agent-policy.md). Capability and validation evidence belongs in [support](../reference/support-matrix.md).

## Implemented form skeleton boundary

The [Settings fixture](../../examples/settings/main.cpp) composes Text, Checkbox, Row and Column through the [C++ form authoring contract](../architecture/ui-model.md#implemented-c-form-authoring). `SettingsRow` is an ordinary C++ function with typed label/identity props, a default gap and ordered child content. The Settings form composes it twice, without component mounting records or binding tables. A session owns a named enabled observation and a derived summary; a host command owns the application boolean and writes that observation after dispatch. The Checkbox's controlled marker and content-derived button name expose the same value alongside its label.

The host prepares command eligibility, full-tree styles/layout/semantics/paint and source inspection before graph publication. Logical focus/activation events exercise the keyboard/gamepad route; pointer press/release and semantic activation reach the same command descriptor and executor. Disabled command snapshots suppress activation, and old-tree owners fail resolution. An invalid style candidate returns a supplied source expression origin while the host retains its accepted snapshot.

The normalized sequence starts false: `FocusNext` then keyboard `Activate` toggles true; focus recovery by author ID and gamepad logical `Activate` toggles false; semantic activation toggles true; pointer press/release toggles false once on release. Each replacement exposes the checked marker, label, derived summary and source origins from one accepted presentation. Both authored-disabled and command-disabled candidates reject logical, pointer, semantic and direct command activation. A pending unexecuted invocation and old-tree action owner reject after replacement despite the same author ID. Physical key/button normalization belongs to the host.

The username variant adds an Account row through that same Settings function, with `TextInput("Username", observed, "settings.edit", props)`. One host-owned `TextEditState` and its session observation supply the committed value, UTF-8 byte anchor/caret and derived `User <value>; feature disabled` summary. The field's ordinary button name joins the label, value and visible `Selection <anchor>:<caret>` Text. The form contract owns this composition's public types and limits; there is no separate editable semantic schema.

The host focuses Checkbox then username with two logical `FocusNext` events. Committed normalized text inserts `日😀` after `Ada`, yielding `Ada日😀` at 10:10; Left moves to 6:6, Shift+Left selects 6:3, inserting `本` yields `Ada本😀` at 6:6, and Delete yields `Ada本` at 6:6. Home, Right, Backspace and End then yield `da本` at 5:5. Each edit or selection command advances one revision. Control/Alt/Super modified keys are outside this mapping; supported keys are consumed before focus navigation. Each synchronous edit host owns its immutable request payload and creates a pure candidate through `edit_text` after the ordinary executor rechecks the live activate owner. The application writes the observation, prepares all presentation stages and only then accepts host state, graph publication and snapshot replacement. On a returned preparation failure it restores the previous observation and keeps the accepted snapshot. Allocation failures propagate; this is not generalized exception rollback.

The fixture also selects all, injects measurement/paint rejection for a replacement and retries a replacement using a fresh execution sequence. Rejection retains host value, revision, selection and accepted inspection/document; host mapping translates stage preorder-node diagnostic paths to source document pointers before locating them. Command-disabled and authored-disabled fields suppress focus, normalized text routing, semantic and direct command requests. Old tree owners, pending invocations and stale edit owners/revisions reject. Malformed edit observations return source-located candidate diagnostics.

The scripted consumer uses a fixed logical viewport, explicit scale/locale/clock, placeholder text metrics and synthetic glyph coverage. It records normalized actions plus the host's accepted document/style replacements, then compares replay geometry, semantics, paint and CPU images. Raw editing keys/text and edit payloads are host-translated and represented by accepted replacement views; replay reproduces recorded views without executing the application's C++ command host. Images and source replacements stay in memory; no caret geometry, composition/IME, clipboard, window, installed font, file save, native input device or GPU is implied.

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
