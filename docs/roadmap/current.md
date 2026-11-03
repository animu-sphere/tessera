# Current

Inactive candidates live in [backlog](backlog.md). [Support](../reference/support-matrix.md) owns capability/validation evidence and the [changelog](../../CHANGELOG.md) owns delivery history.

## v0.5.0 — Application UI Kit

Objective: **Build a practical small Settings/Inspector desktop tool through public Tessera C++ APIs.** Success is a usable application, not a count of widgets or documents. Add widgets, host integration and runtime changes as the application needs them.

### Required application flow

One reference application, grown from the [Settings starter](../../examples/settings-starter/main.cpp), must let a user:

1. Select a settings category from a sidebar.
2. Edit text, toggle a Checkbox and adjust a number through a Slider or numeric field.
3. Scroll the list/form and reach every control with keyboard and pointer input.
4. Apply or Cancel changes while retaining category selection and pending edits across unrelated UI changes.
5. Pick from a Dropdown or Menu.
6. Run interactively in a native desktop window.

The [Settings consumer](../integrations/settings-app.md#proposed-application-workflow) owns the application design; the [starter guide](../guides/settings-starter.md) owns the copy/build/run procedure.

### Work items

Pick any unblocked item and deliver it whole, including its widget, example wiring, tests and guide update in one PR:

- Numeric setting with a Slider (keyboard, pointer, bounded value, actionable validation).
- Native window host for the starter (window, input, presentation) on the primary supported platform.
- Public-API friction fixes found while doing the above: event registration, state updates, styling, error display.

### Exit criteria

- The required flow works through public APIs in a native window, with no private framework plumbing in the example.
- Owner tests for changed subsystems and the Settings consumer smoke pass.
- Release checks under the [validation tiers](../guides/testing.md#validation-tiers) pass; unexecuted platforms stay unvalidated.

### Deferred

Full TreeView, generic Portal, integrated DevTools, all-platform IME, bindings and incremental rendering stay in [backlog](backlog.md).
