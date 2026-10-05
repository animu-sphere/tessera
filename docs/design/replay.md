# Deterministic UI replay

## Proposed recording contract

Replay reproduces a session through the ordinary runtime and injected host services. This is a versioned format/tool proposal; neither a replay CLI nor serialized replay fields are implemented by this document. Capability status is in [support](../reference/support-matrix.md).

A recording identifies:

- Document format/version and initial document or fixture reference, with explicit update/reload events.
- Logical viewport, device scale, and viewport/scale changes.
- Theme/style inputs and their revisions.
- Locale, font/fallback configuration, initial host-declared test state and its schema, and random seed when the host uses randomness.
- Ordered normalized input stream.
- Host time stream, including frame/update boundaries and animation-clock values.
- Resource readiness, failure, and replacement transitions, plus fixture font/asset identities.

Use fixture-owned resources and explicit readiness timing instead of relying on live filesystem/network completion. Define schema versioning, unsupported-version diagnostics, bounds, and deterministic event ordering before freezing the format. Document, replay, and semantic schema versions are distinct.

## Playback and outputs

Playback injects recorded inputs at the [defined update points](architecture.md#frame-scheduling), using the same runtime as application and editor preview. It observes:

- Layout geometry.
- [Semantic projection](semantics.md).
- Ordered application-facing [action requests](input.md#event-to-action-boundary).
- Backend-neutral paint command sequence.
- Optional GPU screenshots under declared backend/device tolerances.

Expect exact values where the numeric contract permits them; otherwise declare geometry tolerances. Compare command semantics and owned data rather than pointer addresses, cache allocation order, or process-specific handles. Resolve recorded targets through fixture identities with defined ambiguity/missing-target diagnostics.

GPU images are optional adapter evidence with target size, scale, color format, fonts, device and tolerance metadata. An image hash alone is not portable cross-driver equivalence. Core replay must remain runnable without a renderer.

[Inspection snapshot bundles](inspection.md#proposed-runner-and-snapshot-bundle) package observations at a selected generation; they do not replace the ordered recording. State restore uses the declared host adapter. Animation may be disabled or advanced at explicit fixture times, never read from an uncontrolled wall clock. Bundle exports and performance durations are separate from deterministic playback assertions.

## Tooling boundary

Recording and playback tooling may support regression tests, bug reports, navigation checks, CI, and Path-finder previews. A future CLI spelling is an open decision; do not document a runnable command until a tool exists.

Host clocks, resources, input normalization, and GPU scheduling remain host responsibilities. Replay supplies controlled adapters to those boundaries, rather than adding a second runtime or hidden global clock.

## Verification

The [testing guide](../guides/testing.md#replay-fixtures) owns test procedure. Format round trips, malformed/bounded input, repeated-run geometry/semantics/actions/paint, reload, scale changes, and resource readiness ordering need fixtures. Compare future incremental updates with full-tree playback. Publish supported configurations and actual results only in support.
