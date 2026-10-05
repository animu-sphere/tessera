# Inspection, snapshots, and automation tooling

## Proposed shared inspection boundary

Tessera should be operable by humans, test runners, and external agents through the same runtime. This proposal defines inspection and tooling boundaries, not a public API, wire format, CLI, or build option. Capability and configuration evidence belongs in [support](../reference/support-matrix.md).

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

## Proposed runner and snapshot bundle

Separate interactive application execution from deterministic one-shot observation. An offscreen runner owns host adapters for viewport/scale, time/frame count, normalized input, fixture resources, and declared application state. It must not require an OS window. GPU rendering still requires a declared device/driver; a software/reference renderer is a separate future choice, not an automatic fallback.

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

## Proposed diagnostics and comparisons

Tooling diagnostics need stable code, severity, category, actionable message, node/property location, optional source span, and typed contextual data. Categories include document/style/layout/render/resource/input/state/accessibility/performance. Hints may be deterministic rules; no model is needed. Preserve existing validation codes, severities, and locations when adapting them; the proposed categories/context/hints do not imply that the current diagnostics already implement this envelope. Numeric overflow observations need policy because intentional scrolling/overlays can extend beyond a parent.

Snapshot comparisons should report semantic additions/removals/state/action changes, logical geometry changes, render-counter changes, and optional visual differences in machine-readable form. Match scoped stable identities, not process handles or allocation order. Incompatible versions or fixture conditions must be reported before judging a regression. Exact/threshold pixel comparisons come first; perceptual methods are later choices. An image change is evidence to explain alongside semantic/layout changes, not a complete behavior assertion.

Performance comparisons identify workload, changed-node count, build/backend/device, warmup, sample count, and measurement method. Separate deterministic counters from noisy durations; absent GPU timing is unavailable, never zero. Consumer-specific budgets require representative baselines and explicit tolerance policy. Measure source edit to verified snapshot as well as parse, update, layout, paint, submit, completion/readback, and export costs. These are instrumentation goals, not promised millisecond or framework-speedup targets.

## Proposed tooling and production boundary

Prove the in-process inspection/action API first, then a small CLI with machine-readable results, then bounded stdin/stdout JSON-RPC. Add local RPC, subscriptions, and optional MCP adapters only for a consumer. CLI command names, option names, transport schema, and build switches remain undecided. A thin versioned JSON/YAML scenario layer may compose the same operations; it must not introduce another UI language or runtime.

State injection, source paths, detailed diagnostics, and mutation transports are explicitly enabled development/test facilities that can be excluded from production builds. Do not start a listener or expose private state by default. A local server needs an explicit access policy, request bounds, and lifetime/cancellation rules. Human Inspector and agent tooling share observations, while each adapter controls which operations it exposes.

## Verification

Use [testing](../guides/testing.md#inspection-and-snapshot-fixtures) for coherent generations, source mapping, selector ambiguity/staleness, action eligibility, declared-state validation, headless/core-only behavior, bundle version failures, comparison reports, and production exclusion. Transport tests follow the in-process contract, rather than replacing algorithm checks. Scope and implementation order belong in [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md).
