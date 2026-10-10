# Inspection, snapshots, and automation tooling

## Proposed shared inspection boundary

Tessera should be operable by humans, test runners, and external agents through the same runtime. This proposal defines inspection and tooling boundaries, not a stable public API, wire format, CLI, or build option; the [implemented prototype](#implemented-prototype-capture-and-target-resolution) covers a small in-process subset. Capability and configuration evidence belongs in [support](../reference/support-matrix.md).

DevTools, Path-finder, regression tools, and agent adapters consume a shared inspection layer. The layer depends on runtime contracts; the core never depends on an agent SDK, RPC server, MCP, or AI model. [Semantics](semantics.md) owns roles, names, state, and supported actions; [UI model](ui-model.md) owns identity and property metadata. Inspection joins these with layout and source information rather than maintaining another widget or accessibility tree.

Publish owned observations from one settled [update generation](architecture.md#update-and-snapshot-rules). External consumers must not retain borrowed node pointers or access mutable runtime internals. A response identifies its generation; an action against an expired generation fails even if an author ID has been reused.

## Proposed inspection records and source mapping

An element record should expose scoped author identity, component/type information when available, semantic role/name/value, ordered hierarchy, logical bounds, visibility and interaction state, supported actions, and links to property/style/layout observations. Keep runtime hierarchy and semantic hierarchy distinguishable: semantic flattening need not preserve every layout container.

Source mappings associate authored nodes/properties with file identity and spans through frontend lowering and reload. Generated components may need both definition and instance origins. Report unavailable source information explicitly; never guess a source line from a preorder index. Source maps are tooling metadata, not runtime behavior or implicit new JSON v1 fields.

Expose only host-declared serializable state slots. An application adapter validates typed values, owns capture/restore and any schema migration, and applies changes at update boundaries. No arbitrary memory access or enumeration of private application state is allowed. Fixture state can include host inputs that affect UI, but UI tooling does not become the owner of application state.

## Proposed target resolution and operations

Prefer targets in this order:

1. Explicit ID within a declared document/component scope.
2. Semantic role and accessible name.
3. Stable component/item key within its declared scope.
4. Explicit structural path.
5. Logical coordinates.

Style classes are presentation details and should not be stable automation identities. Selector grammar remains a decision; do not reuse CSS specificity as target resolution policy. A singular operation requires exactly one match and diagnoses missing, ambiguous, stale, hidden, disabled, or unsupported targets. Tree paths and coordinates are intentionally less stable across edits.

Distinguish semantic operations such as activate, focus, set value, toggle, or select from normalized pointer/key/text/scroll injection. Both follow [input eligibility and action dispatch](input.md#event-to-action-boundary); neither writes component fields or calls host callbacks directly. A pointer click scenario still exercises hit testing and press/release rules. Text entry must respect the editing/IME contract, and set-value requires a declared typed action. Logical coordinates are the default; physical or element-local inputs require explicit conversion.

Waiting observes an explicit predicate over settled generations with a bounded timeout or controlled frame/time limit. It must not add hidden sleeps or advance the runtime's clock implicitly.

## Implemented prototype capture and target resolution

[inspection.hpp](../../include/tessera/inspection/inspection.hpp) implements an in-process subset of the proposals above. It is a prototype, not a wire format or stable API, and it does not extend JSON v1.

- **Input and generation.** `InspectionInput` borrows the same coherent tree/resolved-style/layout snapshot as [semantic projection](semantics.md#implemented-prototype-projection), plus an optional source map and the optional host focus that the projection reports, and is rejected with the same located snapshot and focus codes. `capture_inspection` returns an owned `InspectionSnapshot` that borrows nothing. Its `generation` is the tree identity, so a handle built from it is a `NodeHandle` of that generation. No renderer, shaper, or host state is involved.
- **Element records.** Element *i* corresponds to tree preorder index *i* and records the runtime parent/children (including display-none nodes), kind, author ID, classes, event bindings, and the resolved style. `PropertyObservation`s list every descriptor accepted by the kind in [descriptor order](ui-model.md#implemented-property-metadata), with the effective value and whether it was authored. Displayed elements carry border/padding/content boxes, local visibility, the recorded layout clip, and scroll geometry for scroll boxes; display-none elements have no geometry. An element links to its entry in the snapshot's `SemanticTree` when it has one, keeping runtime and semantic hierarchy distinct. Projection warnings such as `missing_name` are kept as snapshot diagnostics.
- **Source mapping.** `load_document_with_sources` in [serialization.hpp](../../include/tessera/ui/serialization.hpp) returns the same document and diagnostics as `load_document`, plus a `DocumentSourceMap` with a caller-supplied file identity. Each preorder entry has the node's JSON pointer, its object span, and value spans for authored properties, as half-open UTF-8 byte ranges. The map is load-time tooling metadata, not a document field. A supplied map whose node count or property names disagree with the tree fails with `source_map_mismatch` at `/sources/nodes` or `/sources/nodes/<index>`. Without a map, the snapshot reports `SourceStatus::not_supplied` and elements have no source, rather than a guessed location. With a map, warnings located at `/nodes/<index>` gain that node's byte offset.
- **Target resolution.** `resolve_target` resolves exactly one element of a snapshot: by author ID (document scope; display-none elements included), by semantic role plus exact name, by an explicit child-index path from the root, or by a logical point. A point selects the last visible displayed element in preorder whose half-open border box and recorded clip contain it. This includes disabled elements and is an inspection query, not pointer targeting. No match fails as `target_not_found` at `/target` (or `/target/children/<step>` for an out-of-range path step). Several role/name matches fail as `ambiguous_target`, listing the candidates. Non-finite points are `invalid_number`. An optional scope handle restricts every query to that authored subtree, including its root. Scoped paths start at the scope; points stay in root logical coordinates. A foreign or out-of-bounds scope fails as `stale_target` at `/scope`. Without a scope the whole document is searched; component keys have no target form.
- **Operations.** Resolution performs no action. A resolved handle is invoked through [`request_semantic_action`](semantics.md#implemented-prototype-projection), which applies ordinary hidden/disabled eligibility and rejects a handle from an earlier generation as `stale_target`, even after a reload that reuses every author ID. [Replay](replay.md#implemented-prototype-playback) focus and semantic action steps resolve their targets this way in each step's current generation, and a [replay session](replay.md#implemented-prototype-playback) is a controlled host fixture without a window: it captures its current generation for a consumer and records the consumer's handle operations as replayable steps.

## Proposed localization observations

Join [localization](localization.md) resolution results with text, layout and source observations from one settled generation. A locale Inspector or agent report can expose requested and resolved locale, effective UI/paragraph direction, catalog identity/revision and resource source, message ID, fallback chain/hit, missing-message or formatting diagnostic, and selected plural/select branch. Text observations supply script runs, concrete font faces and missing-glyph/fallback information; layout supplies logical bounds, clip and overflow observations. Optional screenshot regions refer to the same generation's completed capture.

Keep message IDs distinct from node/component/command identity. Translated accessible names are observable values; cross-locale scenarios should target stable scoped author IDs or keys rather than assuming a name remains equal. Preserve the shared diagnostic envelope and source mapping. Catalog/argument contents and paths follow host export/redaction policy; report unavailable information explicitly.

Static message-ID extraction, missing/unused-key and argument checks, glossary checks, and translation coverage are tooling candidates over the catalog schema and supported frontend syntax. Dynamic IDs must be reported as unresolved or declared explicitly, rather than counted as complete coverage. Reports distinguish fallback use, missing translation, missing glyph and layout overflow; numeric overflow follows the [invariant policy](#proposed-ui-invariants). These observations add no CLI or transport contract.

## Proposed runner and snapshot bundle

Separate interactive application execution from deterministic one-shot observation. An offscreen runner owns host adapters for viewport/scale, time/frame count, normalized input, fixture resources, and declared application state. It must not require an OS window. The [implemented prototype](#implemented-prototype-offscreen-runner) covers recorded viewport/scale, locale, text-service identity, input, and declared completed-frame capture. Screenshots come from an explicitly declared backend: the proposed [CPU reference backend](rendering.md#proposed-cpu-reference-backend) is the default for deterministic visual fixtures, and GPU capture requires a declared device/driver. Neither substitutes automatically for the other.

[Replay](replay.md) owns reproducible inputs and action order. A snapshot is an observation of one settled generation, while a replay describes progression between generations. Snapshot bundle versioning is independent of UI-document, semantic, and replay versions. Proposed bundle contents:

| Artifact | Content and owner |
| --- | --- |
| Manifest | Bundle version, generation, fixture/input references, viewport/scale, locale, theme, font/asset identities, capture capabilities and rendering environment |
| Screenshot | Optional completed frame readback; [rendering](rendering.md) owns target/color/lifetime rules |
| Tree | Semantic projection plus explicitly identified runtime inspection records |
| State | Host-declared reproducible input state, excluding private or sensitive slots |
| Layout | Logical geometry and available clip/scroll information from [layout](layout.md) |
| Render | Owned command summary and backend counters; no SDK objects, addresses, or GPU handles |
| Diagnostics | Structured issues associated with the captured generation |
| Metrics | Allocation/memory and render counters when available, plus optional CPU/GPU timing with measurement conditions |

Capture CPU observations coherently, then associate completed readback with that same generation/frame. Do not combine a later semantic tree with an earlier image. Missing artifacts carry an explicit reason; a core-only snapshot needs no screenshot. Names and encoding of bundle members remain open until bounded validation, version rejection, and round trips are specified.

## Implemented prototype offscreen runner

[offscreen_runner.hpp](../../include/tessera/inspection/offscreen_runner.hpp) defines `run_offscreen(OffscreenInput)` in core. It is a window-free, in-process prototype, not a CLI, snapshot bundle, or stable API. Frames come only from a host adapter of the [capture boundary](rendering.md#implemented-capture-boundary); core chooses, constructs, and substitutes no backend.

- **Inputs.** `OffscreenInput` borrows a `ReplayRecording`, the text service its [environment](replay.md#implemented-prototype-playback) declares, an optional declared `CaptureBackend`, and an optional `FrameCapture` adapter. A missing recording or text service is `missing_input` at `/recording` or `/text`. A declaration requires an adapter (`missing_input` at `/frames`), an adapter requires a declaration (`missing_input` at `/capture`), and the adapter's `backend()` must equal the declaration (`capture_mismatch` at `/capture`); declaration diagnostics are relocated under `/capture`. These checks run before playback, so an unavailable or different backend fails without capturing anything. The host verifies a font-profile declaration, for example with `verify_replay_fonts`, before running; core neither embeds nor checks the profile.
- **Playback and frames.** The recording plays exactly as `play_replay`; its diagnostics and warnings are relocated under `/recording` (for example `/recording/steps/2/viewport/width` or `/recording/nodes/3`). All replay observations settle before any capture. Then `OffscreenRun::frames` holds one `OffscreenFrame` per output generation, in order, with the generation index, the producing generation's owned logical viewport, device scale, animation-clock value, and its `capture_extent`. A generation whose extent is invalid fails as `out_of_range` under `/generations/<g>/capture`, even without a declared backend.
- **Capture.** Without a declaration the run is core-only: every frame has status `not_declared` and no image, and the manifest has no backend. With one, each generation's owned paint list is passed to the adapter in generation order with its viewport, scale, extent, and explicit animation time; the adapter returns only a completed frame. The image must have the requested extent, the declared format, and `width * height * 4` bytes (`capture_mismatch` at `/generations/<g>/capture`). Adapter diagnostics are relocated under that path; a failure without an error diagnostic is `capture_failed`. Images are bounded to `max_capture_bytes` (256 MiB) per run, checked before each request (`out_of_range`). Any failure returns no partial run; already completed adapter frames are the host's to discard.
- **Manifest and determinism.** `OffscreenManifest` records the replay version, the recording's whole environment (scale, locale, and placeholder or font-profile identity), and the declared backend. A run owns its manifest, replay output, frame conditions, and pixels, and borrows nothing; repeated runs with equal inputs and a deterministic adapter compare equal, image bytes included. Frame numbering, target reuse, resource preparation such as glyph atlas pages, and retirement stay inside the adapter.

The runner captures every output generation, including the declared boolean state and readiness transitions of the [controlled host update boundary](replay.md#implemented-controlled-host-updates). The manifest owns initial slot declarations; each replay generation owns its settled observations alongside its paint. The runner evaluates no animation, executes no host provider or application state binding, and encodes no image.

## Proposed diagnostics and comparisons

Tooling diagnostics need stable code, severity, category, actionable message, node/property location, optional source span, and typed contextual data. Categories include document/style/layout/render/resource/input/state/accessibility/performance. Hints may be deterministic rules; no model is needed. Preserve existing validation codes, severities, and locations when adapting them; the proposed categories/context/hints do not imply that the current diagnostics already implement this envelope. Numeric overflow observations need policy because intentional scrolling/overlays can extend beyond a parent.

Snapshot comparisons should report semantic additions/removals/state/action changes, logical geometry changes, render-counter changes, and optional visual differences in machine-readable form. Match scoped stable identities, not process handles or allocation order. Incompatible versions or fixture conditions must be reported before judging a regression. Exact/threshold pixel comparisons come first; perceptual methods are later choices. An image change is evidence to explain alongside semantic/layout changes, not a complete behavior assertion.

Comparisons are diagnosable in a fixed order, so a report explains a failure at the most semantic level that changed: semantic diff, layout diff, action diff, paint diff, visual diff, then performance counter diff. Pixel differences alone never establish or rule out a behavior change.

## Proposed reactive observations

Development inspection can join [reactive graph](reactive-runtime.md) and owner-tree observations with presentation/source records from one settled generation. Record lifetime-aware computation/owner IDs, dependencies/dependents, accepted value revisions, potential dirtiness, evaluation counts, and the cause of invalidation. A bounded causal trace connects a changed source through derivation, measurement/layout, and paint work so an Inspector or agent can explain why a node updated. Durations are optional measured observations rather than deterministic graph state.

Snapshots own their records, identify unavailable fields, and carry explicit record/edge/trace limits with truncation reporting. Graph exports use the same observations; JSON/DOT encoding, transport operations, and UI panels are separate API decisions. They expose no addresses, mutable storage, executable closures, or private application values. Source names/values are host-declared exports under the [production boundary](#proposed-tooling-and-production-boundary). Observation never flushes pending work or creates dependency edges.

## Proposed performance metrics

A GPU-native renderer is not itself a performance guarantee; CPU and GPU work are both measured.

| Side | Metrics |
| --- | --- |
| CPU | Document update, reconciliation, style resolution, layout, semantic projection, paint generation, text shaping, glyph cache |
| Reactive | Invalidated/evaluated computations, replaced dependency edges, layout nodes touched, paint items rebuilt, allocations per update, owner creation/disposal, queue depth, and update latency |
| GPU | Draw and instance counts, upload bytes, atlas pages, render passes, GPU timing when the backend supplies it |
| Iteration | Edit-to-visible-result latency, broken down into parse, update, layout, paint, submit, completion/readback, and export |

Iteration latency, not frame rate alone, is the first-class metric for live reload, Path-finder, and agent loops. Performance comparisons identify workload, changed-node count, build/backend/device, warmup, sample count, and measurement method. Separate deterministic counters from noisy durations; absent GPU timing is unavailable, never zero. Consumer-specific budgets require representative baselines and explicit tolerance policy. These are instrumentation goals, not promised millisecond or framework-speedup targets.

Measure work proportionality by varying total UI/graph size and the changed region independently. Use chains, fan-out, diamonds, dynamic branches, owner churn, keyed reorder, one changing label among static content, theme changes, viewport resize, and streaming/virtualized consumers. Compare evaluated/touched counts and upload bytes with a full-update baseline; avoid adopting illustrative million-node or widget-count targets as guarantees.

## Proposed UI invariants

UI invariants are machine-readable assertions evaluated against one captured generation, shared by unit and integration tests, the offscreen runner, CI, Path-finder, coding agents, and replay. Illustrative predicates:

```text
visible("#save")          enabled("#save")          focusable("#search")
semantic_name("#save") == "Save"
no_overlap(".toolbar > *")                          inside_viewport("#dialog")
layout_time < budget      draw_calls <= budget      glyph_atlas_pages <= budget
```

Selectors use [target resolution](#proposed-target-resolution-and-operations), so an ambiguous or missing target is a diagnosed failure, not a vacuous pass. Structural predicates read semantic, layout, and focus observations; performance predicates read the metrics above under declared measurement conditions and stay separate from deterministic predicates. A failure reports the predicate, the resolved targets, the generation, and the observed values in the diagnostics envelope. Invariants are evaluated by tooling and never change runtime behavior. Predicate spelling and the scenario format are open decisions.

## Proposed agent capability manifest

Agents operate UI through meaning rather than screen scraping. A capability manifest describes, for one generation and one adapter's permissions, the [commands](commands.md) with their eligibility and argument schemas, the inspectable surfaces (document, hierarchy, selection, properties), and the semantic actions (activate, select, focus, set value). It is a projection of the semantic tree, the command registry, and inspection records, not an agent-specific runtime API. Development-only commands and private state stay out of production manifests, and the manifest version is independent of document, replay, and bundle versions.

## Proposed generation history

Replay already describes progression between generations; time-travel inspection keeps selected observations per generation so tools can step through them. A history entry can hold the document generation, declared state snapshot, semantic tree, layout, focus, the command or action that produced it, paint counters, and resource readiness. DevTools then show differences between generations, focus changes, layout changes, command history, and resource transitions. History is bounded by count or bytes with explicit eviction, observes only owned data, and stays a development facility. A failure reproduction bundle packages the replay recording with the observations needed to reproduce it; a performance regression bundle adds the measurement conditions.

## Proposed tooling and production boundary

Prove the in-process inspection/action API first, then a small CLI with machine-readable results, then bounded stdin/stdout JSON-RPC. Add local RPC, subscriptions, and optional MCP adapters only for a consumer. External automation handles inspect, invoke, replay, capture, diff, and assert consistently through these layers. The intended agent loop is short: edit, reload, capture a screenshot with layout/semantic dumps, compare, assert invariants, and edit again. Remote mutation is limited to declared commands, actions, and state slots; there is no unrestricted mutation API. CLI command names, option names, transport schema, and build switches remain undecided. A thin versioned JSON/YAML scenario layer may compose the same operations; it must not introduce another UI language or runtime.

State injection, source paths, detailed diagnostics, and mutation transports are explicitly enabled development/test facilities that can be excluded from production builds. Do not start a listener or expose private state by default. A local server needs an explicit access policy, request bounds, and lifetime/cancellation rules. Human Inspector and agent tooling share observations, while each adapter controls which operations it exposes.

## Verification

[Inspection checks](../../tests/inspection/inspection_tests.cpp) verify capture determinism, joined property/style/geometry/semantic records, source spans with explicit absence, rejection of incoherent snapshots and mismatched maps, each target form with missing and ambiguous targets, pointer-equivalent invocation, and stale-generation rejection after reload. [Runner checks](../../tests/inspection/offscreen_runner_tests.cpp) verify core-only frames, generation-matched requests and images across scale, scroll, and resize, repeated and Replay JSON-restored equality, relocated diagnostics, and rejection of missing or mismatched declarations, wrong images, zero extents, and the byte bound; [Vulkan fixtures](../../tests/render/vulkan_tests.cpp) implement the adapter with offscreen completion and readback. Proposed records, invariants, manifests, bundles, comparisons, and transports follow [testing](../guides/testing.md#inspection-and-snapshot-fixtures). Transport tests follow the in-process contract rather than replacing algorithm checks.
