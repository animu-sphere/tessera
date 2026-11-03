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

The [Windows starter host](../guides/settings-starter.md#run-in-a-windows-window) now implements the required application flow through public APIs. Focused native and copied-consumer evidence is in [support](../reference/support-matrix.md#settings-native-host-evidence--2026-11-03).

- Run the Tier 3 release checks for the milestone and record platform/configuration limits before release acceptance.
- Fix application or public-API failures found during that acceptance; do not expand the widget catalog without an application need.

### Exit criteria

- The required flow works through public APIs in a native window, with no private framework plumbing in the example.
- Owner tests for changed subsystems and the Settings consumer smoke pass.
- Release checks under the [validation tiers](../guides/testing.md#validation-tiers) pass; unexecuted platforms stay unvalidated.

### Deferred

Full TreeView, generic Portal, integrated DevTools, all-platform IME, bindings and incremental rendering stay in [backlog](backlog.md).
