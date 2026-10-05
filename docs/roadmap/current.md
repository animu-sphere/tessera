# Current

## Active — v0.2.0 usable navigable menus

Objective: make game/tool menus usable with real text and mouse, keyboard, or gamepad.

Depends on the v0.1.0 geometry, renderer, coordinate, and action boundaries. Existing capability/evidence is recorded only in the [support matrix](../reference/support-matrix.md); the v0.1.0 release and earlier slices are in the [changelog](../../CHANGELOG.md). This page lists remaining work.

## Remaining required work

- Real font abstraction, shaping, glyph cache, fallback, wrapping, and fixed Latin/Japanese fixtures.
- Keyboard focus traversal, directional gamepad navigation, activate/back, and focus recovery.
- ScrollView with matching paint/hit-test clipping.
- Style classes, hover/focus/disabled/active states, deterministic precedence and inheritance boundaries.
- SemanticTree v1 and Replay v1.
- IME/clipboard boundary contracts; DPI and pixel-snapping validation.
- In-process inspection with scoped target resolution, source mapping where available, and structured diagnostics; deterministic offscreen runner/capture prototype with declared state, time, viewport/scale, locale, fonts, and resources.
- Carried from v0.1.0: validate transform agreement between native pointer mapping and rendering once menu paint or hit testing uses transforms; see [layout](../design/layout.md#coordinate-spaces) and [rendering](../design/rendering.md#coordinate-conversion).
- Preserve deterministic geometry/paint, offscreen primitive/placeholder Text GPU fixtures, adjacent-batch/reference agreement, and the native menu smoke, including its clip-edge agreement, through the [testing strategy](../guides/testing.md).

## Exit criteria

- The same menu produces equivalent actions through mouse, keyboard, and gamepad.
- Real measurement and painted glyph geometry agree in mixed Latin/Japanese wrapping fixtures.
- Scrolled/clipped nodes paint and hit-test consistently; disabled nodes do not activate.
- Class/state resolution is deterministic and semantic state matches focus/action eligibility.
- Replay reproduces representative interactions, including expected action sequences.
- An in-process consumer observes one coherent tree/layout/semantic generation and rejects ambiguous or stale targets; controlled host fixtures reproduce it without a window, with optional completed-frame capture.

Owners: [text](../design/text.md), [input](../design/input.md), [layout](../design/layout.md), [styling](../design/styling.md), [semantics](../design/semantics.md), [replay](../design/replay.md), [inspection](../design/inspection.md), [rendering](../design/rendering.md).

## Immediately next

Translate native keyboard input in the [Win32 example host](../design/rendering.md#implemented-win32-vulkan-example-host) into the logical commands of the [prototype focus dispatch](../design/input.md#implemented-prototype-focus-dispatch), so the host menu produces its pointer actions from the keyboard; this needs no new dependency. Real text requires an adoption decision for the font/shaping implementation under [dependencies](../reference/dependencies.md#decisions-still-required) before code lands.

Later candidate scopes are in [backlog](backlog.md).
