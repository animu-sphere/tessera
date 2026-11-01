# Agent implementation policy

## Implementation authority

Follow explicit user task instructions. For repository-planned features, [current work](roadmap/current.md), its explicitly linked accepted tasks and fixes to existing documented contracts/tests authorize implementation. Designs, integrations, proposals and [backlog](roadmap/backlog.md) supply constraints and context; a link does not activate their entire scope. A Proposed API without code is not itself a task.

## Choose work from the application

Read current work and the code/docs near the change. Start from a user operation or reproducible defect, use existing APIs, and prefer the smallest public authoring/widget change. Runtime changes need a demonstrated application blocker, existing contract defect, measured bottleneck or concrete platform requirement. Name what the milestone user cannot do without the change; otherwise defer it.

Keep the next operation and its observable acceptance in current work without requiring a separate pre-implementation review task. Prefer application operation -> public API/widget composition -> demonstrated runtime gap -> minimum fix -> targeted checks -> working application. Full-tree evaluation remains the correctness reference. If the same underlying problem needs a second broad runtime revision, inspect the design/API friction with concrete examples before proceeding; keep that review bounded.

## Verification ownership and bounded acceptance

Prefer one user operation or defect per PR; closely related small fixes may be grouped when splitting adds overhead. A short description of behavior, relevant risk and executed checks is sufficient, usually about three lines. There is no mandatory acceptance template or separate inherited-guarantee/stop-condition field.

Run affected owner tests and one representative public-API consumer smoke. Consumers cover successful use and risks specific to their wiring, state and host policy; inherit unchanged runtime guarantees from named owner tests and their applicable assumptions. Host staging remains a consumer responsibility unless a shared boundary owns it; command execution alone does not imply atomic publication. The [testing guide](guides/testing.md#verification-ownership) owns L1-L4 responsibilities and [change selection](guides/testing.md#change-selection).

Do not multiply every operation by all input routes, failure points and build configurations. A new trust/owner boundary, actual regression or first cross-layer failure can justify deeper checks. Preserve substantive ownership, lifetime, invalid-input and regression coverage; removing duplicates needs equivalent owner coverage and preserved diagnostic value, not a long report.

Distinguish executed checks from inherited guarantees. Update [support](reference/support-matrix.md) only when capability scope, support claims or material validation evidence changes; keep routine check results in the PR. Never claim an unexecuted environment. Wider integration and release checks follow the [validation tiers](guides/testing.md#validation-tiers); CI tuning and observation do not block a working UI slice without an actual regression.

## Subsystem stop rule

Stop subsystem expansion when the application operation works, affected owner/consumer checks pass and no active exit criterion needs more from that subsystem. Return to the application. Do not add adjacent lifecycle, async, replay, source-mapping or rendering mechanisms for conceptual completeness.

Done means the operation works, affected regression checks pass and any changed public contract is documented by its owner. Optional widgets, exhaustive route/failure matrices and extra abstractions are not implicit requirements.

## Documentation changes

Use [canonical owners and change routing](README.md#change-routing). Current owns active scope, backlog inactive candidates, subsystem pages contracts, support live capability/configuration evidence and changelog delivery history. Preserve Implemented/Proposed labels and source links. Update only owners whose contract, procedure, scope or material evidence changes; routine UI PRs need no new support history or index edits. Retain existing historical evidence.

For documentation-only changes, run the [documentation check](guides/testing.md#documentation-verification) and review ownership; runtime tests are unnecessary. Documentation checks establish no runtime result.
