# Backlog

Updated: 2026-10-05. Everything below is planned and inactive. The active v0.1.0 minimal Vulkan menu candidate is in [current](current.md#active--v010-minimal-vulkan-menu-candidate); the [roadmap index](README.md) maps the original strategy phases. Versions are candidates without committed dates.

## v0.2.0 candidate — Usable navigable menus

Objective: move from a primitive experiment to menus and small tool panels with real text and non-pointer operation.

Depends on: stable v0.1.0 geometry, rendering, and dispatch boundaries.

Work:

- Font abstraction implementation, shaping, glyph cache, measurement, fallback, and wrapping.
- Declared UTF-8 Latin/Japanese coverage with fixed font fixtures.
- Focus, keyboard traversal, directional gamepad navigation, activation, and cancel/back.
- Scroll container and coordinated viewport/hit-test clipping.
- Style classes and the necessary hover/focus/disabled state resolution.

Exit criteria:

- Text metrics and painted glyphs agree for representative mixed Latin/Japanese and wrapping cases.
- The menu can be operated with keyboard and gamepad, including recovery after focused nodes disappear.
- Scrolled items paint and hit-test consistently; clipped/disabled items cannot activate incorrectly.
- Class/state rules resolve deterministically with tests for precedence and inheritance boundaries.

Design owners: [text](../design/text.md), [input](../design/input.md), [layout](../design/layout.md), [styling](../design/styling.md).

## v0.3.0 candidate — Dynamic UI and editor prototype

Objective: support application-driven composition and source iteration using the shared document model.

Depends on: stable document identity/versioning, menu interaction, and style/text foundations.

Work:

- Props/local state and bindings, conditional children, keyed list reconciliation, and cleanup.
- Application-facing image asset handling and theme variables.
- Source-aware diagnostics and atomic live reload with compatible-state preservation.
- Path-finder authoring/preview prototype using the runtime document schema.

Exit criteria:

- An inventory/tool-panel example updates keyed items without corrupting identity, focus, or binding cleanup.
- Missing/replaced assets and theme variables produce declared fallback/diagnostics.
- Invalid edits keep the last valid UI usable; compatible reload preserves declared local state and retires obsolete resources safely.
- Runtime loading and Path-finder preview round-trip the same document and preserve editor metadata.

Design owners: [UI model](../design/ui-model.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [Path-finder integration](../design/path-finder-integration.md).

## Later — Complete editor bridge and WebGPU

Complete the broader Path-finder bridge after the prototype proves identity, schema evolution, diagnostics, and reload. Choose transport and process boundaries from actual consumer needs.

WebGPU depends on a stable draw-list/resource contract and an evaluated shader/toolchain path. Validate native execution first where appropriate, then separately evaluate browser/WASM feasibility. Exit requires equivalent document/geometry behavior and representative rendered fixture parity under declared tolerances, with explicit shader, device, and unsupported-feature diagnostics.

Native WebGPU, browser hosting, and WASM are separate claims. None follows automatically from a backend name or a Slang target. Metal and Direct3D 12 remain optional later targets.

## Cross-cutting candidates

- Dirty style/layout/paint tracking after the full-tree reference is correct; finer reactive updates after measurement.
- Absolute layout, grid, and richer intrinsic sizing as required by concrete examples.
- UI property animation, additional paint primitives, and advanced glyph techniques after baseline correctness.
- Accessibility metadata schema early in model evolution; native accessibility adapters with separate platform evidence.
- TypeScript-inspired DSL, then optional JSX/TSX compiler after the IR is stable.
- Install/exported package and downstream-consumer checks when there is a consumer.
- Additional platforms, world-space UI adapters, and resource streaming only with bounded integration requirements.

## Explicitly deferred

Full browser DOM/CSS compatibility, a JavaScript VM, embedded browser, browser accessibility emulation, complete SVG, complex filters, rich text editor, a huge widget library, scene-graph coupling, and an OpenUSD-backed UI tree are outside early scope.

Optional USD bindings can connect world/application data through a view model. They must not become a foundational core dependency. Long-term consumers may include Mimikuri launchers/overlays, Path-finder editor UI, animu-sphere game/debug UI, OpenUSD utility tools, and lightweight WASM applications; these are possible uses rather than supported integrations.
