# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). The [agent policy](../agent-policy.md) defines implementation authority and the stop rule; [support](../reference/support-matrix.md) owns capability/validation evidence and the [changelog](../../CHANGELOG.md) owns delivery history.

## v0.5.0 — Application UI Kit

Objective: **Build a practical small Settings/Inspector desktop tool through public Tessera C++ APIs.** Judge success by usable application operations and change-to-merge lead time. Extend runtime internals only when a concrete application operation is blocked; runtime generalization is not a milestone goal.

### Required application flow

One reference application must let a user:

1. Select a settings category from a sidebar.
2. Edit text, toggle a Checkbox and adjust a number through a Slider or an adequate existing input API.
3. Scroll the list/form and reach required controls with keyboard and pointer input.
4. Apply or Cancel changes while retaining category selection and pending edits across unrelated UI changes.
5. Use one practical selection UI, such as a Dropdown or Menu.

The [Settings consumer](../integrations/settings-app.md#proposed-application-workflow) owns the application design. Reuse its public authoring, command, focus and host-staging boundaries; a scripted CPU fixture alone does not establish an interactively usable desktop application.

### Current slice

Add one numeric setting to the [copyable Settings starter](../../examples/settings-starter/main.cpp), using an adequate existing input API before introducing a Slider abstraction. Make a bounded value editable through public APIs, with actionable validation, and include it in the host's draft and Apply/Cancel copies. Keep category selection and unrelated username/feature edits across numeric changes; Cancel restores the last applied value, or its initial value before the first Apply. Keep shell and host ownership explicit.

Stop when numeric editing, Apply/Cancel and representative invalid input succeed through affected owner tests and one consumer smoke. Check independent draft/save retention; inherit unchanged edit, command and focus guarantees under the [testing guide](../guides/testing.md#verification-ownership). The earlier clipboard-review candidate is deferred to [editing follow-ups](backlog.md#v050-follow-up-candidates); it is not a prerequisite for this flow. No new runtime abstraction or general widget framework is required by this slice.

### Implementation sequence

- **P0:** Complete the minimal application shell and first editable Settings flow using existing controls, with explicit host state and Apply/Cancel.
- **P1:** Add only the missing numeric, scrolling/list and selection controls needed by the required flow. Reuse existing APIs before introducing a Slider, ScrollView/List or Dropdown/Menu abstraction.
- **P2:** Reduce demonstrated public-API friction in event registration, state updates, styling, examples and error display.
- **P3:** Add the minimum native presentation/input or text integration required for interactive use. Broader focus, IME and clipboard adapters need a concrete application operation; they are not an inventory to complete.

Choose one operation at a time within this sequence. Native presentation needed for the desktop tool is required; all-platform text adapters and parity across every input route are not.

### Milestone exit criteria

- The reference application's required flow works through public APIs without private framework plumbing, including interactive desktop use through a declared host configuration.
- Affected owner contracts and a representative consumer smoke pass; pending edits, selection and accepted Apply/Cancel observations are retained according to the consumer contract.
- Required integration/import and claimed platform checks pass before release under the [validation tiers](../guides/testing.md#validation-tiers). Unexecuted platforms remain unvalidated. All candidate widgets need not be implemented.

### Bounded validation supporting work

Retain prior [release acceptance](../reference/support-matrix.md#v040-release-acceptance--2026-10-27) and [CI measurements](../reference/support-matrix.md#ci-scope-and-timing-evidence--2026-10-26). Fix publication failures and reproducible regressions with priority. CI tuning runs alongside feature work; use available measurements under the [testing policy](../guides/testing.md#measurement-and-safe-reduction), without a fixed observation period or sample-count gate. Resolve persistent integration failures before release. Workflow and required-check changes need their own verified transition; this roadmap changes no CI settings.

### Deferred scope

Full TreeView, generic Portal, integrated DevTools, all-platform IME, bindings, incremental rendering and exhaustive input-route parity stay in [backlog](backlog.md). Promote only the minimum behavior needed by a concrete application operation.
