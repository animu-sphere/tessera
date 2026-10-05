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

Validate fractional scales and scale changes as well as 1:1 rendering. Normalized input must map back to the same logical space. World-space projection belongs to a host adapter. The [Win32 example host](#implemented-win32-vulkan-example-host) defines one concrete logical-size and pointer-pixel mapping.

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
- **Clips and overflow.** Commands remain in root logical coordinates. Each emitting box's commands are enclosed in a `PushClip` of its recorded [layout clip](layout.md#implemented-prototype-algorithm); consecutive emitting boxes with the same clip share one push/pop pair, so generated clips never nest. Unclipped overflow paints in place. There is no implicit viewport clip, transform, image emission, z-index, or custom paint callback. Transforms remain available to direct draw-list authors; future scrolling and transformed geometry must share their semantics with hit testing.

`validate(PaintInput)` rejects missing tree/layout/shaper (`missing_input` at `/tree`, `/layout`, `/text`), a wrong style count (`style_count` at `/styles`), and invalid styles under `/styles/<index>`. It reconstructs the displayed topology and requires exactly one box per displayed node (`layout_count` at `/layout/boxes`), matching live handles in preorder (`layout_node`), and matching layout parent indices (`layout_parent`). Border/padding/visibility must agree with the current styles (`layout_style`), and each recorded clip must equal the clip derived from its parent's geometry and overflow (`layout_clip` at `/layout/boxes/<index>/clip`). Box numeric diagnostics use the draw-list geometry rules under `/layout/boxes/<index>`; derived content geometry must also be finite. Invalid input returns no value and never calls the shaper.

The caller must recompute layout after any geometry or text/font change. Validation catches topology and recorded-edge/visibility mismatches; the result does not store a style fingerprint, so it cannot detect every stale dimension or text metric.

Shaping diagnostics are prefixed with `/nodes/<NodeHandle::index>`. A failed shaper returning no error diagnostic receives `text_shape_failed` at `/nodes/<index>/text`. Generated commands are validated before returning; malformed glyph data is located under `/nodes/<index>/commands/<local-command-index>`. Any error discards the entire list, including commands from preceding nodes. Successful shaping warnings are preserved with the owned list. Placeholder runs remain test data, not real font evidence; the optional backend's explicitly enabled placeholder path is defined below.

Non-rectangular clip shapes, transformed clipping, blend modes, and offscreen composition remain open decisions. Extend command coverage only with evidence.

## Host integration

The host owns the GPU device, queues, window, frame scheduling, and presentation. The concrete backend owns its pipelines, descriptor/binding data, UI uploads, and command generation. An example host can own a standalone device/window; that does not transfer those responsibilities into the UI core.

CPU data and resource lifetimes follow the frame contract above: GPU-visible resources cannot be reclaimed before `retire` covers their last frame. Each backend specifies synchronization, resize, resource replacement, and device-loss behavior rather than relying on hidden global state. Borrowed-device integration and standalone examples must obey the same ownership rules.

## Backends and shaders

The optional Vulkan module is defined below. WebGPU is the intended rendering boundary for the [Web host](web-host.md), with native WebGPU evaluated separately. Metal and Direct3D 12 require a concrete consumer. Scheduling remains in backlog and configuration evidence in support.

Slang compiles the Vulkan [primitive shader](../../backends/vulkan/shaders/primitive.slang) below `UiDrawList`; ordinary components do not refer to shader entry points or pipeline objects. It is a build tool for this module, not a core requirement. The [module build](../../backends/vulkan/CMakeLists.txt) emits separate SPIR-V 1.3 reference vertex, batch vertex, solid fragment, and image fragment artifacts, preserving entry-point names. `SV_VulkanVertexID` avoids requiring the optional draw-parameters feature; see the [Slang SPIR-V mapping](https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/a2-01-spirv-target-specific.html).

Reassess shader-source strategy for WebGPU, including WGSL suitability, against its selected toolchain. Adopted choices belong in [dependencies](../reference/dependencies.md), validation in [support](../reference/support-matrix.md).

### Implemented Vulkan primitive boundary

[renderer.hpp](../../backends/vulkan/include/tessera/vulkan/renderer.hpp) defines `VulkanRenderer`, available through `tessera::vulkan` only when `TESSERA_BUILD_VULKAN=ON`. Its header exposes Vulkan types within this optional module; core headers remain SDK-independent. It records commands and never submits queues, waits on the host, or presents.

- **Construction and ownership.** `VulkanContext` borrows the host physical/logical device and compatible render pass, with subpass 0 containing one single-sampled color attachment. The host enables Vulkan 1.1 or newer. Only `R8G8B8A8_SRGB` and `B8G8R8A8_SRGB` are accepted. SPIR-V spans are borrowed during construction only. The renderer owns two reference pipelines, two additional pipelines when batching is enabled, a pipeline layout, descriptor layout, and bounded image descriptor pool. Unsupported construction arguments throw `std::invalid_argument`; Vulkan creation/allocation failures throw `std::runtime_error` carrying the failing operation/result. Construction cleans up partially created objects. The host completes and retires all frames before destroying the renderer, then may destroy its borrowed device/pass/resources. No internal queue wait or device-loss recovery occurs.
- **Target and submission.** `set_target` borrows a command buffer already recording inside the compatible render pass and a physical extent. `submit` validates the entire frame/list, image bindings, composed/transformed numeric bounds, and target before recording anything or updating image use. It rejects stale frame numbers (`stale_frame`), missing/unsupported target extents (`invalid_target`), extent mismatch (`extent_mismatch`), unsafe float intermediates (`geometry_overflow`), unknown images (`invalid_handle`), and glyph commands without explicit placeholder opt-in (`unsupported_command`). Rejection leaves frame numbers and resource use unchanged. Successful calls set viewport/scissors/pipelines/push constants/descriptors and emit commands in authored order. This changes command-buffer graphics state; a host rendering afterward must rebind its state. Calls are single-threaded and not reentrant.
- **Coordinates and clips.** Extent must equal `ceil(logical_size * device_scale)` per axis, computed in double from the supplied float values. The host calls `set_target` again after resize; pipelines can remain when pass/format compatibility is unchanged. Affine transforms compose in authored order. Clips become physical continuous axis-aligned bounds, including negative-axis transforms, intersect before rounding, and include a pixel when its center lies inside the half-open bounds: edges become `ceil(edge - 0.5)`, clamped to the framebuffer. A clip edge therefore covers exactly the pixels a primitive edge at the same position covers, and agrees with pixel-center pointer mapping. Empty intersections and zero-area primitives emit no draws. Clip/transform pops restore the complete previous state. Rotated/skewed clips remain rejected by common draw-list validation; rotated/skewed primitives are accepted.
- **Primitive pixels.** A six-vertex quad carries local coordinates; fragments discard outside circular rounded corners. Radius clamps to half the smaller side. Borders subtract an inset rounded inner rectangle; its radius is `max(0, authored_radius - max(edge_widths))`, then clamps to the inner dimensions. If inner dimensions collapse the whole outer shape is filled. This is the prototype's explicit unequal-border corner rule. No antialiasing/MSAA or pixel snapping is introduced. RGB command colors convert from sRGB to linear; straight-alpha source-over blending occurs in the sRGB attachment's linear domain before attachment encoding. Solid and image pipelines cover the commands. The default reference path keeps each primitive a separate draw and reuses adjacent pipeline binds; the optional instance path follows [batching](#batching).
- **Images and retirement.** `bind_image` associates a nonzero neutral handle with borrowed host `VkImageView`/`VkSampler` from the same device; views expose linear RGB to the shader (normally an sRGB view for encoded assets), and samples use straight alpha. The host uploads/transitions the image to `SHADER_READ_ONLY_OPTIMAL` and maintains that state and object lifetime through retirement. The source rectangle must fit normalized [0,1] coordinates; tint uses the same linear color/straight-alpha rules. `max_images` bounds live bindings (`image_limit`). Rebind/unbind returns `resource_in_use` until the last successful referencing frame is retired, including clipped/zero-area image references. Unknown unbinds fail. `retire` advances monotonically, clamps to the last successful frame, and does not authorize resource changes in future frames. The host calls it only after observing completion of every covered frame. Replacement updates only retired descriptors; no external asset loading API is implied.

Real font rasterization and device-loss recovery require subsequent implementation. Swapchain presentation is host work; see the [Win32 example host](#implemented-win32-vulkan-example-host).

### Implemented Vulkan placeholder Text

`VulkanContext::placeholder_text` defaults false. When enabled, the host promises every `DrawGlyphRun` was produced by `PlaceholderTextShaper` with default `FontId` 0. The backend cannot infer the shaper from numeric glyph IDs. Nonzero font IDs fail with `unsupported_font` at `/commands/<index>/run/font`; surrogate IDs and IDs above U+10FFFF fail with `invalid_glyph` at `/commands/<index>/run/glyphs/<index>/id`. No real font, fallback service, atlas, or additional GPU resource is introduced.

The original [5x7 bitmap marks](../../backends/vulkan/placeholder_glyph.hpp) cover ASCII letters/digits and `. , : - _ + / ! ? ( )`. Lowercase uses uppercase shapes. Space draws nothing; other Unicode scalars use an outlined missing-glyph box, including Japanese characters. This is a legible menu/test placeholder, not Latin/Japanese font support.

Each non-space glyph emits one solid-pipeline quad. Its top left is `command.origin + glyph.position + (0.05 em, -0.8 em)`; size is `(0.4 em, 0.7 em)`, using `GlyphRun::size`. Rows are supplied in per-primitive data and the fragment shader discards unset cells. Placement honors the shaper's baseline pen positions, including explicit line heights and multiline runs, without deriving positions from run metrics. It preserves the placeholder shaper's 0.5 em advance. Cells have hard edges, with no antialiasing or pixel snapping.

Glyphs inherit the current affine transform, physical scissor, authored order, and linear-light straight-alpha blending. Origin/pen sums, quad bounds, and transformed intermediates are checked before recording, including whitespace and fully clipped glyphs. A rejected run records nothing, consumes no frame number, and does not pin preceding image references. Empty runs and spaces draw no pixels. Bitmap data is copied into command-buffer push constants on the reference path or retained instance uploads on the batch path; it needs no atlas retirement, and uses the existing host completion/destruction rule.

### Implemented Win32 Vulkan example host

[vulkan-menu](../../examples/vulkan-menu/main.cpp) is an example host built with the optional module on Windows (`tessera_vulkan_menu`). It is not a library API: window, OS input, DPI, instance/device/queue/surface/swapchain, synchronization, presentation, and retirement stay in the executable. The renderer receives only the borrowed context, a recording command buffer, `FrameInfo`, and the owned paint list.

- **Document path.** An embedded JSON v1 menu becomes a `UiTree` whose buttons carry the `button` class; a host-built `StyleSheet` and the current hover/active/focus state go through the [prototype resolver](styling.md#implemented-prototype-style-resolution); the host runs `compute_layout` and `build_paint_list` with `PlaceholderTextShaper`, enables `VulkanContext::placeholder_text`, and supplies the batch vertex artifact to enable adjacent batching. A fixed-height `overflow: clip` list cuts its last button roughly in half, so presentation uses two scissor batches. The buttons are `focusable`, and their labels inherit text size and color. `.button:hover` and `.button:active` rules change only backgrounds and `.button:focus` only the border color; whenever resolved styles change after input, the host recomputes layout and paint.
- **Presentation.** One graphics queue family that also presents, an sRGB-nonlinear `B8G8R8A8_SRGB` (preferred) or `R8G8B8A8_SRGB` surface format, FIFO, and two frames in flight. Each slot owns a command buffer, acquire semaphore, and fence; each swapchain image owns its render-finished semaphore. After waiting a slot or image fence, the host calls `retire` with the last successful renderer frame submitted before that fence, because the fence also covers earlier submissions on the queue. A rejected submission still presents the cleared image and consumes no frame number.
- **Update point.** `WM_SIZE` and `WM_DPICHANGED` (scale = DPI / 96, suggested window rectangle applied) idle the device, retire every submitted frame, recreate the swapchain/framebuffers, recompute layout, and `refresh` the pointer and focus dispatchers before later input is mapped. A zero client extent suspends rendering. The swapchain extent is authoritative; each logical axis is the largest `float` whose `ceil(logical * scale)`, computed as the renderer does, equals the physical extent.
- **Pointer pixels.** Integer client pixel `(x, y)` maps to logical `((x + 0.5) / scale, (y + 0.5) / scale)`, the pixel center sampled by rasterization and by the scissor, so a pixel painted by a half-open border box inside its clip hits the same box. [Input](input.md#host-boundary) owns the message-to-event mapping.
- **Smoke readback.** `--smoke` requires the validation layer with synchronization validation, adds transfer-source usage to swapchain images, and reads selected frames back through a render pass that differs only in final layout (compatible passes carry identical dependencies). It posts mouse, capture-loss, cancel-mode, key, and synthetic `WM_DPICHANGED` messages through the window procedure, feeds scripted gamepad samples through the polling path in place of XInput, checks the host action sequence, focused node, the resolved `:active` background of a held button and `:focus` border of the focused one, and presented background/left-border pixels against the styles that produced them, and fails on validation errors or rejected submissions/events. Each capture compares every pixel within three rows of the list clip edge with `hit_test` at that pixel's center; posted clicks on the rows just inside and outside the edge must and must not activate the clipped button.

Separate present queues, device loss, and real fonts are not handled by this host.

## Batching

### Implemented Vulkan adjacent batching

The optional [renderer](../../backends/vulkan/include/tessera/vulkan/renderer.hpp) enables batching when `VulkanContext::batch_vertex_spirv` contains the compiled `batchMain` vertex entry point; an empty span selects the individual-draw reference path. Both paths use the same validated ordered packets and fragment shaders. No core or JSON v1 contract changes.

- **Grouping and order.** Merge only consecutive emitted packets with the same solid/image pipeline, image handle (zero for solid), and identical physical integer scissor. Source-over blending is fixed. Rectangle, border, and placeholder glyph modes, color, image crop/tint, and transform are per-instance values, so they can differ within a batch. Stack commands resolve into packet state before grouping; no sorting occurs. Empty/clipped commands emit no packet, but referenced images still obey the existing retirement rule. A batch records `vkCmdDraw(6, instance_count, 0, first_instance)` in packet order. Instance order and framebuffer blending obey Vulkan [primitive order](https://docs.vulkan.org/spec/latest/chapters/drawing.html#drawing-primitive-order) and [rasterization order](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-order).
- **Uploads and failure.** Each nonempty successful batch submission allocates its own host-visible vertex buffer with 112 bytes per emitted primitive; coherent memory is preferred, otherwise the entire mapped allocation is flushed before recording. No buffer is overwritten or reused across in-flight submissions. Host `retire` releases only uploads covered by its clamped completion value. Empty lists allocate no upload. All validation and allocations finish before any commands, image-use updates, or success counters change. Vulkan allocation/map/flush failures throw `std::runtime_error` with the failing operation/result; missing host-visible memory also throws. More than `UINT32_MAX` emitted primitives returns `primitive_limit` at `/commands` on either path. Hosts must regularly retire completed frames; retained upload count grows with unretired nonempty submissions. Pooling and timing claims are outside this contract.
- **Observations.** `submission_stats()` returns emitted `primitives`, recorded `draw_calls`, and payload `upload_bytes` for the last successful call; before success all are zero, and rejection preserves them. Reference uploads report zero bytes. Allocation padding is excluded. `pending_uploads()` reports retained batch buffers. These counters do not measure CPU or GPU duration. The native smoke checks batch reduction and its two-frame retention bound.

Keep the reference path for image comparison whenever grouping or packet encoding changes. Future blend modes must become batch keys before they can be merged.

`Canvas` / custom paint uses this command vocabulary and observes balanced clip/transform state. It must not bypass draw-list resource lifetime or depend on a concrete backend.

Retained packets, atlas use, packed instance data, and partial uploads are future optimization choices driven by representative tool workloads. Preserve ordering, blend/clip/transform batch boundaries, and completion-safe ownership. A separate Paint Tree or Render Tree is not required merely to mirror an architecture diagram; introduce a representation only with a defined consumer and lifetime.

## Proposed capture boundary

An offscreen host owns targets, submission, completion, and readback. A reusable capture service associates an image with the submitted frame/update generation, physical extent, scale, format/color convention, and backend/device metadata. CPU inspection and GPU completion must agree under the [snapshot bundle](inspection.md#proposed-runner-and-snapshot-bundle) contract. Readback does not weaken retirement requirements or add window/platform types to core.

Image encoding/export belongs to optional tooling, with explicit dependency/licensing choices. Existing fixture readback and PPM artifacts are examples of backend evidence, not a general PNG-capture API, runner, or CLI. A future software/reference backend is evaluated separately; headless means no window, not no GPU.

## Proposed overlay paint

The [overlay/portal model](ui-model.md#proposed-overlays-and-portals) provides layer identity and presentation ancestry. Paint uses shared placement/clip geometry to place overlay commands after the appropriate content layer, without inheriting ordinary content clips accidentally. Define deterministic ordering and balanced stack boundaries; input must use the same presentation order. Portals do not bypass resource retirement or create backend-owned components.

## Assets

Fonts, images/icons, textures, style sheets, UI documents, and future vector assets come through an external asset interface. The host resolves logical asset IDs into resources; Tessera must not hard-code filesystem or network access.

The strategy's illustrative `loadUiAsset(id)` is a starting point. Finalize handle ownership, readiness/failure states, cancellation, fallback, resource invalidation, and replacement separately. Low-level image drawing can be tested in the primitive backend phase before the application-facing `Image` asset component lands.

## Verification

Validate draw-list order, balanced stacks, invalid handles, and completion-safe resource lifetime independently of screenshots. Use a few image cases for overlapping rectangles, clipping, transforms, alpha, borders, and later images/glyphs. Record viewport scale, target format, backend, device, and tolerances with results. A shader compile check alone is not runtime rendering evidence.

[Vulkan fixtures](../../tests/render/vulkan_tests.cpp) provide a host-owned offscreen target, procedural 2x2 image, queue/fence/readback, required validation layer with synchronization validation, numeric pixel assertions, and PPM artifacts. They exercise source-over order, rounded/inside borders, nested/empty/mirrored clips with pixel-center fractional edges, composed/rotated/skewed transforms, image sampling/crop/tint, scale/resize, rejected calls, capacity and retirement. Placeholder fixtures additionally check bitmap/baseline/multiline/space/missing marks at four scales, glyph clip/transform/alpha, atomic rejection, and a JSON -> tree -> layout -> paint -> GPU Text menu. Both paths run these fixtures; batch checks also compare complete mixed-scene images against the reference at four scales and check draw counts, multiple retained uploads, rejection, and partial/future retirement. They fail when required device/layer capabilities are absent; they do not silently substitute CPU rendering. The [development guide](../guides/development.md#optional-vulkan-workflow) owns execution commands.
