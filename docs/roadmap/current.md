# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md), and delivery history in the [changelog](../../CHANGELOG.md).

## v0.5.0 — Application UI Kit

Objective: **Tessera で小〜中規模のデスクトップツールを作れる。** Grow practical editing and widget operations from copyable application consumers, composing the public authoring surface and ordinary commands before extending runtime contracts. Broader [follow-up candidates](backlog.md#v050-follow-up-candidates) require individual promotion; adopting this milestone does not activate the inventory.

### Current slice

- **Minimum observable operation:** review copying/pasting a selected UTF-8 Username in the [Settings starter](../../examples/settings-starter/main.cpp), using the [whole-field replacement evidence](../reference/support-matrix.md#settings-starter-select-all-evidence--2026-11-01) as the baseline. Trace existing selection, edit commands and host staging before selecting the smallest host-owned clipboard boundary; record the demonstrated missing operation and its bounded implementation acceptance here.
- **New risks:** distinguish clipboard reads/writes from accepted draft publication, empty/reversed selection, rejected clipboard content, focus/eligibility and failure that must preserve draft/save observations. Declare whether an injected service or a concrete platform adapter supplies clipboard contents; evidence for one cannot establish the other.
- **Inherited guarantees:** [text_edit](../../tests/input/text_edit_tests.cpp) owns scalar boundaries, revision/owner validation and replacement bounds; [commands](../../tests/commands/commands_tests.cpp) owns once-only execution and accepted identities; [focus](../../tests/input/focus_tests.cpp) owns eligible author-ID recovery. Reuse the starter's [consumer boundary](../integrations/settings-app.md#implemented-copyable-editapply-starter-boundary) and affected checks under the [testing guide](../guides/testing.md#change-selection); do not re-prove those guarantees for each clipboard route.
- **Stop condition:** a source-backed consumer review identifies one operation, minimum boundary, new-risk checks and an observable stop before implementation is promoted. Undo/redo, multiline, IME, native text adapters and generalized shortcut/widget APIs remain separate candidates.

### Milestone exit criteria

- Each promoted application operation composes public authoring and ordinary input/command boundaries, with explicit host ownership and bounded consumer acceptance.
- The selected editing workflow preserves focus, eligibility, UTF-8 selection and owned save observations through accepted full-tree changes; the affected owner checks and consumer CPU observations agree.
- Select further widget, popup/dialog and native operations from demonstrated desktop-tool needs before treating them as release gates. Aggregate required integration/import/platform evidence under the [validation tiers](../guides/testing.md#validation-tiers) before release; this first operation does not establish a native desktop-tool release.

### Bounded validation supporting work

Retain the prior release's [acceptance evidence](../reference/support-matrix.md#v040-release-acceptance--2026-10-27). Confirm tag verification/source publication under the [release workflow](../guides/development.md#release-workflow); publication failures and reproducible released-scope regressions take priority. This validation work does not activate adjacent implementation or prevent a bounded UI operation without an actual failure.

- **Minimum observable operation:** confirm that main integration runs complete, and compare hosted PR and full integration durations from the [PR and full integration procedures](../guides/development.md#ci-scopes-and-reproducible-measurements) with the [recorded baseline](../reference/support-matrix.md#ci-scope-and-timing-evidence--2026-10-26), verifying shorter PR feedback and retained Debug/Release/import coverage without changing runtime assertions. Check the provisional ten-minute PR p95 against those samples.
- **New risks:** distinguish configure/build/test/queue time, fresh versus reused builds, local versus hosted conditions and actual failures versus reruns. Check documentation-only reporting, conservative scope fallback and main-run failures. Record missing runner capacity/cost and hosted parallelism observations rather than assuming them.
- **Inherited guarantees:** preserve all registered checks and tag/manual release verification, including its FetchContent filter; full integration must exercise both imports. Keep required-check names/settings unchanged until parallel workflow evidence and protection settings support a coordinated transition; the manual release workflow supplies the old comparison path.
- **Stop:** record hosted scope/results in support and follow the two-week Tier 2 observation policy. A failing main integration blocks the next release. CI cleanup requires its own measured justification in [backlog](backlog.md#validation-and-ci-follow-ups) and must not block a selected UI operation or delay a release without failures.
