# Current

## Active — v0.1.0 minimal Vulkan menu candidate

Objective: complete the declarative document -> layout -> paint -> GPU pipeline with a small pointer-operated native menu.

Depends on document, resolved-style, layout, paint, and pointer contracts. Existing capability/evidence is recorded only in the [support matrix](../reference/support-matrix.md); previous slices are in the [changelog](../../CHANGELOG.md). This page lists remaining work.

## Remaining required work

- Select Vulkan, shader, and example-host dependencies under the [dependency policy](../reference/dependencies.md). Evaluate Slang below the rendering boundary and record the chosen build/artifact path.
- Execute primitive draw commands in Vulkan: rectangles/rounded rectangles, inside borders, clips, transforms, basic alpha, and conservative batching preserving visible order. Exercise low-level image sampling without requiring a full application asset API; see [rendering](../design/rendering.md).
- Build a standalone menu host with explicit window/device/queue ownership, placeholder Text, normalized pointer input, and cancellation delivery; see [input](../design/input.md).
- Validate logical coordinates, framebuffer size, device scale, resize, and clip/transform conversion; see [layout](../design/layout.md#coordinate-spaces) and [rendering](../design/rendering.md#coordinate-conversion).
- Introduce basic property metadata for the existing vocabulary, sharing validation/default information with the runtime; see [reflection](../design/ui-model.md#proposed-property-reflection).
- Preserve deterministic geometry/paint fixtures and add a small GPU image fixture set through the [testing strategy](../guides/testing.md).

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

Choose and record renderer/shader/host dependencies, then execute primitive fixtures below `UiDrawList`. Keep the runtime independent of concrete implementations. Paint output/limits are defined in [rendering](../design/rendering.md#implemented-paint-generation); use direct draw-list fixtures for command forms it does not generate.

Later candidate scopes are in [backlog](backlog.md).
