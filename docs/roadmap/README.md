# Roadmap

[Current](current.md) owns the active milestone and remaining work. [Backlog](backlog.md) owns inactive version candidates and their dependencies. [Support matrix](../reference/support-matrix.md) owns implementation and validation status; [changelog](../../CHANGELOG.md) owns delivery history.

Version labels are candidate scopes without committed dates or release promises. Foundation and layout delivery remain historical records rather than a parallel phase-status table. Candidate scope is defined once, in current or backlog.

## Planning rules

- Each candidate has an objective, dependencies, bounded work, and observable exit criteria.
- Establish full-tree correctness before incremental invalidation or aggressive batching.
- Define reflection, semantic, replay, coordinate, and event/action boundaries early; introduce implementations at their owning milestones.
- Prioritize keyboard/gamepad operation, mixed Latin/Japanese text, and consumer-driven editor primitives.
- Promote optional work explicitly before making it an exit gate.
- Move a candidate's scope between backlog and current when scheduling changes; do not copy it.
- Remove completed tasks from current and record delivery/evidence with the [change-routing rules](../README.md#change-routing).

Design pages own technical contracts. Milestones reference those pages and name acceptance outcomes without reproducing the contracts.
