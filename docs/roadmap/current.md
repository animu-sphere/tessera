# Current

## Active — v0.1.0 minimal Vulkan menu candidate

Objective: complete the declarative document -> layout -> paint -> GPU pipeline with a small pointer-operated native menu.

Depends on document, resolved-style, layout, paint, and pointer contracts. Existing capability/evidence is recorded only in the [support matrix](../reference/support-matrix.md); previous slices are in the [changelog](../../CHANGELOG.md). This page lists remaining work.

## Remaining required work

- Record an interactive session of the [Win32 example host](../design/rendering.md#implemented-win32-vulkan-example-host) with physical mouse input and a real monitor DPI change; its automated smoke posts window messages. Mapping rules are in [input](../design/input.md#host-boundary).
- Exercise conservative adjacent batching preserving visible order where the native host justifies it; use the existing ordered primitive path as the reference under [rendering](../design/rendering.md#batching).
- Validate clip/transform agreement between native pointer mapping and rendering once menu paint or hit testing uses clips/transforms; see [layout](../design/layout.md#coordinate-spaces) and [rendering](../design/rendering.md#coordinate-conversion).
- Preserve deterministic geometry/paint, offscreen primitive/placeholder Text GPU fixtures, and the native menu smoke through the [testing strategy](../guides/testing.md).

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

Record the interactive [Win32 example host](../design/rendering.md#implemented-win32-vulkan-example-host) session with physical mouse input and a real monitor DPI change; this needs an operator. Code work can proceed with conservative adjacent batching in that host, using the ordered primitive path as the reference under [rendering](../design/rendering.md#batching).

Later candidate scopes are in [backlog](backlog.md).
