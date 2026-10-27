# Backlog

Inactive milestone candidates live here; [current](current.md) owns the active scope. Versions are candidates without committed dates. Implementation and validation status is owned by the [support matrix](../reference/support-matrix.md).

Everything here is planning context and does not authorize implementation during the active milestone. Promote only a demonstrated fixture requirement into current, with a minimum fix, observable acceptance and a stop condition under the [agent policy](../agent-policy.md). A detailed design or a candidate's dependencies do not activate adjacent work.

Inspection foundations enter early so DevTools, accessibility, testing, and agents share the same identity/semantic/action boundaries. Build in-process observation and controlled host fixtures before CLI, snapshot comparison, transport, or optional MCP integration. Web shares this foundation; its broader host implementation follows runtime/text/lifecycle parity prerequisites below. External strategy phase numbers do not replace these candidate scopes.

Choose follow-up scope from real applications and their measured authoring friction, beginning with the [Settings reference application](../integrations/settings-app.md). The candidate sequence moves from a practical UI kit through tooling, language boundaries and larger applications to measured scale. Each candidate below defines its own proof; its inventory is not an all-features release gate.

## Validation and CI follow-ups

The [bounded validation task](current.md#bounded-validation-supporting-work) owns hosted rollout comparison. The [testing guide](../guides/testing.md#validation-tiers) owns tier responsibilities, selection, provisional SLO and reduction safeguards. These follow-ups are inactive and must not become a prerequisite for ordinary UI authoring.

- After hosted comparison, use runner capacity/cost and main-run frequency to choose whether daily scheduling adds useful coverage. Compare against the tag/manual release workflow before extending execution or changing required status names.
- After parallel-run evidence is stable, coordinate any required-check names and rulesets while retaining wide matrices/import coverage and release verification/publication. Required checks must finish on documentation-only PRs. Extend path selection only for measured need, with matcher tests and conservative full-core fallback.
- After equivalent owner coverage is identified, shrink duplicate consumer assertions incrementally, documenting the owning regression and diagnostic value. Observe Tier 2 failures for two weeks and restore important missed checks to the PR gate.
- Use measured configure/build/test bottlenecks to choose safe parallelism, caching or build-target separation. Do not create a general CI framework or delete protection to meet an unmeasured time target.

Promote only the next bounded step with measurement-backed acceptance and a stop condition. Before a release, aggregate full Debug/Release, both source imports, CPU/replay and the required available platform evidence under the active milestone's exit criteria.

## Widget candidates beyond the reference application

v0.4.0 delivered the controls required by Settings and the bounded [renderer diagnostics panel](../integrations/renderer-diagnostics.md), as recorded in the [release acceptance](../reference/support-matrix.md#v040-release-acceptance--2026-10-27). Additional baseline candidates are a Label convenience, RadioButton/RadioGroup, Slider, Spacer/Separator, Stack/Overlay, ScrollView and a basic List, plus Toolbar, Tabs, SplitPane, Menu/MenuItem and InspectorRow. Text, Button, Checkbox, TextInput, Row and Column and the diagnostics consumer's minimum numeric controls are owned by their [UI model](../architecture/ui-model.md#implemented-c-form-authoring) and [consumer](../integrations/renderer-diagnostics.md) contracts rather than duplicated here; broader widget APIs stay candidates.

Promote additional widgets from an application operation; do not implement this inventory for completeness. Prefer ordinary components and the [shared authoring/lowering model](../proposals/cpp-authoring.md); a full generalized API per widget is not required.

## After v0.4.0

v0.4.x patch candidates take regression fixes only. v0.5.0 then prioritizes practical applications, widget growth and native integration under the candidate below. Improvements recorded during v0.4.0 convergence stay here until a consumer operation promotes them.

## v0.5.0 candidate — Application UI Kit

Objective: **Tessera で小〜中規模のデスクトップツールを作れる。** Build an editor-like desktop tool without custom framework infrastructure.

Depends on the high-level C++ authoring surface, reusable components and the reference application's input/style contracts.

Candidates:

- Grow the [widget inventory](#widget-candidates-beyond-the-reference-application) from the desktop tool's operations; mature TextInput, clipboard, undo/redo, multiline and IME under declared platform boundaries.
- Menus, dropdowns, context menus, dialogs, richer Inspector controls and application themes.
- List/tree selection, TreeView, better scrolling, keyboard/gamepad navigation and focus scopes where an interaction demonstrates the need.
- Introduce the minimum overlay mechanism for a real Dropdown or ContextMenu, then extract a shared abstraction only from required behavior; see [overlays](../proposals/overlays-and-portals.md).
- A first practical DevTools/inspection UI using ordinary semantics, commands and observations.

Exit criteria to refine per consumer: the tool's editing, selection, scrolling, menu/dialog and navigation operations compose through public APIs without private framework plumbing; focus and command eligibility survive popup dismissal and keyed changes; replay/CPU-reference observations reproduce. Platform text claims require dedicated evidence.

Owners: [authoring](../proposals/cpp-authoring.md), [UI model](../architecture/ui-model.md), [input](../architecture/input.md), [styling](../architecture/styling.md), [layout](../architecture/layout.md), [inspection](../architecture/inspection.md).

## v0.6.0 candidate — Authoring and Tooling

Objective: **Tessera UI を作る・調べる・修正する体験を成立させる。** Make `edit -> reload -> inspect -> interact -> capture -> verify` a normal developer workflow.

Depends on an application-shaped consumer, stable source/semantic identity and ordinary inspection/replay boundaries.

Candidates:

- [Path-finder integration](../integrations/path-finder.md), source-aware editing, hierarchy/property editing, style inspection, source navigation, live reload and actionable validation diagnostics.
- UI capture/comparison, reusable snapshot/replay workflows, a component browser/widget gallery and authoring metadata only where tooling requires it.
- Select bounded mechanisms from [editor extensions](#unscheduled-editor-extensions), preserving source ownership, atomic acceptance and the shared runtime. The earlier editor shortcut follow-up is also owned there.

Exit criteria to refine per workflow: a developer edits source, reloads, inspects its hierarchy/style/property, locates the source target, interacts, captures and verifies through one generation model. Invalid edits preserve the accepted state with located diagnostics; representative replay and capture comparisons reproduce without a separate editor execution model.

Owners: [Path-finder](../integrations/path-finder.md), [inspection](../architecture/inspection.md), [reload](../proposals/live-reload.md), [replay](../architecture/replay.md).

## v0.7.0 candidate — Language Bindings and Stable Host Boundary

Objective: **C++ 以外から Tessera を安全に利用できる境界を作る。** Establish a minimal host-facing boundary suitable for non-C++ consumers.

Depends on practical high-level authoring, demonstrated host ownership and a concrete scripting/tooling consumer.

Candidates, in priority order:

- Define opaque handles, ownership/lifetime and error/result rules, plus a C ABI facade where useful; choose compatibility commitments from downstream evidence.
- Prototype Python first, evaluate Rust second and Go third. Bind the high-level authoring/host model with native-feeling language APIs rather than mirroring every internal C++ type.
- Evaluate serialized/document interfaces for foreign languages. Each language frontend must lower into the same runtime; illustrative binding syntax is not a contract.

Exit criteria to refine per binding: an independent non-C++ consumer authors and operates a small application, handles errors and teardown safely, rejects stale/foreign handles, and matches ordinary semantic/replay observations. Do not promise all three bindings or a stable ABI before a validated consumer requires them.

Owners: [UI model](../architecture/ui-model.md), [architecture](../architecture/overview.md), [dependencies](../reference/dependencies.md).

## v0.8.0 candidate — Broader Application Model

Objective: **より大きなアプリケーションを構成できる。** Compose larger applications from consumer-proven UI and host boundaries.

Depends on reference applications that demonstrate needs beyond a small tool.

Possible scope:

- Richer application shells, [command palette](../proposals/command-palette.md), docking/pane management where demanded, larger virtualized collections, TreeTable/DataGrid, resizable/sortable/pinned/hidden columns, editable cells and typed drag and drop.
- Background data/resources, async application patterns, subtree [context](../architecture/ui-model.md#proposed-context), application themes, localization integration and accessibility adapter maturation.
- Custom canvas/graph-editor integration and plugin/extension boundaries only where real consumers exist. Applications retain domain data, history and external execution.

Exit criteria to refine per application: its multi-pane/data/async operations remain composable through public boundaries; selection, focus, scroll anchors and ownership survive changes; resource replacement/cancellation and adapters preserve coherent generations and reproducible observations. Promote only the required part of this scope.

Owners: [UI model](../architecture/ui-model.md), [layout](../architecture/layout.md), [input](../architecture/input.md), [commands](../architecture/commands.md), [localization](../architecture/localization.md), [semantics](../architecture/semantics.md), [graph editor](../integrations/graph-editor.md).

## v0.9.0 candidate — Performance and Scale

Objective: **Correctness-first runtime を保ちながら、大規模 UI へ拡張する。** Optimize measured application workloads while retaining the full-tree/scalar correctness references.

Prerequisites: representative applications, recorded traces, a baseline, an identified bottleneck and explicit correctness parity.

Candidates, selected by measurement:

- [Stage dirtiness and incremental evaluation](../proposals/incremental-evaluation.md): style, measure/placement, text, input, semantic and paint work; paint reuse and list virtualization for representative large collections.
- [Retained display items and partial uploads](../proposals/incremental-evaluation.md#proposed-retained-render-updates), completion-safe GPU buffers/atlases, batching and allocation reductions. Bindless or GPU-driven scene caches require a demonstrated bottleneck.
- Bounded [reactive observations](../architecture/inspection.md#proposed-reactive-observations), owner/dependency/update-cause traces and work-proportionality benchmarks; dense/high-frequency tool baselines and compact regression reports.
- Host-driven animation, priority/frame queues or visibility-aware evaluation only without losing input, semantics or lifetime work. Selective parallel text/layout/resource preparation requires owned inputs and deterministic publication; general concurrent mutation needs separate justification.

For each significant optimization define the workload, baseline, metric, expected improvement, correctness reference and regression check before implementation. Follow correctness -> real application -> measurement -> bottleneck -> minimum optimization -> reference parity.

Exit criteria to refine per optimization: full-tree geometry/semantics/paint and scalar-reference images agree under declared tolerances; changed work is measured separately from total size; queues/cancellation/retirement stay bounded. Reports declare hardware/build/fonts/backend, timing noise and deterministic counter regressions separately. No universal speedup or platform claim follows from one workload.

Owners: [reactive runtime](../architecture/reactive-runtime.md), [incremental evaluation](../proposals/incremental-evaluation.md), [rendering](../architecture/rendering.md), [inspection](../architecture/inspection.md), [testing](../guides/testing.md).

## Unscheduled editor extensions

These broader mechanisms are inactive. Use the [bounded editor fixture](../integrations/path-finder.md#implemented-in-process-panel-fixture) to identify a concrete consumer operation before promoting its minimum scope into [current work](current.md). Reconsider these candidates from concrete fixture friction instead of treating them as a prerequisite sequence:

- **Lifecycle automation and UI fallback:** generalized [declarations](../proposals/automatic-component-lifecycle.md#proposed-automatic-component-lifecycle), declaration/default origins, setup/cleanup conveniences and [contained UI fallback](../proposals/error-boundaries.md#proposed-error-boundaries) beyond the fixture's owner/state/disposal requirements.
- **Resource observations:** [owned resource payloads](../proposals/async-resources.md#proposed-async-state), real thumbnail/image loading, provider integration and completion-safe retirement beyond a host-driven fixture observation; cancellation and logical ownership must preserve the [frame lifetime](../architecture/rendering.md#implemented-frame-contract).
- **Broader reload and reflection:** [state-schema migrations and compatibility](../proposals/live-reload.md#live-reload-transaction) across scroll anchors, accepted resources and command owners; richer [declaration/default origins and typed diagnostic context](../architecture/inspection.md#proposed-inspection-records-and-source-mapping); [reflection observations](../proposals/property-reflection.md#proposed-property-reflection) for inherited/resolved/read-only values and new property kinds. Preserve affected-stage metadata without making incremental evaluation a prerequisite.
- **Overlays and palette:** shared [Portal ownership](../proposals/overlays-and-portals.md#proposed-overlays-and-portals), [placement](../architecture/layout.md#scrolling-and-overlay-geometry), [focus/targeting](../architecture/input.md#proposed-overlay-interaction), [semantic relationships](../architecture/semantics.md#proposed-semantic-projection), composed Tooltip/Dropdown/Modal and [command palette](../proposals/command-palette.md#proposed-command-palette). Include native capture/cancellation and platform shortcut conventions only for an explicit consumer, with transform hit/render agreement and ordinary command eligibility.
- **Large editor lists:** bounded [VirtualList realization](../proposals/virtualization.md#proposed-virtualization-foundation) over thousands of keyed logical rows, deterministic measurement, focus/selection/scroll anchors, async rows and an explicit semantic virtualization policy. A small keyed hierarchy does not require this mechanism.
- **Editor history shortcut follow-up:** defer the earlier regression-first connection of normalized Control+Z/Control+Shift+Z from Inspector focus to ordinary label-history commands. Preserve editing-first consumption, toolbar activation owners/root scope, immutable replacement-tree shortcut maps, shared execution sequences and accepted identities. Acceptance must cover exact source/preview/Inspector restoration, retained selection/state/focus/async owners, toolbar/replay parity, empty/mismatched/open-draft rejection, repeat/consumed input, stale requests and staging-failure retry. The [source review](../reference/support-matrix.md#editor-history-shortcut-review--2026-10-16) owns the observed gap and evidence limits; promotion needs renewed consumer need.
- **Editing expansion:** multiline convenience, host clipboard adapters and platform composition integration beyond the bounded string operation; preserve the [revisioned editing boundary](../architecture/input.md#proposed-editing-ime-and-clipboard-boundary) and [logical replay inputs](../architecture/replay.md#proposed-editor-workflow-inputs). Native shortcut adapters, extra platform/gamepad chords, customization, draft-local undo and generalized input routing need a separate editor operation.
- **Packaged verification and discovery:** [versioned snapshot bundle/CLI](../proposals/snapshot-bundles.md#proposed-runner-and-snapshot-bundle), machine-readable differences, selective visual comparisons and source-edit-to-verification instrumentation; [agent capability manifest](../proposals/agent-capability-manifest.md#proposed-agent-capability-manifest) for ordinary inspect/invoke/replay/capture/invariant/source-edit/reload/comparison operations. An in-process fixture is sufficient before a concrete consumer needs packaging or discovery.
- **Broader performance evidence:** representative application profiling, Vulkan/CPU scene comparisons across declared configurations and authoring-friction measurements to select follow-up scope. Do not turn profiling infrastructure or aggressive evaluation/GPU optimization into editor completion prerequisites.

## Unscheduled accessibility, automation, and runtime adapters

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

Prerequisites: stable common UI semantics, high-level authoring and backend-neutral widgets, then stable document/draw/resource/text boundaries and replay fixtures suitable for parity checks. Browser support is a strategic direction, not an early authoring-release blocker.

Evaluate native WebGPU, browser WebGPU hosting, and WASM separately. The intended sequence is a Web host adapter, WebGPU rendering, then browser text/input integration under [Web host](../proposals/web-host.md). Project the same components, state, semantics, commands, inspection and replay model where technically appropriate; do not create a separate Web runtime.

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

Prerequisites: reactive bindings and coherent publication, consumer-required subtree context, host resource/reload ownership, and text measurement/paint agreement. Preserve localization compatibility from the common model onward; complete tooling is not an early authoring-release gate, and a concrete translation library must not enter core.

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

Prerequisites: faces, shaping, bitmap glyph cache, fallback, wrapping, deterministic fixtures and consumer-required editing/selection boundaries; consult support for implementation evidence.

Candidates, in dependency order:

- Real-font caret/selection geometry beyond the reference application's bounded editing needs; authored font-alias syntax and bundled alias mappings.
- Read-only rich text spans, MSDF glyphs with size/scale-dependent [raster mode selection](../architecture/text.md#proposed-glyph-raster-strategy) sampled by GPU and CPU backends, and variable font instances.
- [International text layout](../architecture/text.md#proposed-international-text-layout): Unicode line breaking, script segmentation and bidirectional layout coordinated with [localization](#later--localization-and-international-layout); color emoji and advanced OpenType features.
- Font asset metadata, license metadata hooks, and packaging warnings under [font licensing](../reference/dependencies.md#font-licensing).

Exit criteria to refine per item: measurement/paint agreement holds across fallback and variation changes, zoomed text meets declared image tolerances, and each script or emoji format claim has dedicated fixtures.

Owners: [text](../architecture/text.md), [rendering](../architecture/rendering.md), [dependencies](../reference/dependencies.md).

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

- Timeline and plotting primitives, selected from a concrete profiling/tool workload.
- Collaboration and multi-user presence, after commands, transactions, and generation history.
- Host-clock property animation after baseline style/layout correctness; see [styling](../proposals/animation.md#proposed-animation).
- Richer layout algorithms in the order owned by [layout](../architecture/layout.md#extension-order).
- Finer dirty-subtree/reactive updates after measured need and full-tree parity.
- TypeScript-inspired DSL, then optional JSX/TSX compilation after the IR stabilizes; no JavaScript VM requirement.
- Broader editor bridge transport/process choices driven by actual consumers.
- Additional language/application bindings beyond the [binding candidate](#v070-candidate--language-bindings-and-stable-host-boundary), driven by a concrete consumer.
- Responsive viewport/container rules and general constraint/flow layout after the existing [layout extension order](../architecture/layout.md#extension-order), when a tool consumer requires them.
- Gradients, paths (through a path rasterizer), richer composition, and retained/partial GPU updates after bounded paint/resource contracts and measured workloads; blur/complex filters stay outside early/mid scope. Simple shadows are in the analytic primitives candidate above.

## Explicitly deferred

Full DOM/CSS compatibility, JavaScript VM, embedded browser, complete SVG, complex filters/effects, rich-text editor suite (read-only rich text and Markdown rendering are separate candidates above), huge widget catalog, Qt or Flutter API compatibility, scene-graph coupling, an OpenUSD-backed internal UI tree, application data ownership, and an unrestricted remote mutation API are outside early/mid scope. Premature multithreaded API or stable ABI guarantees are also excluded.

Optional USD/engine integration reaches the runtime through view models and adapters under [architecture](../architecture/overview.md).
