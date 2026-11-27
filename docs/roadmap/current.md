# Current

## v0.7.0 candidate: Application Experience

Shift from widget coverage to making an ordinary desktop application comfortable to build, operate, inspect and verify.

### Next operation

Snapshot bundle verification and comparison: the second slice of [snapshot bundles](../proposals/snapshot-bundles.md), after [capture](../architecture/inspection.md#implemented-snapshot-bundle-tooling). Use it to compare [Inspector scenarios](../guides/inspector-hierarchy.md#snapshot-bundles) from before and after a change without reading every member.

- `verify`: load a bundle with bounded validation, reject unsupported bundle or member versions, report missing, unlisted or unparsable members, and confirm that the listed Replay and Semantic JSON members load.
- `diff`: report incompatible conditions (bundle version, environment, text service, capture backend, glyphs, viewport or scale) before judging. Then report semantic, layout, action, paint-summary and pixel differences, in that order, as machine-readable JSON. Exit status distinguishes equal, different and invalid input.
- Match nodes by author ID, falling back to preorder position for unnamed nodes. Compare geometry with a declared tolerance and pixels exactly or by count over a threshold; perceptual methods stay out.

After this slice, choose the next bounded editing or observation need from the [Hierarchy / Inspector application](../guides/inspector-hierarchy.md) or the [backlog](backlog.md).

Implemented capabilities and limits: [support matrix](../reference/support-matrix.md).
Delivered work: [changelog](../../CHANGELOG.md).
