# Backlog

Inactive milestone candidates live here; [current](current.md) owns the active scope. Versions are candidates without committed dates. Implementation and validation status is owned by the [support matrix](../reference/support-matrix.md).

Inspection foundations enter early so DevTools, accessibility, testing, and agents share the same identity/semantic/action boundaries. Build in-process observation and controlled host fixtures before CLI, snapshot comparison, transport, or optional MCP integration. Web shares this foundation; its broader host implementation follows runtime/text/lifecycle parity prerequisites below. External strategy phase numbers do not replace these candidate scopes.

## v0.2.0 — Usable navigable menus

Active; its complete scope and exit criteria are in [current](current.md).

## v0.3.0 candidate — Dynamic UI and editor prototype

Objective: support application-driven composition and Path-finder iteration using the same document/schema.

Depends on menu/text/style foundations and explicit document identity/versioning.

Work:

- Props/local state/bindings, conditional children, keyed reconciliation, cleanup and resource retirement.
- Image assets and theme variables.
- Source-aware diagnostics, atomic live reload, compatible-state preservation, Path-finder preview.
- Full property reflection/introspection v1 and a schema-driven Inspector.
- OverlayRoot/Portal and bounded Tooltip/Dropdown/Modal primitives.
- TextInput contract and VirtualList prototype based on stable item keys.
- A small inspection/render CLI consumer and versioned snapshot bundle prototype; semantic/layout/counter comparisons and selective visual regression artifacts across declared viewport fixtures.
- Reload-to-snapshot linkage and source-edit-to-verification instrumentation shared with Path-finder.

Exit criteria:

- An inventory/tool panel updates without corrupting identity, focus, bindings, or cleanup.
- Missing/replaced assets and theme variables follow declared fallback/diagnostic rules.
- Invalid edits keep the last valid UI; compatible edits preserve declared state and retire old resources.
- Runtime and Path-finder round-trip the same document, including editor metadata.
- Inspector properties derive from runtime metadata; portal focus/ownership and list item identity remain coherent.
- Tooling uses host-declared state slots, captures one generation, and emits machine-readable observations/differences; invalid bundle versions or incompatible comparison conditions are diagnosed.

Owners: [UI model](../design/ui-model.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [input](../design/input.md), [layout](../design/layout.md), [Path-finder](../design/path-finder-integration.md), [inspection](../design/inspection.md).

## v0.4.0 candidate — Productive tool UI

Objective: add primitives required by real editor/utility consumers without growing a general widget catalog.

Depends on component identity, text/editing boundaries, and the Path-finder prototype.

Candidates:

- Production VirtualList, tree/hierarchy primitives, resizable split panels, a minimal docking subset.
- Editable TextInput, IME implementation, clipboard, selection, and drag/drop.
- Canvas/custom paint and performance instrumentation.
- Dirty style/layout/paint tracking after full-tree correctness.
- Representative dense/high-frequency tool benchmarks, measured iteration latency, and consumer-specific regression budgets after recording a baseline; thin scenario tooling and compact CI reports/artifacts.

Exit criteria to refine from a consumer prototype: long lists preserve keyed selection/focus, Japanese composition and clipboard work under declared platform evidence, custom paint respects the common draw/resource contract, and incremental results match full-tree output. Benchmark/iteration reports identify representative fixtures and measurement conditions; consumer budgets and CI reports distinguish timing noise from deterministic counter regressions.

Owners: [UI model](../design/ui-model.md), [layout](../design/layout.md), [input](../design/input.md), [text](../design/text.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [inspection](../design/inspection.md).

## v0.5.0 candidate — Accessibility, automation, and robust runtime

Objective: make UI reliably inspectable and operable through accessibility and external automation adapters.

Depends on stable semantics/actions, replay, and lifecycle contracts.

Candidates:

- Native accessibility adapters and external semantic invocation using the earlier shared inspection foundation.
- Replay recording tools, focus recovery/modal navigation hardening.
- Resource/device-loss diagnostics, install/export packaging, downstream integration checks.
- Persistent automation adapters built on the earlier inspection/action API: bounded stdin/stdout JSON-RPC first, local RPC/subscriptions only for a consumer, optional MCP last; explicit development enablement and production exclusion.

Exit criteria to refine per adapter: semantic invocation obeys ordinary action eligibility, stale identities fail safely, adapter behavior has platform-specific evidence, replay reproduces failures, and downstream consumers exercise documented lifetime boundaries. Transport validation must cover request limits, cancellation/teardown, permitted state slots, and exclusion of development mutation/source exports from production.

Owners: [semantics](../design/semantics.md), [input](../design/input.md), [replay](../design/replay.md), [rendering](../design/rendering.md), [dependencies](../reference/dependencies.md), [inspection](../design/inspection.md).

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

## Unscheduled extensions

- Host-clock property animation after baseline style/layout correctness; see [styling](../design/styling.md#proposed-animation).
- Richer layout algorithms in the order owned by [layout](../design/layout.md#extension-order).
- Finer dirty-subtree/reactive updates after measured need and full-tree parity.
- TypeScript-inspired DSL, then optional JSX/TSX compilation after the IR stabilizes; no JavaScript VM requirement.
- Broader editor bridge transport/process choices driven by actual consumers.
- Responsive viewport/container rules and general constraint/flow layout after the existing [layout extension order](../design/layout.md#extension-order), when a tool consumer requires them.
- Gradients, shadows, paths, richer composition, and retained/partial GPU updates after bounded paint/resource contracts and measured workloads; blur/complex filters stay outside early/mid scope.

## Explicitly deferred

Full DOM/CSS compatibility, JavaScript VM, embedded browser, complete SVG, complex filters/effects, rich-text editor suite, huge widget catalog, scene-graph coupling, and an OpenUSD-backed internal UI tree are outside early/mid scope. Premature multithreaded API or stable ABI guarantees are also excluded.

Optional USD/engine integration reaches the runtime through view models and adapters under [architecture](../design/architecture.md).
