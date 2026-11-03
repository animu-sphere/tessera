# Roadmap

[Current](current.md) owns the active milestone and remaining work. [Backlog](backlog.md) owns inactive version candidates and their dependencies. [Support matrix](../reference/support-matrix.md) owns implementation and validation status; [changelog](../../CHANGELOG.md) owns delivery history.

Version labels are candidate scopes without committed dates or release promises. Candidate scope is defined once, in current or backlog.

## Planning rules

- Each milestone has an objective, a reference application and observable exit criteria.
- Build the application with existing APIs where they fit; add widgets, host integration or runtime changes when the application needs them, and ship each piece complete with its tests and docs.
- Establish full-tree correctness before incremental invalidation or aggressive batching.
- Prioritize keyboard/gamepad operation, mixed Latin/Japanese text, and consumer-driven editor primitives.
- Measure representative applications before performance work.
- Move completed work out of current and record it in the changelog; move candidates between backlog and current when scheduling changes.

Design pages own technical contracts. Milestones reference those pages and name acceptance outcomes without reproducing the contracts.
