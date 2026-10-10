# Backlog

Inactive milestone candidates live here; [current](current.md) owns the active scope. Versions are candidates without committed dates. Implementation and validation status is owned by the [support matrix](../reference/support-matrix.md).

Inspection foundations enter early so DevTools, accessibility, testing, and agents share the same identity/semantic/action boundaries. Build in-process observation and controlled host fixtures before CLI, snapshot comparison, transport, or optional MCP integration. Web shares this foundation; its broader host implementation follows runtime/text/lifecycle parity prerequisites below. External strategy phase numbers do not replace these candidate scopes.

Priority across candidates: the contracts that later work depends on (commands, transactions, async state, UI invariants, component identity and reconciliation), then error boundaries, the command palette, the agent capability manifest, generalized virtualization, typed drag and drop, data grids, and context, and only then generation history, timeline/plotting primitives, collaboration, Web/WASM, and world-space UI. Items within a candidate are listed in dependency order.

## v0.3.0 candidate — Dynamic UI and editor prototype

Objective: support application-driven composition and Path-finder iteration using the same document/schema, and make dynamic UI safe to observe and verify, not only able to change.

Depends on menu/text/style foundations and explicit document identity/versioning.

Work:

- Props/local state/bindings, conditional children, keyed reconciliation, cleanup and resource retirement.
- [Command registry](../design/commands.md) implementation, [transaction](../design/commands.md#proposed-transaction-boundary) integration, and a [command palette](../design/commands.md#proposed-command-palette) prototype built from ordinary components.
- [Error boundaries](../design/ui-model.md#proposed-error-boundaries) and [async state](../design/ui-model.md#proposed-async-state) primitives.
- Image assets and theme variables.
- Validate transform agreement between native pointer mapping and rendering when a consumer uses transformed menu paint or hit testing, under [layout coordinates](../design/layout.md#coordinate-spaces) and [rendering conversion](../design/rendering.md#coordinate-conversion).
- Source-aware diagnostics, atomic live reload, compatible-state preservation, Path-finder preview.
- Full property reflection/introspection v1 and a schema-driven Inspector.
- OverlayRoot/Portal and bounded Tooltip/Dropdown/Modal primitives.
- TextInput contract and VirtualList prototype based on stable item keys.
- Scalar [CPU reference backend](../design/rendering.md#proposed-cpu-reference-backend) for the existing draw-list vocabulary and grayscale glyphs, with deterministic window- and GPU-free capture; Vulkan fixtures compared with it under declared tolerances.
- A small inspection/render CLI consumer and versioned snapshot bundle prototype; semantic/layout/counter comparisons and selective visual regression artifacts across declared viewport fixtures, captured with the CPU reference backend by default.
- Reload-to-snapshot linkage and source-edit-to-verification instrumentation shared with Path-finder.
- [UI invariant](../design/inspection.md#proposed-ui-invariants) prototype and [agent capability manifest](../design/inspection.md#proposed-agent-capability-manifest) v0.
- Source consumption by a downstream CMake project through `add_subdirectory`/`FetchContent` with the `tessera::core` and optional `tessera::vulkan` targets, checked by a minimal consumer, as Path-finder's integration path. Installed/exported packages remain v0.5.0 scope.

Exit criteria:

- An inventory/tool panel updates without corrupting identity, focus, bindings, or cleanup.
- Missing/replaced assets and theme variables follow declared fallback/diagnostic rules.
- Invalid edits keep the last valid UI; compatible edits preserve declared state and retire old resources.
- Runtime and Path-finder round-trip the same document, including editor metadata; a minimal downstream project builds and links Tessera from source.
- Inspector properties derive from runtime metadata; portal focus/ownership and list item identity remain coherent.
- Tooling uses host-declared state slots, captures one generation, and emits machine-readable observations/differences; invalid bundle versions or incompatible comparison conditions are diagnosed.
- The CPU reference backend renders the primitive/glyph fixtures identically across runs without a window or GPU, and both Vulkan paths agree with it within declared tolerances.
- Menu, shortcut, palette, and semantic invocations of one command share eligibility and produce one transaction per gesture; a failing subtree or reload keeps the previous valid generation; invariants report located results for one captured generation.

Owners: [UI model](../design/ui-model.md), [commands](../design/commands.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [input](../design/input.md), [layout](../design/layout.md), [Path-finder](../design/path-finder-integration.md), [inspection](../design/inspection.md).

## v0.4.0 candidate — Productive tool UI

Objective: add primitives required by real editor/utility consumers without growing a general widget catalog. Mimikuri, Path-finder, and utility applications become full consumers in this generation.

Depends on component identity, text/editing boundaries, and the Path-finder prototype.

Candidates:

- [Generalized virtualization](../design/layout.md#proposed-virtualization-foundation) with production VirtualList and tree/hierarchy primitives, then DataGrid/TreeTable composed from primitives: resizable, sortable, pinned, and hideable columns; single and multi-selection; editable cells; keyboard navigation; row hierarchy; context menus.
- Resizable split panels and a minimal docking subset.
- Editable TextInput, IME implementation, clipboard, selection, and [typed drag and drop](../design/input.md#proposed-typed-drag-and-drop).
- [Context](../design/ui-model.md#proposed-context) for subtree services; transaction/undo integration hardening.
- Canvas/custom paint, [performance instrumentation](../design/inspection.md#proposed-performance-metrics), and a timeline/profiling primitive.
- UI invariants in CI.
- Dirty style/layout/paint tracking after full-tree correctness.
- Representative dense/high-frequency tool benchmarks, measured iteration latency, and consumer-specific regression budgets after recording a baseline; thin scenario tooling and compact CI reports/artifacts.

Exit criteria to refine from a consumer prototype: long lists, trees, and grids preserve keyed selection/focus and scroll anchors, Japanese composition and clipboard work under declared platform evidence, custom paint respects the common draw/resource contract, and incremental results match full-tree output. Benchmark/iteration reports identify representative fixtures and measurement conditions; consumer budgets and CI reports distinguish timing noise from deterministic counter regressions.

Owners: [UI model](../design/ui-model.md), [layout](../design/layout.md), [input](../design/input.md), [commands](../design/commands.md), [text](../design/text.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [inspection](../design/inspection.md).

## v0.5.0 candidate — Accessibility, automation, and robust runtime

Objective: make UI reliably inspectable and operable through accessibility and external automation adapters.

Depends on stable semantics/actions, replay, and lifecycle contracts.

Candidates:

- Native accessibility adapters and external semantic invocation using the earlier shared inspection foundation.
- Replay recording tools, focus recovery/modal navigation hardening.
- Resource/device-loss diagnostics, install/export packaging, downstream integration checks.
- [Agent capability manifest](../design/inspection.md#proposed-agent-capability-manifest) v1, command discovery, and semantic automation through the shared command path.
- [Generation history](../design/inspection.md#proposed-generation-history) for time-travel inspection, failure reproduction bundles, and performance regression bundles.
- Persistent automation adapters built on the earlier inspection/action API: bounded stdin/stdout JSON-RPC first, local RPC/subscriptions only for a consumer, optional MCP last; explicit development enablement and production exclusion.

Exit criteria to refine per adapter: external automation handles inspect, invoke, replay, capture, diff, and assert consistently; semantic invocation obeys ordinary action and command eligibility, stale identities fail safely, adapter behavior has platform-specific evidence, replay reproduces failures, and downstream consumers exercise documented lifetime boundaries. Transport validation must cover request limits, cancellation/teardown, permitted state slots, and exclusion of development mutation/source exports from production.

Owners: [semantics](../design/semantics.md), [input](../design/input.md), [commands](../design/commands.md), [replay](../design/replay.md), [rendering](../design/rendering.md), [dependencies](../reference/dependencies.md), [inspection](../design/inspection.md).

## Later — WebGPU, WASM, and world-space UI

Prerequisites: stable document, draw/resource and text boundaries, and replay fixtures suitable for parity checks.

Evaluate native WebGPU, browser WebGPU hosting, and WASM separately. The intended Web path is the shared runtime in WASM, WebGPU canvas visuals, and semantic DOM/browser editing adapters under [Web host](../design/web-host.md).

Candidates:

- WASM packaging and browser host with explicit device/canvas/input/asset ownership and a thin typed JS state/action bridge.
- Semantic DOM accessibility and focus synchronization from the common SemanticTree.
- Hybrid browser text editing, Japanese IME, selection and clipboard; evaluate mobile keyboard/autofill/password-manager needs separately.
- Worker/OffscreenCanvas only after single-thread correctness and measured need, with ordered generation-aware messages and teardown.

Exit criteria to refine per host: equivalent document/state/geometry/actions and selective rendered fixture parity under declared tolerances; semantic DOM focus/removal and Japanese editing work in recorded browser configurations; bridge lifetime and malformed inputs are tested. A backend name or shader target does not establish browser support.

World-space adapters and streamed resources require bounded host requirements. Metal/D3D12 need a concrete consumer before scheduling.

Owners: [architecture](../design/architecture.md), [Web host](../design/web-host.md), [rendering](../design/rendering.md), [semantics](../design/semantics.md), [input](../design/input.md), [dependencies](../reference/dependencies.md), [replay](../design/replay.md).

## Later — Typography expansion

Prerequisites: v0.2.0 real text (faces, shaping, bitmap glyph cache, fallback, wrapping, deterministic fixtures) and the v0.4.0 editing/selection work.

Candidates, in dependency order:

- Caret and selection geometry for text inputs; authored font-alias syntax and bundled alias mappings.
- Read-only rich text spans, MSDF glyphs with size/scale-dependent [raster mode selection](../design/text.md#proposed-glyph-raster-strategy) sampled by GPU and CPU backends, and variable font instances.
- Unicode line breaking, script segmentation, advanced bidirectional layout, color emoji, and advanced OpenType features.
- Font asset metadata, license metadata hooks, and packaging warnings under [font licensing](../reference/dependencies.md#font-licensing).

Exit criteria to refine per item: measurement/paint agreement holds across fallback and variation changes, zoomed text meets declared image tolerances, and each script or emoji format claim has dedicated fixtures.

Owners: [text](../design/text.md), [rendering](../design/rendering.md), [dependencies](../reference/dependencies.md).

## Later — Analytic primitives and CPU compatibility performance

Prerequisites: the v0.3.0 CPU reference backend, and v0.4.0 dirty tracking for damage-based repaint.

Candidates:

- Analytic coverage antialiasing applied to every backend together, then circles/ellipses/capsules, focus rings, and simple shadows under shared [primitive semantics](../design/rendering.md#proposed-primitive-semantics).
- CPU tile binning, damaged-tile repaint, worker threads with deterministic and performance modes, and SIMD paths beside the scalar reference.
- Host backend selection with an automatic GPU-to-CPU compatibility fallback and a host that presents the CPU framebuffer.

Exit criteria to refine per item: GPU and CPU agree on each primitive within declared tolerances, optimized and multithreaded CPU paths match the scalar reference, damage-based repaint matches full repaint, and compatibility-mode responsiveness is measured on a representative tool workload rather than promised.

Owners: [rendering](../design/rendering.md), [text](../design/text.md), [inspection](../design/inspection.md).

## Later — Graph canvas and node editor

Objective: a domain-agnostic [graph canvas](../design/graph-editor.md) over ordinary components and custom paint.

Prerequisites: Canvas/custom paint and transformed paint/hit-test agreement, overlays/portals, keyed reconciliation, themes, commands and transactions, typed drag and drop, generalized virtualization, and inspection target resolution.

Stages:

- Canvas base: grid, viewport transform, pan/zoom, coordinate conversion.
- Nodes and connections: container nodes, ports, drag, selection, edges, temporary edges, domain compatibility, curve hit testing.
- Editor UX: search palette, context menus, clipboard, transactions for application-owned undo, minimap.
- Scale: spatial index, culling, edge batching, LOD, text culling and caching.
- Advanced: subgraphs, layout providers, execution overlays, profiling, graph diff, node registry.
- Agent operation: ID-based inspection and operations, deterministic graph snapshots.

Planning scale (not support claims): about 500 nodes/1,000 edges for an MVP, 5,000+ nodes/10,000+ edges for a production editor, and 10,000-100,000 nodes with LOD or partial browsing.

Owners: [graph editor](../design/graph-editor.md), [rendering](../design/rendering.md), [layout](../design/layout.md), [input](../design/input.md), [inspection](../design/inspection.md).

## Later — Conversational and agent workspace

Objective: a reference [conversational workspace](../design/conversational-ui.md) that exercises text, virtualization, streaming, async work, and automation together.

Prerequisites: production VirtualList with key-plus-offset anchors, editable text with IME/clipboard, real text with fallback, async state primitives, commands, and inspection fixtures.

Stages:

- Conversation foundation: message model, read-only rich text, basic Markdown, code blocks, composer, streaming append with update coalescing, host task/cancellation bridge.
- Structured content: attachments and images, tool call/result views, syntax highlighting, tables, diffs, message actions, persistence, basic branching.
- Agent workspace: agent timeline, approvals, progress, terminal, file browser, artifact panel, docking.
- Interactive workspace: interactive message content, custom block registry, graph/3D/USD artifacts, remote and collaborative sessions.

Exit criteria to refine from the reference application: its long-conversation, streaming Markdown, large code, tool-heavy, and mixed workspace fixtures run deterministically under inspection with recorded baselines.

Owners: [conversational UI](../design/conversational-ui.md), [layout](../design/layout.md), [text](../design/text.md), [input](../design/input.md), [semantics](../design/semantics.md), [inspection](../design/inspection.md).

## Unscheduled extensions

- Timeline and plotting primitives beyond the v0.4.0 profiling primitive.
- Collaboration and multi-user presence, after commands, transactions, and generation history.
- Host-clock property animation after baseline style/layout correctness; see [styling](../design/styling.md#proposed-animation).
- Richer layout algorithms in the order owned by [layout](../design/layout.md#extension-order).
- Finer dirty-subtree/reactive updates after measured need and full-tree parity.
- TypeScript-inspired DSL, then optional JSX/TSX compilation after the IR stabilizes; no JavaScript VM requirement.
- Broader editor bridge transport/process choices driven by actual consumers.
- Responsive viewport/container rules and general constraint/flow layout after the existing [layout extension order](../design/layout.md#extension-order), when a tool consumer requires them.
- Gradients, paths (through a path rasterizer), richer composition, and retained/partial GPU updates after bounded paint/resource contracts and measured workloads; blur/complex filters stay outside early/mid scope. Simple shadows are in the analytic primitives candidate above.

## Explicitly deferred

Full DOM/CSS compatibility, JavaScript VM, embedded browser, complete SVG, complex filters/effects, rich-text editor suite (read-only rich text and Markdown rendering are separate candidates above), huge widget catalog, Qt or Flutter API compatibility, scene-graph coupling, an OpenUSD-backed internal UI tree, application data ownership, and an unrestricted remote mutation API are outside early/mid scope. Premature multithreaded API or stable ABI guarantees are also excluded.

Optional USD/engine integration reaches the runtime through view models and adapters under [architecture](../design/architecture.md).
