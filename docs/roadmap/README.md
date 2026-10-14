# Roadmap

[Current](current.md) owns the active milestone and remaining work. [Backlog](backlog.md) owns inactive version candidates and their dependencies. [Support matrix](../reference/support-matrix.md) owns implementation and validation status; [changelog](../../CHANGELOG.md) owns delivery history.

Only current scope authorizes repository-planned feature implementation. Backlog and other planning material are context until explicitly promoted. The [agent policy](../agent-policy.md) owns implementation authority and the subsystem stop rule.

Version labels are candidate scopes without committed dates or release promises. Foundation and layout delivery remain historical records rather than a parallel phase-status table. Candidate scope is defined once, in current or backlog.

## Planning rules

- Each candidate has an objective, dependencies, bounded work, and observable exit criteria.
- Organize active work around one end-to-end integration slice. Name demonstrated blockers and minimum fixes instead of ordering whole subsystems for completion.
- Build the fixture with existing APIs first. Extend a subsystem only when that fixture cannot proceed or an existing contract/test requires a correctness fix; return to integration once the required invariants and exit criteria are satisfied.
- Establish full-tree correctness before incremental invalidation or aggressive batching.
- Read the relevant reflection, semantic, replay, coordinate, event/action, and command/transaction constraints before changing a boundary; a design dependency does not schedule an implementation.
- Prioritize keyboard/gamepad operation, mixed Latin/Japanese text, and consumer-driven editor primitives.
- Promote optional work explicitly before making it an exit gate.
- Move the required portion of a candidate between backlog and current when scheduling changes; do not copy active scope or promote adjacent mechanisms automatically.
- Remove completed tasks from current and record delivery/evidence with the [change-routing rules](../README.md#change-routing).

Design pages own technical contracts. Milestones reference those pages and name acceptance outcomes without reproducing the contracts.
