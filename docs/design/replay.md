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

## Implemented prototype playback

[replay.hpp](../../include/tessera/replay/replay.hpp) implements an in-process subset of this contract. It is a prototype, not a serialized format or stable API, and it does not extend JSON v1.

- **Recording.** `ReplayRecording` holds a replay version (only `replay_prototype_version` 0 is accepted), the host's declared actions as a `ValidationContext`, the logical viewport, an initial `UiDocument` with one `ResolvedStyle` per node in tree preorder, and at most `max_replay_steps` (10000) ordered steps. A step is a normalized `InputEvent`, a `ReplayResize` with a new logical viewport, or a `ReplayReload` with a replacement document and its styles. Input steps are pointer events, `Scroll`, or the logical commands `FocusNext`, `FocusPrevious`, `Navigate`, `Activate`, and `Cancel`; keys are translated by the host before recording, as for the dispatcher. Styles are supplied already resolved; recordings carry no stylesheet or interaction state for the [prototype resolver](styling.md#implemented-prototype-style-resolution). Device scale, time apart from input timestamps, locale, fonts, resources, and host state are not recorded; the caller injects the `TextShaper`.
- **Playback.** `play_replay(recording, shaper)` runs the ordinary `UiTree::create`, [`compute_layout`](layout.md#implemented-prototype-algorithm), [`build_paint_list`](rendering.md#implemented-paint-generation), [`PointerDispatcher`](input.md#implemented-pointer-dispatch), [`FocusDispatcher`](input.md#implemented-prototype-focus-dispatch), and [`build_semantic_tree`](semantics.md#implemented-prototype-projection) path without a renderer. The initial snapshot and each resize/reload/scroll step settle a full-tree generation and then `refresh` pointers and focus; a reload creates a new tree identity, so a held press is cleared and focus is restored by author ID as for any snapshot replacement, and focus on a node that became disabled or hidden moves by the dispatcher's recovery rule. A command step goes through focus dispatch against the current snapshot without settling a generation. Pointer steps never move focus; a host pointer-press focus policy is not replayed. All input steps share one non-decreasing timestamp stream (`event_order` at `/steps/<index>/timestamp`). A scroll step goes through [scroll routing](input.md#implemented-scroll-routing) against the current snapshot; playback stores the returned offsets as layout's requested offsets, which a reload resets to zero and a resize keeps for layout to re-clamp. `KeyDown`, `KeyUp`, and `TextInput` are rejected as `unsupported_event` at `/steps/<index>/event`.
- **Outputs.** `ReplayOutput` owns, per generation, the producing step (absent for the initial snapshot), every layout box as preorder node index, parent index, border box, border/padding edges, visibility, optional clip, and optional scroll geometry, and the owned paint list. Semantic observations are recorded after each generation settles and after each command step, in that order: each `ReplaySemantics` holds the observing step, its generation, the focused node's preorder index (present even when the node has no entry), and the projected entries with preorder node and `labelled_by` indices in place of handles. Action requests from pointer and command steps are recorded in order with their input step, generation, binding, action name, binding-owner preorder index, and author ID when present. No `NodeHandle` or tree identity escapes, so repeated playback compares equal.
- **Failures.** An unsupported version is `unsupported_version` at `/version`; too many steps is `out_of_range` at `/steps`. Other diagnostics come from the reused stage and are relocated under their source: document diagnostics under `/document` or `/steps/<index>/document`, layout/paint/dispatch diagnostics under the root for the initial snapshot or `/steps/<index>` for a step (for example `/steps/2/timestamp` or `/steps/0/viewport/width`). Failure returns no partial output. Semantic warnings such as `missing_name` and `focus_not_exposed` are relocated the same way per observation (for example `/nodes/1` initially and `/steps/0/nodes/1` after a command) and returned with a successful output.
- **Comparison.** `compare_replay(expected, actual, geometry_tolerance)` reports one `replay_mismatch` per difference, located in the expected output: counts at `/generations`, `/generations/<g>/boxes`, `/generations/<g>/paint/commands`, `/semantics`, `/semantics/<s>/nodes`, and `/actions`; fields such as `/generations/<g>/boxes/<b>/border_box` and `/semantics/<s>/focused`; and whole commands, semantic entries, and actions at their index. Box rectangles, clips, edges, and scroll extents/offsets match within the non-negative finite tolerance in logical units (inclusive); clip and scroll presence, node/parent indices, visibility, steps, paint commands, semantic observations, and actions must be equal. An invalid tolerance is reported at `/geometry_tolerance`.

Recorded-target resolution through fixture identities, programmatic or pointer-press focus steps, resource readiness, animation time, scale changes, and a versioned serialized format remain proposals above.

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

The [testing guide](../guides/testing.md#replay-fixtures) owns test procedure. [Replay checks](../../tests/replay/replay_tests.cpp) cover prototype playback of clicks, drag-out, resize, and reload with a held press, and of a scroll step followed by a click on the moved button; command steps that request the same actions as clicks, with per-step semantic focus, focus restored across a reload by author ID, and recovery from a disabled focused button; observed geometry/paint/semantic/action identities and located semantic warnings; repeated-run equality; located comparison mismatches and tolerance; and rejected versions, bounds, documents, styles, viewports, key input, scroll deltas, and timestamp order across pointer, scroll, and command steps. Format round trips and malformed serialized input, scale changes, and resource readiness ordering still need fixtures. Compare future incremental updates with full-tree playback. Publish supported configurations and actual results only in support.
