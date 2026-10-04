# Rendering

Status: Draft design. Vulkan is the first backend target; none is implemented.

## Paint boundary

```text
retained nodes -> resolved style -> layout -> paint -> UiDrawList
-> backend draw packets -> GPU submission
```

The UI layer generates resolved drawing data. Backends consume that data without evaluating components, selectors, layout, events, or application state.

Candidate commands:

```text
DrawRect / DrawRoundedRect / DrawBorder
DrawImage / DrawGlyphRun
PushClip / PopClip
PushTransform / PopTransform
```

The draw list must encode explicit paint order and resource references. Define clip/transform stack nesting, opacity/blend semantics, color space, and invalid-command diagnostics. Resource references are neutral handles; Vulkan or WebGPU objects cannot appear in the UI document or shared draw-list API.

Proposed baseline: preserve tree-derived paint order, use a documented alpha convention, and keep clipping/transforms consistent with hit testing. Stacking rules, clip shapes, transformed clipping, and offscreen composition remain open decisions. Begin with rectangles and simple clips; extend command coverage only with evidence.

## Renderer interface and host integration

Conceptual interface, not compilable API documentation:

```cpp
class UiRenderer {
public:
    virtual void beginFrame(const UiFrameInfo&) = 0;
    virtual void submit(const UiDrawList&) = 0;
    virtual void endFrame() = 0;
};
```

Before finalizing it, define frame dimensions/device scale, render target format and color space, resource bindings, errors, submission lifetime, and completion. An explicit host submission context or result may replace this sketch.

The host owns the GPU device, queues, window, frame scheduling, and presentation. The concrete backend owns its pipelines, descriptor/binding data, UI uploads, and command generation. An example host can own a standalone device/window; that does not transfer those responsibilities into the UI core.

Submitted CPU data and resource versions must remain valid for the period declared by the backend contract. GPU-visible resources cannot be reclaimed before completion. Specify synchronization, resize, resource replacement, and device-loss behavior rather than relying on hidden global state. Borrowed-device integration and standalone examples must obey the same ownership rules.

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
