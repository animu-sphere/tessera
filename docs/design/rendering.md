# Rendering

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

Concrete-backend validation is recorded in [support](../reference/support-matrix.md), separately from this interface contract.

## Coordinate conversion

`FrameInfo::device_scale` expresses physical pixels per logical unit. Layout and draw geometry follow [logical coordinates](layout.md#coordinate-spaces); the renderer/host boundary converts them to the physical framebuffer and backend viewport/scissor conventions. Define framebuffer extent, rounding, clip-edge conversion, and resize handling with the concrete backend. Pixel snapping is a later explicit scale-aware policy, not hidden rounding in layout.

Validate fractional scales and scale changes as well as 1:1 rendering. Normalized input must map back to the same logical space. World-space projection belongs to a host adapter.

## Paint boundary

```text
retained nodes -> resolved style -> layout -> paint -> UiDrawList
-> backend draw packets -> GPU submission
```

The UI layer generates resolved drawing data. Backends consume that data without evaluating components, selectors, layout, events, or application state.

### Implemented paint generation

[paint.hpp](../../include/tessera/render/paint.hpp) defines `PaintInput` and `build_paint_list(PaintInput)`. One pass borrows the immutable `UiTree`, one `ResolvedStyle` per tree node indexed by `NodeHandle::index`, its `LayoutResult`, and the same `TextShaper` used for layout. The returned `UiDrawList` owns all commands and glyph data and can outlive those inputs. No tree handle, backend object, asset lookup, or application callback is stored in the output.

- **Order and geometry.** Walk displayed boxes in tree preorder. Each box emits background (`DrawRect`), inside border (`DrawBorder`), then Text (`DrawGlyphRun`), before its children; later siblings paint above earlier subtrees. Rectangles use `border_box`, borders use its recorded per-edge widths, and both preserve `corner_radius`. Text is shaped using its required `text` string property and resolved `TextStyle`, and placed at `content_box().origin`; the shaper's glyph positions and metrics are preserved.
- **Alpha.** Each command's straight alpha is multiplied by the product of the node's and all ancestors' resolved opacity. RGB is unchanged. This per-command attenuation does not provide offscreen group composition; overlapping descendants can produce different pixels from group opacity.
- **Eligibility.** Display-none subtrees have no boxes. A hidden box suppresses its own commands; visibility is local in the supplied resolved styles, so an explicitly visible descendant can still paint. Ancestor opacity continues to apply through hidden boxes. Transparent backgrounds, borders, and text emit nothing; borders with all zero widths and runs with no glyphs emit nothing. Hidden or zero-opacity boxes and transparent Text do not call `shape`.
- **Overflow.** Commands remain in root logical coordinates, including overflowing content. There is no implicit viewport/container clip, transform, image emission, z-index, or custom paint callback. Clips/transforms remain available to direct draw-list authors; future scrolling and transformed geometry must share their semantics with hit testing.

`validate(PaintInput)` rejects missing tree/layout/shaper (`missing_input` at `/tree`, `/layout`, `/text`), a wrong style count (`style_count` at `/styles`), and invalid styles under `/styles/<index>`. It reconstructs the displayed topology and requires exactly one box per displayed node (`layout_count` at `/layout/boxes`), matching live handles in preorder (`layout_node`), and matching layout parent indices (`layout_parent`). Border/padding/visibility must agree with the current styles (`layout_style`). Box numeric diagnostics use the draw-list geometry rules under `/layout/boxes/<index>`; derived content geometry must also be finite. Invalid input returns no value and never calls the shaper.

The caller must recompute layout after any geometry or text/font change. Validation catches topology and recorded-edge/visibility mismatches; the result does not store a style fingerprint, so it cannot detect every stale dimension or text metric.

Shaping diagnostics are prefixed with `/nodes/<NodeHandle::index>`. A failed shaper returning no error diagnostic receives `text_shape_failed` at `/nodes/<index>/text`. Generated commands are validated before returning; malformed glyph data is located under `/nodes/<index>/commands/<local-command-index>`. Any error discards the entire list, including commands from preceding nodes. Successful shaping warnings are preserved with the owned list. Placeholder runs remain test data, not real font or GPU rendering evidence.

Non-rectangular clip shapes, transformed clipping, blend modes, and offscreen composition remain open decisions. Extend command coverage only with evidence.

## Host integration

The host owns the GPU device, queues, window, frame scheduling, and presentation. The concrete backend owns its pipelines, descriptor/binding data, UI uploads, and command generation. An example host can own a standalone device/window; that does not transfer those responsibilities into the UI core.

CPU data and resource lifetimes follow the frame contract above: GPU-visible resources cannot be reclaimed before `retire` covers their last frame. Each backend specifies synchronization, resize, resource replacement, and device-loss behavior rather than relying on hidden global state. Borrowed-device integration and standalone examples must obey the same ownership rules.

## Backends and shaders

Initial module: `backends/vulkan/`. Later: `backends/webgpu/`. Metal and Direct3D 12 are possible future targets without a delivery commitment.

Slang is the proposed shader layer for rectangles, rounded corners, borders, images, glyphs, clipping, and later effects. Keep it below `UiDrawList`; ordinary components do not refer to shader entry points or pipeline objects. Use a small set of general-purpose primitive pipelines rather than one pipeline per widget.

SPIR-V for Vulkan is the first intended artifact path. Evaluate Slang as an implementation choice below the public contract; it is not a core requirement. Reassess shader-source strategy for WebGPU, including WGSL suitability, against the selected toolchain. Adopted choices belong in [dependencies](../reference/dependencies.md), validation in [support](../reference/support-matrix.md).

## Batching

Candidate batch keys are pipeline, texture, clip state, and blend mode. Only combine commands when their visible ordering remains correct; global texture sorting can change overlapping translucent UI. Prefer adjacent compatible batches first. Track submission and upload costs once a correct baseline exists, without making speculative optimization a foundation requirement.

`Canvas` / custom paint uses this command vocabulary and observes balanced clip/transform state. It must not bypass draw-list resource lifetime or depend on a concrete backend.

## Proposed overlay paint

The [overlay/portal model](ui-model.md#proposed-overlays-and-portals) provides layer identity and presentation ancestry. Paint uses shared placement/clip geometry to place overlay commands after the appropriate content layer, without inheriting ordinary content clips accidentally. Define deterministic ordering and balanced stack boundaries; input must use the same presentation order. Portals do not bypass resource retirement or create backend-owned components.

## Assets

Fonts, images/icons, textures, style sheets, UI documents, and future vector assets come through an external asset interface. The host resolves logical asset IDs into resources; Tessera must not hard-code filesystem or network access.

The strategy's illustrative `loadUiAsset(id)` is a starting point. Finalize handle ownership, readiness/failure states, cancellation, fallback, resource invalidation, and replacement separately. Low-level image drawing can be tested in the primitive backend phase before the application-facing `Image` asset component lands.

## Verification

Validate draw-list order, balanced stacks, invalid handles, and completion-safe resource lifetime independently of screenshots. Use a few image cases for overlapping rectangles, clipping, transforms, alpha, borders, and later images/glyphs. Record viewport scale, target format, backend, device, and tolerances with results. A shader compile check alone is not runtime rendering evidence.
