# Current

## Active — v0.1.0 minimal Vulkan menu candidate

Objective: complete the declarative document -> layout -> paint -> GPU pipeline with a small pointer-operated native menu.

Depends on document, resolved-style, layout, paint, and pointer contracts. Existing capability/evidence is recorded only in the [support matrix](../reference/support-matrix.md); previous slices are in the [changelog](../../CHANGELOG.md). This page lists remaining work.

## Remaining required work

- Build the selected Win32 standalone menu host with explicit window/device/queue/swapchain ownership, the [placeholder Text path](../design/rendering.md#implemented-vulkan-placeholder-text), normalized pointer input, and cancellation delivery; see [dependencies](../reference/dependencies.md#adopted-vulkan-choices) and [input](../design/input.md).
- Exercise conservative adjacent batching preserving visible order where the native host justifies it; use the existing ordered primitive path as the reference under [rendering](../design/rendering.md#batching).
- Validate native logical/pointer coordinates, framebuffer size, device scale, resize, and clip/transform agreement at the host boundary; see [layout](../design/layout.md#coordinate-spaces) and [rendering](../design/rendering.md#coordinate-conversion).
- Introduce basic property metadata for the existing vocabulary, sharing validation/default information with the runtime; see [reflection](../design/ui-model.md#proposed-property-reflection).
- Preserve deterministic geometry/paint and offscreen primitive/placeholder Text GPU fixtures, and add focused native-menu/presentation evidence through the [testing strategy](../guides/testing.md).

## Optional early prototypes

- [Replay](../design/replay.md) recording/playback v0 for deterministic geometry, paint, and expected actions.
- Minimal [semantic schema](../design/semantics.md) review/prototype ahead of navigable menus.

These prototypes are recommended foundations, not additional release gates. They must not silently extend JSON v1 or imply stable public APIs.

## Exit criteria

- A JSON document produces deterministic geometry/paint and a visible Vulkan menu.
- Overlap, border, clip, transform, and basic alpha cases have runtime image evidence under declared tolerances.
- Native pointer clicks produce expected host action requests; focus/capture loss cancels presses predictably.
- Logical/physical conversion works at declared scales; core public types remain backend-neutral.
- Property metadata covers the chosen foundation vocabulary and agrees with validation/defaults.
- Submission/resource retirement is verified for the concrete host. Record actual OS/compiler/GPU, placeholders, and unsupported cases in support.

## Immediately next

Connect the selected Win32 menu host to the optional Vulkan renderer with explicit [placeholder Text opt-in](../design/rendering.md#implemented-vulkan-placeholder-text). Keep window/input/DPI/swapchain ownership in the host. Paint output/limits are defined in [rendering](../design/rendering.md#implemented-paint-generation); the [Vulkan boundary](../design/rendering.md#implemented-vulkan-primitive-boundary) defines recording and retirement.

Later candidate scopes are in [backlog](backlog.md).
