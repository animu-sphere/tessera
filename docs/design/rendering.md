# Rendering

Status: Draw-list and frame-submission contracts implemented. No paint generation or backend is implemented; Vulkan is the first backend target.

## Implemented draw-list contract

[draw_list.hpp](../../include/tessera/render/draw_list.hpp) defines `UiDrawList`, an owned ordered vector of commands. Later commands paint above earlier ones. Geometry is in logical units within the current transform.

| Command | Meaning |
| --- | --- |
| `DrawRect` | Filled rectangle; `corner_radius > 0` makes it rounded |
| `DrawBorder` | Border painted inside its rectangle with per-edge widths |
| `DrawImage` | `ImageHandle`, normalized source rectangle, and tint |
| `DrawGlyphRun` | Owned [`GlyphRun`](text.md) placed at the text box top left, with color |
| `PushClip` / `PopClip` | Intersect / restore the axis-aligned clip |
| `PushTransform` / `PopTransform` | Compose `current * transform` / restore; `Affine2D` maps `x' = a*x + c*y + tx`, `y' = b*x + d*y + ty` |

Colors are sRGB-encoded with straight alpha in [0, 1]; backends convert to their target format and blending convention. A node's opacity is multiplied into its command colors; group opacity (offscreen composition) is not representable yet. `ImageHandle` is a neutral, nonzero value issued by the host/backend resource system; the draw list never contains GPU objects.

Clips and transforms share one LIFO stack with a maximum depth of 64. `validate(UiDrawList)` reports a pop that does not close the innermost push of the same kind or a push left open (`unbalanced_stack`), excessive nesting (`stack_limit`), a clip pushed while the composed transform rotates or skews (`unsupported_clip`), a non-invertible transform (`singular_transform`), a zero image handle (`invalid_handle`), and non-finite or out-of-range geometry and colors. Paths are `/commands/<index>/...`.

## Implemented frame contract

[renderer.hpp](../../include/tessera/render/renderer.hpp) defines `FrameInfo` (a nonzero frame number strictly increasing per renderer, positive logical size, positive device scale in physical pixels per logical unit) and the abstract `UiRenderer`:

- `submit(frame, list)` borrows both arguments for the call only. A backend copies or encodes everything it needs before returning. Invalid input returns diagnostics and records nothing.
- Images referenced by frame N stay valid until the host calls `retire(M)` with M >= N. The host, which owns queues and fences, calls `retire` after observing GPU completion. A backend may reclaim its own upload memory for retired frames.
- The host provides the device, queues, render target, and presentation through the concrete backend's own construction or host-context API, never through core types. Target format, color-space conversion, resize, and device-loss behavior are specified per backend.

No concrete backend implements this interface yet; the contract alone is not backend evidence.

## Paint boundary

```text
retained nodes -> resolved style -> layout -> paint -> UiDrawList
-> backend draw packets -> GPU submission
```

The UI layer generates resolved drawing data. Backends consume that data without evaluating components, selectors, layout, events, or application state.

Proposed baseline for paint generation: preserve tree-derived paint order and keep clipping/transforms consistent with hit testing. Stacking rules, non-rectangular clip shapes, transformed clipping, blend modes, and offscreen composition remain open decisions. Begin with rectangles and simple clips; extend command coverage only with evidence.

## Host integration

The host owns the GPU device, queues, window, frame scheduling, and presentation. The concrete backend owns its pipelines, descriptor/binding data, UI uploads, and command generation. An example host can own a standalone device/window; that does not transfer those responsibilities into the UI core.

CPU data and resource lifetimes follow the frame contract above: GPU-visible resources cannot be reclaimed before `retire` covers their last frame. Each backend specifies synchronization, resize, resource replacement, and device-loss behavior rather than relying on hidden global state. Borrowed-device integration and standalone examples must obey the same ownership rules.

## Backends and shaders

Initial module: `backends/vulkan/`. Later: `backends/webgpu/`. Metal and Direct3D 12 are possible future targets without a delivery commitment.

Slang is the proposed shader layer for rectangles, rounded corners, borders, images, glyphs, clipping, and later effects. Keep it below `UiDrawList`; ordinary components do not refer to shader entry points or pipeline objects. Use a small set of general-purpose primitive pipelines rather than one pipeline per widget.

SPIR-V for Vulkan is the first intended artifact path. A WebGPU shader-target path, including WGSL suitability, must be checked against the eventually selected toolchain. No shader target, SDK version, browser, or WASM support is validated today. See [dependencies](../reference/dependencies.md).

## Batching

Candidate batch keys are pipeline, texture, clip state, and blend mode. Only combine commands when their visible ordering remains correct; global texture sorting can change overlapping translucent UI. Prefer adjacent compatible batches first. Track submission and upload costs once a correct baseline exists, without making speculative optimization a foundation requirement.

`Canvas` / custom paint uses this command vocabulary and observes balanced clip/transform state. It must not bypass draw-list resource lifetime or depend on a concrete backend.

## Assets

Fonts, images/icons, textures, style sheets, UI documents, and future vector assets come through an external asset interface. The host resolves logical asset IDs into resources; Tessera must not hard-code filesystem or network access.

The strategy's illustrative `loadUiAsset(id)` is a starting point. Finalize handle ownership, readiness/failure states, cancellation, fallback, resource invalidation, and replacement separately. Low-level image drawing can be tested in the primitive backend phase before the application-facing `Image` asset component lands.

## Verification

Validate draw-list order, balanced stacks, invalid handles, and completion-safe resource lifetime independently of screenshots. Use a few image cases for overlapping rectangles, clipping, transforms, alpha, borders, and later images/glyphs. Record viewport scale, target format, backend, device, and tolerances with results. A shader compile check alone is not runtime rendering evidence.
