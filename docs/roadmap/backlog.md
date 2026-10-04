# Backlog

Inactive milestone candidates live here; [current](current.md) owns the active scope. Versions are candidates without committed dates. Implementation and validation status is owned by the [support matrix](../reference/support-matrix.md).

## v0.2.0 candidate — Usable navigable menus

Objective: make game/tool menus usable with real text and mouse, keyboard, or gamepad.

Depends on the v0.1.0 geometry, renderer, coordinate, and action boundaries.

Work:

- Real font abstraction, shaping, glyph cache, fallback, wrapping, and fixed Latin/Japanese fixtures.
- Keyboard focus traversal, directional gamepad navigation, activate/back, and focus recovery.
- ScrollView with matching paint/hit-test clipping.
- Style classes, hover/focus/disabled/active states, deterministic precedence and inheritance boundaries.
- SemanticTree v1 and Replay v1.
- IME/clipboard boundary contracts; DPI and pixel-snapping validation.

Exit criteria:

- The same menu produces equivalent actions through mouse, keyboard, and gamepad.
- Real measurement and painted glyph geometry agree in mixed Latin/Japanese wrapping fixtures.
- Scrolled/clipped nodes paint and hit-test consistently; disabled nodes do not activate.
- Class/state resolution is deterministic and semantic state matches focus/action eligibility.
- Replay reproduces representative interactions, including expected action sequences.

Owners: [text](../design/text.md), [input](../design/input.md), [layout](../design/layout.md), [styling](../design/styling.md), [semantics](../design/semantics.md), [replay](../design/replay.md).

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

Exit criteria:

- An inventory/tool panel updates without corrupting identity, focus, bindings, or cleanup.
- Missing/replaced assets and theme variables follow declared fallback/diagnostic rules.
- Invalid edits keep the last valid UI; compatible edits preserve declared state and retire old resources.
- Runtime and Path-finder round-trip the same document, including editor metadata.
- Inspector properties derive from runtime metadata; portal focus/ownership and list item identity remain coherent.

Owners: [UI model](../design/ui-model.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [input](../design/input.md), [layout](../design/layout.md), [Path-finder](../design/path-finder-integration.md).

## v0.4.0 candidate — Productive tool UI

Objective: add primitives required by real editor/utility consumers without growing a general widget catalog.

Depends on component identity, text/editing boundaries, and the Path-finder prototype.

Candidates:

- Production VirtualList, tree/hierarchy primitives, resizable split panels, a minimal docking subset.
- Editable TextInput, IME implementation, clipboard, selection, and drag/drop.
- Canvas/custom paint and performance instrumentation.
- Dirty style/layout/paint tracking after full-tree correctness.

Exit criteria to refine from a consumer prototype: long lists preserve keyed selection/focus, Japanese composition and clipboard work under declared platform evidence, custom paint respects the common draw/resource contract, and incremental results match full-tree output.

Owners: [UI model](../design/ui-model.md), [layout](../design/layout.md), [input](../design/input.md), [text](../design/text.md), [rendering](../design/rendering.md), [styling](../design/styling.md).

## v0.5.0 candidate — Accessibility, automation, and robust runtime

Objective: make UI reliably inspectable and operable through accessibility and external automation adapters.

Depends on stable semantics/actions, replay, and lifecycle contracts.

Candidates:

- Native accessibility adapters, semantic inspection, external automation and semantic action invocation.
- Replay recording tools, focus recovery/modal navigation hardening.
- Resource/device-loss diagnostics, install/export packaging, downstream integration checks.

Exit criteria to refine per adapter: semantic invocation obeys ordinary action eligibility, stale identities fail safely, adapter behavior has platform-specific evidence, replay reproduces failures, and downstream consumers exercise documented lifetime boundaries.

Owners: [semantics](../design/semantics.md), [input](../design/input.md), [replay](../design/replay.md), [rendering](../design/rendering.md), [dependencies](../reference/dependencies.md).

## Later — WebGPU, WASM, and world-space UI

Prerequisites: stable document, draw/resource and text boundaries, and replay fixtures suitable for parity checks.

Evaluate native WebGPU, browser WebGPU hosting, and WASM separately. Require equivalent document/geometry behavior and rendered fixture parity under declared tolerances. A backend name or shader target does not establish browser support.

World-space adapters and streamed resources require bounded host requirements. Metal/D3D12 need a concrete consumer before scheduling.

Owners: [architecture](../design/architecture.md), [rendering](../design/rendering.md), [dependencies](../reference/dependencies.md), [replay](../design/replay.md).

## Unscheduled extensions

- Host-clock property animation after baseline style/layout correctness; see [styling](../design/styling.md#proposed-animation).
- Richer layout algorithms in the order owned by [layout](../design/layout.md#extension-order).
- Finer dirty-subtree/reactive updates after measured need and full-tree parity.
- TypeScript-inspired DSL, then optional JSX/TSX compilation after the IR stabilizes; no JavaScript VM requirement.
- Broader editor bridge transport/process choices driven by actual consumers.

## Explicitly deferred

Full DOM/CSS compatibility, JavaScript VM, embedded browser, complete SVG, complex filters/effects, rich-text editor suite, huge widget catalog, scene-graph coupling, and an OpenUSD-backed internal UI tree are outside early/mid scope. Premature multithreaded API or stable ABI guarantees are also excluded.

Optional USD/engine integration reaches the runtime through view models and adapters under [architecture](../design/architecture.md).
