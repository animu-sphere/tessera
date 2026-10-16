# Backlog

Inactive milestone candidates live here; [current](current.md) owns the active scope. Versions are candidates without committed dates. Implementation and validation status is owned by the [support matrix](../reference/support-matrix.md).

Everything here is planning context and does not authorize implementation during the active milestone. Promote only a demonstrated fixture requirement into current, with a minimum fix, observable acceptance and a stop condition under the [agent policy](../agent-policy.md). A detailed design or a candidate's dependencies do not activate adjacent work.

Inspection foundations enter early so DevTools, accessibility, testing, and agents share the same identity/semantic/action boundaries. Build in-process observation and controlled host fixtures before CLI, snapshot comparison, transport, or optional MCP integration. Web shares this foundation; its broader host implementation follows runtime/text/lifecycle parity prerequisites below. External strategy phase numbers do not replace these candidate scopes.

Choose follow-up scope from the Path-finder mini prototype's measured application requirements and authoring friction. Candidate themes include richer layout, incremental stage evaluation, retained rendering, accessibility, animation, broader platform adapters, application bindings, and advanced agent authoring. Generalized virtualization, typed drag and drop, data grids, and context depend on the active application primitives; generation history, timeline/plotting, collaboration, Web/WASM, and world-space UI follow concrete consumers. Items within a candidate are listed in dependency order, without making every theme a committed next-release requirement.

## Unscheduled editor extensions

These broader mechanisms are inactive. Use the [bounded editor fixture](../integrations/path-finder.md#implemented-in-process-panel-fixture) to select a follow-up under [current work](current.md#current-slice). Reconsider these candidates from concrete fixture friction instead of treating them as a prerequisite sequence:

- **Lifecycle automation and UI fallback:** generalized [declarations](../proposals/automatic-component-lifecycle.md#proposed-automatic-component-lifecycle), declaration/default origins, setup/cleanup conveniences and [contained UI fallback](../proposals/error-boundaries.md#proposed-error-boundaries) beyond the fixture's owner/state/disposal requirements.
- **Resource observations:** [owned resource payloads](../proposals/async-resources.md#proposed-async-state), real thumbnail/image loading, provider integration and completion-safe retirement beyond a host-driven fixture observation; cancellation and logical ownership must preserve the [frame lifetime](../architecture/rendering.md#implemented-frame-contract).
- **Broader reload and reflection:** [state-schema migrations and compatibility](../proposals/live-reload.md#live-reload-transaction) across scroll anchors, accepted resources and command owners; richer [declaration/default origins and typed diagnostic context](../architecture/inspection.md#proposed-inspection-records-and-source-mapping); [reflection observations](../proposals/property-reflection.md#proposed-property-reflection) for inherited/resolved/read-only values and new property kinds. Preserve affected-stage metadata without making incremental evaluation a prerequisite.
- **Overlays and palette:** shared [Portal ownership](../proposals/overlays-and-portals.md#proposed-overlays-and-portals), [placement](../architecture/layout.md#scrolling-and-overlay-geometry), [focus/targeting](../architecture/input.md#proposed-overlay-interaction), [semantic relationships](../architecture/semantics.md#proposed-semantic-projection), composed Tooltip/Dropdown/Modal and [command palette](../proposals/command-palette.md#proposed-command-palette). Include native capture/cancellation and platform shortcut conventions only for an explicit consumer, with transform hit/render agreement and ordinary command eligibility.
- **Large editor lists:** bounded [VirtualList realization](../proposals/virtualization.md#proposed-virtualization-foundation) over thousands of keyed logical rows, deterministic measurement, focus/selection/scroll anchors, async rows and an explicit semantic virtualization policy. A small keyed hierarchy does not require this mechanism.
- **Editing expansion:** multiline convenience, host clipboard adapters and platform composition integration beyond the active single-string operation; preserve the [revisioned editing boundary](../architecture/input.md#proposed-editing-ime-and-clipboard-boundary) and [logical replay inputs](../architecture/replay.md#proposed-editor-workflow-inputs). Native shortcut adapters, additional platform/gamepad chords, customization, draft-local undo and generalized input routing stay here; only the two normalized label-history chords are scoped in [current work](current.md#current-slice).
- **Packaged verification and discovery:** [versioned snapshot bundle/CLI](../proposals/snapshot-bundles.md#proposed-runner-and-snapshot-bundle), machine-readable differences, selective visual comparisons and source-edit-to-verification instrumentation; [agent capability manifest](../proposals/agent-capability-manifest.md#proposed-agent-capability-manifest) for ordinary inspect/invoke/replay/capture/invariant/source-edit/reload/comparison operations. An in-process fixture is sufficient before a concrete consumer needs packaging or discovery.
- **Broader performance evidence:** representative application profiling, Vulkan/CPU scene comparisons across declared configurations and authoring-friction measurements to select follow-up scope. Do not turn profiling infrastructure or aggressive evaluation/GPU optimization into editor completion prerequisites.

## v0.4.0 candidate — Productive tool UI

Objective: extend primitives required by measured editor/utility workloads without growing a general widget catalog. Refine the candidate scope after the Path-finder prototype; Mimikuri and utility applications provide additional consumer requirements.

Depends on component identity, text/editing boundaries, and the Path-finder prototype.

Candidates:

- [Generalized virtualization](../proposals/virtualization.md#proposed-virtualization-foundation) with production VirtualList and tree/hierarchy primitives, then DataGrid/TreeTable composed from primitives: resizable, sortable, pinned, and hideable columns; single and multi-selection; editable cells; keyboard navigation; row hierarchy; context menus.
- Resizable split panels and a minimal docking subset.
- Extend the [string-editing consumer](../integrations/path-finder.md#implemented-in-process-panel-fixture) with platform IME/clipboard hardening and richer selection/multiline behavior where consumers require it; [typed drag and drop](../architecture/input.md#proposed-typed-drag-and-drop).
- [Context](../architecture/ui-model.md#proposed-context) for subtree services, including [theme scopes](../architecture/styling.md#proposed-theme-scopes); transaction/undo integration beyond the [bounded editor label history](../integrations/path-finder.md#implemented-in-process-panel-fixture).
- Canvas/custom paint, [performance instrumentation](../architecture/inspection.md#proposed-performance-metrics), and a timeline/profiling primitive.
- Broader consumer-specific UI invariants and regression budgets over the prototype's verification loop; packaged bundles and CI consumers need explicit promotion.
- [Stage invalidation](../proposals/incremental-evaluation.md#proposed-stage-invalidation) and incremental style, measure/placement, [text](../proposals/incremental-evaluation.md#proposed-incremental-text-invalidation), input, semantic, and paint evaluation over the active milestone's reactive kernel, verified against full-tree output.
- Bounded [reactive observations](../architecture/inspection.md#proposed-reactive-observations), owner/dependency inspection and update-cause traces, plus graph/work-proportionality benchmarks without fixed illustrative scale targets.
- Representative dense/high-frequency tool benchmarks, measured iteration latency, and consumer-specific regression budgets after recording a baseline; thin scenario tooling and compact CI reports/artifacts.

Exit criteria to refine from a consumer prototype: long lists, trees, and grids preserve keyed selection/focus and scroll anchors, Japanese composition and clipboard work under declared platform evidence, custom paint respects the common draw/resource contract, and incremental results match full-tree output. Benchmark/iteration reports identify representative fixtures and measurement conditions; consumer budgets and CI reports distinguish timing noise from deterministic counter regressions.

Owners: [reactive runtime](../architecture/reactive-runtime.md), [UI model](../architecture/ui-model.md), [layout](../architecture/layout.md), [input](../architecture/input.md), [commands](../architecture/commands.md), [text](../architecture/text.md), [rendering](../architecture/rendering.md), [styling](../architecture/styling.md), [inspection](../architecture/inspection.md).

## v0.5.0 candidate — Accessibility, automation, and robust runtime

Objective: make UI reliably inspectable and operable through accessibility and external automation adapters.

Depends on stable semantics/actions, replay, and lifecycle contracts.

Candidates:

- Native accessibility adapters and external semantic invocation using the earlier shared inspection foundation.
- Replay recording tools, focus recovery/modal navigation hardening.
- Resource/device-loss diagnostics, install/export packaging, downstream integration checks.
- [Agent capability manifest](../proposals/agent-capability-manifest.md#proposed-agent-capability-manifest), command discovery, and semantic automation through the shared command path; choose the first version's scope from an explicit consumer.
- [Generation history](../proposals/generation-history.md#proposed-generation-history) for time-travel inspection, failure reproduction bundles, and performance regression bundles.
- Persistent automation adapters built on the ordinary inspection/action API: bounded stdin/stdout JSON-RPC first, local RPC/subscriptions only for a consumer, optional MCP last; explicit development enablement and production exclusion.

Exit criteria to refine per adapter: external automation handles inspect, invoke, replay, capture, diff, and assert consistently; semantic invocation obeys ordinary action and command eligibility, stale identities fail safely, adapter behavior has platform-specific evidence, replay reproduces failures, and downstream consumers exercise documented lifetime boundaries. Transport validation must cover request limits, cancellation/teardown, permitted state slots, and exclusion of development mutation/source exports from production.

Owners: [semantics](../architecture/semantics.md), [input](../architecture/input.md), [commands](../architecture/commands.md), [replay](../architecture/replay.md), [rendering](../architecture/rendering.md), [dependencies](../reference/dependencies.md), [inspection](../architecture/inspection.md).

## Later — WebGPU, WASM, and world-space UI

Prerequisites: stable document, draw/resource and text boundaries, and replay fixtures suitable for parity checks.

Evaluate native WebGPU, browser WebGPU hosting, and WASM separately. The intended Web path is the shared runtime in WASM, WebGPU canvas visuals, and semantic DOM/browser editing adapters under [Web host](../proposals/web-host.md).

Candidates:

- WASM packaging and browser host with explicit device/canvas/input/asset ownership and a thin typed JS state/action bridge.
- Semantic DOM accessibility and focus synchronization from the common SemanticTree.
- Hybrid browser text editing, Japanese IME, selection and clipboard; evaluate mobile keyboard/autofill/password-manager needs separately.
- Worker/OffscreenCanvas only after single-thread correctness and measured need, with ordered generation-aware messages and teardown.

Exit criteria to refine per host: equivalent document/state/geometry/actions and selective rendered fixture parity under declared tolerances; semantic DOM focus/removal and Japanese editing work in recorded browser configurations; bridge lifetime and malformed inputs are tested. A backend name or shader target does not establish browser support.

World-space adapters and streamed resources require bounded host requirements. Metal/D3D12 need a concrete consumer before scheduling.

Owners: [architecture](../architecture/overview.md), [Web host](../proposals/web-host.md), [rendering](../architecture/rendering.md), [semantics](../architecture/semantics.md), [input](../architecture/input.md), [dependencies](../reference/dependencies.md), [replay](../architecture/replay.md).

## Later — Localization and international layout

Objective: [localized messages](../architecture/localization.md) and runtime language changes participate in the same reactive, semantic, layout and verification contracts as other UI values.

Prerequisites: reactive bindings and coherent publication, the v0.4.0 subtree context, host resource/reload ownership, and text measurement/paint agreement. Contract design can inform those foundations without adding the external direction's implementation phases to the active milestone.

Candidates, in dependency order:

- Define validated locale identity, semantic message references/typed arguments and provider integration in the common IR before frontend convenience APIs or resource encodings; settle inline direction and logical-edge rules with [layout](../architecture/layout.md#proposed-logical-direction-and-edges).
- Owned versioned catalogs, centralized fallback and missing-message policy, runtime locale switching and atomic catalog replacement with last-valid-state preservation.
- Plural/select and number/date/time/unit formatting behind optional services; evaluate MessageFormat/ICU4X under [dependency adoption](../reference/dependencies.md#proposed-localization-choices).
- Language/script-aware text inputs and direction-aware placement/icons, coordinated with the [international text algorithms](#later--typography-expansion); introduce RTL consumer fixtures without waiting for every typography extension.
- [Locale observations](../architecture/inspection.md#proposed-localization-observations), static ID/argument/coverage tooling and reproducible [locale/catalog transitions](../architecture/replay.md#proposed-localization-inputs).
- Expanded and RTL pseudo locales with numeric/semantic invariants, selective CPU-reference screenshots and compact CI/agent reports under [testing](../guides/testing.md#localization-fixtures).

Exit criteria to refine per consumer: language/resource changes preserve identity, focus and user edits while publishing coherent messages, semantic labels, geometry and paint; invalid catalogs keep the accepted state with located diagnostics. Fallback, formatting and missing-glyph reports remain distinguishable. Declared default, expanded and RTL fixtures reveal clipping, wrap and direction regressions; repeated replay/capture agrees with fixed catalogs, data, fonts and formatting inputs.

Owners: [localization](../architecture/localization.md), [UI model](../architecture/ui-model.md), [reactive runtime](../architecture/reactive-runtime.md), [layout](../architecture/layout.md), [text](../architecture/text.md), [inspection](../architecture/inspection.md), [replay](../architecture/replay.md), [dependencies](../reference/dependencies.md).

## Later — Typography expansion

Prerequisites: v0.2.0 real text (faces, shaping, bitmap glyph cache, fallback, wrapping, deterministic fixtures) and the v0.4.0 editing/selection work.

Candidates, in dependency order:

- Caret and selection geometry for text inputs; authored font-alias syntax and bundled alias mappings.
- Read-only rich text spans, MSDF glyphs with size/scale-dependent [raster mode selection](../architecture/text.md#proposed-glyph-raster-strategy) sampled by GPU and CPU backends, and variable font instances.
- [International text layout](../architecture/text.md#proposed-international-text-layout): Unicode line breaking, script segmentation and bidirectional layout coordinated with [localization](#later--localization-and-international-layout); color emoji and advanced OpenType features.
- Font asset metadata, license metadata hooks, and packaging warnings under [font licensing](../reference/dependencies.md#font-licensing).

Exit criteria to refine per item: measurement/paint agreement holds across fallback and variation changes, zoomed text meets declared image tolerances, and each script or emoji format claim has dedicated fixtures.

Owners: [text](../architecture/text.md), [rendering](../architecture/rendering.md), [dependencies](../reference/dependencies.md).

## Later — Reactive scheduling and retained render performance

Prerequisites: the [active application/editor verification loop](current.md), measured tool workloads, and full-tree parity for each proposed incremental stage. Aggressive evaluation/rendering optimization follows the integration proof rather than blocking it.

Candidates, in dependency order:

- [Retained display items and partial uploads](../proposals/incremental-evaluation.md#proposed-retained-render-updates), with completion-safe buffer/atlas reuse and full paint/image agreement.
- Bindless rendering, retained GPU scene caches, GPU-driven UI, and deeper batching only when profiling a real Tessera application justifies the complexity.
- Host-driven animation integration, then priority/frame queues or visibility-aware evaluation only when needed without losing input, semantic, or lifetime work.
- Selective parallel text/layout/resource preparation with owned inputs and ordered publication; general concurrent reactive mutation and compiler dependency hints require separate justification.
- Optional immediate authoring facades or observable collection helpers over the common IR/kernel only when a consumer needs them.

Exit criteria to refine per consumer: deterministic observations and full-tree/render-reference output agree; changed-region work is measured separately from total UI size; queues, cancellation, and retirement stay bounded under replacement and owner churn. Optimization results cannot imply platform support or universal timing guarantees.

Owners: [reactive runtime](../architecture/reactive-runtime.md), [UI model](../architecture/ui-model.md), [styling](../architecture/styling.md), [layout](../architecture/layout.md), [text](../architecture/text.md), [rendering](../architecture/rendering.md), [inspection](../architecture/inspection.md).

## Later — Analytic primitives and CPU compatibility performance

Prerequisites: the [CPU reference contract](../architecture/rendering.md#implemented-scalar-cpu-reference-backend), and measured need plus dirty tracking for damage-based repaint.

Candidates:

- Analytic coverage antialiasing applied to every backend together, then circles/ellipses/capsules, focus rings, and simple shadows under shared [primitive semantics](../architecture/rendering.md#proposed-primitive-semantics).
- CPU tile binning, damaged-tile repaint, worker threads with deterministic and performance modes, and SIMD paths beside the scalar reference.
- Host backend selection with an automatic GPU-to-CPU compatibility fallback and a host that presents the CPU framebuffer.

Exit criteria to refine per item: GPU and CPU agree on each primitive within declared tolerances, optimized and multithreaded CPU paths match the scalar reference, damage-based repaint matches full repaint, and compatibility-mode responsiveness is measured on a representative tool workload rather than promised.

Owners: [rendering](../architecture/rendering.md), [text](../architecture/text.md), [inspection](../architecture/inspection.md).

## Later — Graph canvas and node editor

Objective: a domain-agnostic [graph canvas](../integrations/graph-editor.md) over ordinary components and custom paint.

Prerequisites: Canvas/custom paint and transformed paint/hit-test agreement, overlays/portals, keyed reconciliation, themes, commands and transactions, typed drag and drop, generalized virtualization, and inspection target resolution.

Stages:

- Canvas base: grid, viewport transform, pan/zoom, coordinate conversion.
- Nodes and connections: container nodes, ports, drag, selection, edges, temporary edges, domain compatibility, curve hit testing.
- Editor UX: search palette, context menus, clipboard, transactions for application-owned undo, minimap.
- Scale: spatial index, culling, edge batching, LOD, text culling and caching.
- Advanced: subgraphs, layout providers, execution overlays, profiling, graph diff, node registry.
- Agent operation: ID-based inspection and operations, deterministic graph snapshots.

Planning scale (not support claims): about 500 nodes/1,000 edges for an MVP, 5,000+ nodes/10,000+ edges for a production editor, and 10,000-100,000 nodes with LOD or partial browsing.

Owners: [graph editor](../integrations/graph-editor.md), [rendering](../architecture/rendering.md), [layout](../architecture/layout.md), [input](../architecture/input.md), [inspection](../architecture/inspection.md).

## Later — Conversational and agent workspace

Objective: a reference [conversational workspace](../integrations/conversational-ui.md) that exercises text, virtualization, streaming, async work, and automation together.

Prerequisites: production VirtualList with key-plus-offset anchors, editable text with IME/clipboard, real text with fallback, async state primitives, commands, and inspection fixtures.

Stages:

- Conversation foundation: message model, read-only rich text, basic Markdown, code blocks, composer, streaming append with update coalescing, host task/cancellation bridge.
- Structured content: attachments and images, tool call/result views, syntax highlighting, tables, diffs, message actions, persistence, basic branching.
- Agent workspace: agent timeline, approvals, progress, terminal, file browser, artifact panel, docking.
- Interactive workspace: interactive message content, custom block registry, graph/3D/USD artifacts, remote and collaborative sessions.

Exit criteria to refine from the reference application: its long-conversation, streaming Markdown, large code, tool-heavy, and mixed workspace fixtures run deterministically under inspection with recorded baselines.

Owners: [conversational UI](../integrations/conversational-ui.md), [layout](../architecture/layout.md), [text](../architecture/text.md), [input](../architecture/input.md), [semantics](../architecture/semantics.md), [inspection](../architecture/inspection.md).

## Unscheduled extensions

- Timeline and plotting primitives beyond the v0.4.0 profiling primitive.
- Collaboration and multi-user presence, after commands, transactions, and generation history.
- Host-clock property animation after baseline style/layout correctness; see [styling](../proposals/animation.md#proposed-animation).
- Richer layout algorithms in the order owned by [layout](../architecture/layout.md#extension-order).
- Finer dirty-subtree/reactive updates after measured need and full-tree parity.
- TypeScript-inspired DSL, then optional JSX/TSX compilation after the IR stabilizes; no JavaScript VM requirement.
- Broader editor bridge transport/process choices driven by actual consumers.
- Python/application bindings after component/runtime lifecycle stabilization and a concrete scripting or tooling consumer.
- Responsive viewport/container rules and general constraint/flow layout after the existing [layout extension order](../architecture/layout.md#extension-order), when a tool consumer requires them.
- Gradients, paths (through a path rasterizer), richer composition, and retained/partial GPU updates after bounded paint/resource contracts and measured workloads; blur/complex filters stay outside early/mid scope. Simple shadows are in the analytic primitives candidate above.

## Explicitly deferred

Full DOM/CSS compatibility, JavaScript VM, embedded browser, complete SVG, complex filters/effects, rich-text editor suite (read-only rich text and Markdown rendering are separate candidates above), huge widget catalog, Qt or Flutter API compatibility, scene-graph coupling, an OpenUSD-backed internal UI tree, application data ownership, and an unrestricted remote mutation API are outside early/mid scope. Premature multithreaded API or stable ABI guarantees are also excluded.

Optional USD/engine integration reaches the runtime through view models and adapters under [architecture](../architecture/overview.md).
