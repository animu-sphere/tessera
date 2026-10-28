# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md). Implementation authority and the subsystem stop rule are owned by the [agent policy](../agent-policy.md); implementation and validation evidence belongs in [support](../reference/support-matrix.md), and delivery history in the [changelog](../../CHANGELOG.md).

## v0.4.0 publication and observation

Objective: publish the v0.4.0 Practical UI Authoring source release and keep its released scope healthy until a next milestone is explicitly adopted. The [release acceptance](../reference/support-matrix.md#v040-release-acceptance--2026-10-27) classifies every v0.4.0 exit criterion against dated evidence; the [changelog](../../CHANGELOG.md#v040--2026-10-27) owns release contents.

### Current slice

- **Publish:** tag the release merge commit under the [release workflow](../guides/development.md#release-workflow) and confirm that tag verification and source publication succeed. A failure is a release blocker; fix only its cause.
- **Patch scope:** v0.4.x takes regression fixes in released scope only, under [after v0.4.0](backlog.md#after-v040). Change code only for a reproducible defect in released scope or a failing existing contract, each opened with the four acceptance items in the [agent policy](../agent-policy.md#verification-ownership-and-bounded-acceptance).
- **Next milestone:** the [v0.5.0 candidate](backlog.md#v050-candidate--application-ui-kit) and later candidates stay inactive until explicitly adopted here with a concrete consumer operation, bounded acceptance and a stop condition. Record noticed improvements in [backlog](backlog.md).

### Bounded validation supporting work

- **Minimum observable operation:** confirm that main integration runs complete, and compare hosted PR and full integration durations from the [PR and full integration procedures](../guides/development.md#ci-scopes-and-reproducible-measurements) with the [recorded baseline](../reference/support-matrix.md#ci-scope-and-timing-evidence--2026-10-26), verifying shorter PR feedback and retained Debug/Release/import coverage without changing runtime assertions. Check the provisional ten-minute PR p95 against those samples.
- **New risks:** distinguish configure/build/test/queue time, fresh versus reused builds, local versus hosted conditions and actual failures versus reruns. Check documentation-only reporting, conservative scope fallback and main-run failures. Record missing runner capacity/cost and hosted parallelism observations rather than assuming them.
- **Inherited guarantees:** preserve all registered checks and tag/manual release verification, including its FetchContent filter; full integration must exercise both imports. Keep required-check names/settings unchanged until parallel workflow evidence and protection settings support a coordinated transition; the manual release workflow supplies the old comparison path.
- **Stop:** record hosted scope/results in support and follow the two-week Tier 2 observation policy. A failing main integration blocks the next release. CI cleanup requires its own measured justification in [backlog](backlog.md#validation-and-ci-follow-ups) and must not block a selected UI operation or delay a release without failures.

### Stop condition

Stop when v0.4.0 is published, Tier 2 observation follows its policy above and no reproducible regression in released scope is open. Do not start v0.5.0 widget, overlay, tooling or native-integration work from backlog inventories without explicit adoption.
