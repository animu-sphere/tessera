# Current

## Active — v0.2.0 usable navigable menus

Objective: make game/tool menus usable with real text and mouse, keyboard, or gamepad. This milestone completes runtime correctness and adds no large UI components.

Depends on the v0.1.0 geometry, renderer, coordinate, and action boundaries. Existing capability/evidence is recorded only in the [support matrix](../reference/support-matrix.md); the v0.1.0 release and earlier slices are in the [changelog](../../CHANGELOG.md). This page lists remaining work.

## Remaining required work

- Real text beyond the [font shaper](../design/text.md#implemented-font-shaper), [family/logical-alias selection](../design/text.md#implemented-family-and-logical-alias-selection), [bounded menu wrapping profile](../design/text.md#implemented-menu-wrapping-profile), and [native menu font path](../design/rendering.md#implemented-win32-vulkan-example-host): broader Unicode line-break/Japanese tailoring as fixtures require it, and broader backend image evidence under the [deterministic font profile](../design/text.md#proposed-deterministic-font-profile).
- Focus recovery hardening as menus require it; an operator session with a physical controller for the Win32 host's [gamepad translation](../design/input.md#host-boundary).
- SemanticTree v1; Replay v1 scale-change, animation-time, and resource-readiness inputs beyond [Replay JSON v1](../design/replay.md#implemented-replay-json-v1) as fixtures require them.
- IME/clipboard boundary contracts; DPI and pixel-snapping validation.
- In-process inspection with scoped target resolution, source mapping where available, and structured diagnostics; extend the [offscreen runner](../design/inspection.md#implemented-prototype-offscreen-runner) with declared state, animation time, and resource readiness inputs as fixtures require, and with real-font capture beyond the [Vulkan fixture adapter](../design/rendering.md#implemented-capture-boundary) as fixtures require.
- Carried from v0.1.0: validate transform agreement between native pointer mapping and rendering once menu paint or hit testing uses transforms; see [layout](../design/layout.md#coordinate-spaces) and [rendering](../design/rendering.md#coordinate-conversion).
- Design only: settle the proposed [command registry and transaction boundary](../design/commands.md) and [async state](../design/ui-model.md#proposed-async-state) contracts against the input, semantics, replay, and inspection contracts, so the next milestone can implement them. No implementation is required here.
- Preserve deterministic geometry/paint, offscreen primitive/placeholder Text GPU fixtures, adjacent-batch/reference agreement, and the native menu smoke, including its clip-edge agreement, through the [testing strategy](../guides/testing.md).

## Exit criteria

- The same menu produces equivalent actions through mouse, keyboard, and gamepad.
- Real measurement and painted glyph geometry agree in mixed Latin/Japanese wrapping fixtures.
- Scrolled/clipped nodes paint and hit-test consistently; disabled nodes do not activate.
- Class/state resolution is deterministic and semantic state matches focus/action eligibility.
- Replay reproduces representative interactions, including expected action sequences.
- An in-process consumer observes one coherent tree/layout/semantic generation and rejects ambiguous or stale targets; controlled host fixtures reproduce it without a window, with optional completed-frame capture.
- The command, transaction, and async-state pages list their resolved rules, remaining open decisions, and first implementation fixtures.

Owners: [text](../design/text.md), [input](../design/input.md), [commands](../design/commands.md), [UI model](../design/ui-model.md), [layout](../design/layout.md), [styling](../design/styling.md), [semantics](../design/semantics.md), [replay](../design/replay.md), [inspection](../design/inspection.md), [rendering](../design/rendering.md).

## Immediately next

Continue hardening [focus recovery](../design/input.md#implemented-prototype-focus-dispatch) for scrolled menus where fixtures expose gaps, such as focus left out of view by a resize, which no step reveals, using replay recordings and the [offscreen runner](../design/inspection.md#implemented-prototype-offscreen-runner) for fixtures. Preserve the existing offscreen, real-font runner, and native glyph checks, and extend the [bounded menu wrapping profile](../design/text.md#implemented-menu-wrapping-profile) only for demonstrated Unicode/Japanese fixture requirements. A physical-controller session can confirm the gamepad path whenever a controller is available.

Later candidate scopes are in [backlog](backlog.md).
