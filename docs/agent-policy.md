# Agent implementation policy

## Implementation authority

For repository-planned feature work, only these sources authorize implementation:

1. [Current work](roadmap/current.md), including its bounded supporting work.
2. Accepted issues or tasks explicitly linked from current work.
3. Fixes required to preserve an existing documented contract or pass an existing test.

Follow explicit user task instructions. Architecture, subsystem designs, integrations, proposals, and [backlog](roadmap/backlog.md) provide constraints and context; they do not independently authorize feature implementation. A detailed proposal is not a task, and a link from an active task does not activate the linked document's entire design.

Do not implement a Proposed section merely because its API has no code. Read [support](reference/support-matrix.md) for capability and validation evidence; design maturity labels do not establish support.

## Choose work from the integration fixture

Read [current work](roadmap/current.md) first, then support, [architecture](architecture/overview.md), the relevant subsystem contract, and [testing](guides/testing.md). Prefer the next observable step in the active author/reload/inspect/operate/capture/verify loop.

Before extending a subsystem, identify the fixture operation that cannot proceed, the expected observable behavior, and the smallest required change. Use existing APIs to construct the fixture before treating a possible design gap as a blocker. Record a demonstrated blocker and its bounded acceptance condition in current work; promote only that part of an inactive candidate.

Full-tree evaluation is the correctness reference. Convenience APIs, adjacent mechanisms, and optimization require their own demonstrated need; architectural depth does not establish scheduling priority.

For runtime functionality, name a blocked real application operation or widget, a failing existing contract, a measured performance bottleneck, or a concrete platform integration requirement. Otherwise keep the mechanism in backlog/proposals. An active authoring slice may add the public convenience needed by its application without redesigning the underlying runtime. Prefer application operation -> authoring API -> widget -> demonstrated runtime gap -> minimum fix -> regression -> working application. Define each slice's observable stop condition before implementation.

## Subsystem stop rule

A subsystem is sufficient for the active slice when:

- the integration fixture can proceed through the operation it required;
- tests cover the required invariants and affected boundaries;
- no active exit criterion is blocked by that subsystem.

Return to the integration fixture at that point. Do not implement adjacent proposal items for conceptual completeness. In particular, do not deepen lifecycle, async resources, replay, source mapping, or rendering solely because their designs describe further mechanisms.

## Documentation changes

Use the [canonical owners and change routing](README.md#canonical-owners). Current owns active scope, backlog owns inactive candidates, subsystem pages own contracts, support owns live capability/configuration evidence, and the changelog owns delivery history. Preserve Implemented/Proposed labels and source links; proposal extraction must retain invariants and design detail without activating it.

For documentation-only changes, run the [documentation check](guides/testing.md#documentation-verification) and review semantic ownership. Runtime test results must not be inferred from documentation checks.
