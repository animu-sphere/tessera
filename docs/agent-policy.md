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

For runtime functionality, name a blocked real application operation or widget, a failing existing contract, a measured performance bottleneck, or a concrete platform integration requirement. Otherwise keep the mechanism in backlog/proposals. An active authoring slice may add the public convenience needed by its application without redesigning the underlying runtime. Prefer failing application operation -> shortest public authoring API -> composition of existing widgets -> demonstrated runtime gap -> minimum fix -> regression -> working application. Define each slice's observable stop condition before implementation.

Before proposing additional implementation, state what users of the active milestone could not do without it. If that cannot be stated, defer it to backlog. When two consecutive UI-operation changes each require broad runtime changes, investigate authoring API friction before adding further operations; introduce any resulting generalization in a small step grounded in measurements and real examples.

## Verification ownership and bounded acceptance

Concentrate guarantees at their owning boundaries; verify application use in consumers. Do not exhaustively re-prove a runtime contract for every consumer feature. Cite the owner test and its applicable assumptions when inheriting a guarantee. A consumer proves a successful operation through public APIs and risks specific to its wiring, state or host policy. Host staging is a consumer responsibility unless a shared boundary actually owns it; do not infer atomic publication from command execution alone. The [testing guide](guides/testing.md#verification-ownership) owns the responsibility model and owner-test mapping.

Define slice acceptance with four items:

1. **Minimum observable operation:** what the user can do and what accepted result proves it works.
2. **New risks:** the identities, validation, state retention or cross-boundary behavior introduced or changed by this operation.
3. **Inherited guarantees:** named owner tests and assumptions, plus affected checks selected under the testing guide.
4. **Stop condition:** the smallest fixture observation that closes the operation and returns work to integration.

Scope one change (pull request) to one user operation or one defect, and open its description with these four items. Keep new risks to the few boundaries the change can actually break, typically two or three. Once the accepted observation and new-risk checks pass, do not generalize the API or add adjacent features in that change; record noticed improvements in [backlog](roadmap/backlog.md). Updating current work is part of the change, not a separate long-running task.

Do not require the Cartesian product of all input sources, failure points and build configurations for each slice. A first cross-layer failure mode, an actual regression or a distinct trust/owner boundary can justify deeper consumer checks; record the reason and termination condition. Preserve existing regression coverage until equivalent owner coverage and measurement justify a bounded change.

Distinguish checks executed for this change from guarantees inherited from existing contracts. Record configuration and limits in support; do not claim success for an unexecuted environment. Use targeted local checks, a small representative PR gate and wider integration/release validation under the [CI policy](guides/testing.md#validation-tiers). Measure before removing protection or changing required checks.

## Subsystem stop rule

A subsystem is sufficient for the active slice when:

- the integration fixture can proceed through the operation it required;
- owner tests cover the required invariants, and consumer checks cover the operation's new risks and affected boundaries;
- no active exit criterion is blocked by that subsystem.

Return to the integration fixture at that point. Do not implement adjacent proposal items for conceptual completeness. In particular, do not deepen lifecycle, async resources, replay, source mapping, or rendering solely because their designs describe further mechanisms.

## Documentation changes

Use the [canonical owners and change routing](README.md#canonical-owners). Current owns active scope, backlog owns inactive candidates, subsystem pages own contracts, support owns live capability/configuration evidence, and the changelog owns delivery history. Preserve Implemented/Proposed labels and source links; proposal extraction must retain invariants and design detail without activating it.

Keep current work to the next operation, bounded observable acceptance and stop condition; put detailed behavior in its subsystem or consumer owner. Do not create a review article or changelog entry for every pre-implementation selection. Record significant direction intake and deliveries; retain existing history.

For documentation-only changes, run the [documentation check](guides/testing.md#documentation-verification) and review semantic ownership. Runtime test results must not be inferred from documentation checks.
