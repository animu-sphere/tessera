# Look and visual rendering

> Proposed direction beyond implemented [linear](../architecture/rendering.md#implemented-linear-gradients) and [radial gradients](../architecture/rendering.md#implemented-radial-gradients), [distance-based outer/inset shadows](../architecture/rendering.md#implemented-box-shadows), [smooth solid/dashed outlines](../architecture/rendering.md#implemented-outlines-and-dashed-strokes), [analytic rectangle/ellipse fills](../architecture/rendering.md#implemented-analytic-fills), [fill-v1 Slang procedural fills](../architecture/rendering.md#implemented-procedural-fills) and [sdf-v1 custom shapes](../architecture/rendering.md#implemented-custom-shapes) and [offscreen layers](../architecture/rendering.md#implemented-offscreen-layers) with group opacity. [Current work](../roadmap/current.md) owns scheduled work; this proposal does not add release gates to v0.7.0 Application Experience.

## Goal and scope

Make ordinary Inspector and Settings screens attractive while allowing distinctive GPU-native visuals through SDF techniques and Slang. Authors should be able to combine built-in looks and custom effects in the same declarative UI without learning renderer internals.

Treat Look as the combination of shape, paint/material, effects and motion, rather than only theme colors. These are responsibilities, not a commitment to a new `Look` type, material framework or shader graph. Extend the existing authoring, style, paint and backend boundaries where they fit.

The slices below are useful starting points, not a fixed sequence or capability ceiling. A consumer can justify textures, additional passes, other shapes or a broader material/composition API without first completing every earlier slice. Choose a coherent user-visible result; a small first gradient PR is a suggested starting point, not a permanent PR-size or feature-selection rule.

## Responsibilities

| Concern | Responsibility | Starting examples |
| --- | --- | --- |
| Shape | Geometry and coverage, separate from layout allocation | Rounded rectangles, circles/ellipses, custom SDF |
| Paint / material | Color or other appearance inside coverage | Solid, linear/radial gradient, procedural Slang fill |
| Effect | Decoration or processing of drawing results | Stroke, shadow, local glow |
| Motion | Explicit changes over host-controlled time | Color, fill parameters, shape transitions |
| Composition | Relationships between drawing layers | Paint order, clips, offscreen layers with group opacity, later masks and effects |

SDF is a shape/coverage technique, not a required replacement for image sampling, ordinary glyph rendering or other efficient paths. Distinguish an exact signed distance from an inside/outside function or a distance estimate: smooth unions and deformations may change distance accuracy. Boundary-distance glow is an approximation, not the same operation as a blurred box shadow or backdrop blur.

## Rendering boundary

Preserve the [rendering contract](../architecture/rendering.md): retained nodes resolve through style and layout to owned, ordered, backend-neutral draw data. Backends do not evaluate UI state, layout, focus or input. Core interfaces remain free of Slang compiler, Vulkan and other SDK types.

At submission, bounds, shape, paint and parameters belong to one coherent frame snapshot. Prefer extending existing commands when their semantics fit; introduce commands such as a resolved shape or procedural fill when they express genuinely different drawing behavior. Names and authoring syntax remain open.

Each selected feature defines:

- Local and logical coordinates, transforms, bounds/overflow, clipping and fractional device-scale behavior.
- Shape coverage, antialiasing and stroke placement, including existing per-edge inside-border semantics where applicable.
- Color inputs, gradient interpolation space, shader calculation/output space and alpha/blending conversion. Current input colors are sRGB-encoded with straight alpha; inherited opacity attenuates individual commands unless a node composes as an offscreen layer.
- Validation, capability discovery and observable unsupported/failure behavior for the participating backends.
- Referenced resource ownership, replacement and completion-safe retirement.

These are extension points, not a requirement to redesign current `DrawRect` or the scalar CPU renderer before shipping a feature. Existing draw lists retain their documented behavior unless an explicit contract change says otherwise.

## Candidate implementation slices

### A. Visual basics

The [Inspector property card](../guides/inspector-hierarchy.md#gradient-property-card) uses a multi-stop radial gradient and a finite distance-based outer shadow through ordinary styles and CPU/Vulkan rendering, alongside existing linear fills and their original two-color shorthand. The [name input](../guides/inspector-hierarchy.md#name-input-inset-shadow) uses the same shadow property in inset mode; [focus outlines](../guides/inspector-hierarchy.md#focus-outlines) use smooth solid/dashed strokes. Add blurred shadows, additional caps/joins, broader shape coverage or text decoration where useful.

Define stop ordering/ranges, degenerate geometry, interpolation and alpha before implementation. Preserve rounded-corner clipping, existing border widths, paint order and continuous coordinates. Check transparent stops and fractional scales. A first consumer can be one card rather than a new application.

Provide a CPU reference for the built-in behavior or an explicitly declared capability/fallback path. Do not silently substitute a solid fill and call it gradient validation.

### B. Built-in analytic and SDF shapes

Circle/ellipse and rounded-rectangle background fills now have opt-in shared CPU/Vulkan 4x4 coverage under [analytic fills](../architecture/rendering.md#implemented-analytic-fills). The Inspector combines a circular Enabled indicator with its smooth gradient card. The ellipse equation supplies inside/outside coverage, not an exact Euclidean signed distance.

Extend these foundations with additional shapes, ellipse strokes and local glow when a consumer needs them. Exercise thin strokes, small dimensions, clamped radii and transformed/fractional-scale edges.

GPU analytic/SDF evaluation and a small CPU reference should express the same visual semantics, with declared tolerances rather than mandatory identical algorithms or bitwise pixels. Keep the existing layout-box hit test by default; shape-aware input is an explicit choice with its own CPU geometry contract, not implicit execution of a GPU shader during input dispatch.

### C. Slang procedural fill

The [fill-v1 profile](../architecture/rendering.md#implemented-procedural-fills) implements the first step: a build-time Slang fragment fill inside the existing background shape, a logical identity with typed parameters and explicit host time, Vulkan registration with diagnostics, and a declared color/gradient fallback that CPU always uses. A style's `time_rate` moves a fill with paint's explicit host time without restyle or layout, and `time_dependent` tells a host to request frames; the [Win32 Vulkan menu](../architecture/rendering.md#implemented-win32-vulkan-example-host) animates its aurora backdrop that way, presenting only while shown and pausing its clock when minimized. Typed theme tokens supply a whole fill request. Broader profiles (textures, multipass), per-parameter tokens, recorded host time and an Inspector consumer remain open; the guidance below still applies to them.

Apply a procedural fill to an ordinary panel: for example an animated gradient, noise or holographic surface. Begin with a fragment-like fill inside an existing shape so coverage, layout and interaction remain understandable.

A useful first profile is one element, one pass and typed uniforms without arbitrary texture access. This describes the initial implementation scope, not a permanent ban on texture inputs, multipass materials or other shader stages. Additional capabilities need their own resource, composition and validation semantics.

Candidate resolved data carries a logical shader identity, typed parameters, bounds, clip and opacity. Source loading, compilation, reflection and pipeline caching belong to backend/asset integration; draw lists do not carry compiler services, source code or GPU objects. Declare the shader ABI's local position, element size, optional device scale, host time, uniform layout, color space and output alpha.

Compilation, ABI validation and unsupported-capability failures produce diagnostics. The author/host chooses a declared solid fallback or explicit omission of the failed fill; neither is reported as successful execution of the requested shader. Failure must not corrupt accepted UI state.

Arbitrary Slang execution on CPU is not a prerequisite. Backend capability and fallback declarations distinguish a genuine implementation from a substitute.

### D. Custom SDF and motion

The [sdf-v1 profile](../architecture/rendering.md#implemented-custom-shapes) implements the first step: a build-time Slang `signedDistance(ShapeInput)` replaces the background shape's coverage, reading only the sign so distance estimates are acceptable, within the fill box and its hard or 4x4 sampling, painted with color or gradient and moved by explicit host time. The built-in shape is the declared fallback that CPU always draws, and input keeps the rectangular default. The [Win32 Vulkan menu](../architecture/rendering.md#implemented-win32-vulkan-example-host) outlines its title badge with a moving blob. Procedural paint inside a custom shape, distance-based smoothing, strokes and glow from distance magnitude (which need a declared accuracy), and shape-aware input with separately supplied CPU geometry remain open.

Use the [animation proposal](animation.md) for injected host time, replay evaluation and invalidation. Procedural uniforms can change with time without introducing a general animation framework first. Define easing, interruption and geometry effects for the properties actually exposed.

### E. Layer and offscreen effects

[Offscreen layers](../architecture/rendering.md#implemented-offscreen-layers) implement the first step: `PushLayer`/`PopLayer` paint into an isolated surface of unquantized premultiplied linear color that composes once with an opacity, under the clip in effect when it opened. `composition: layer` wraps a node's subtree, and `StyleTransitions` keeps the layer while opacity fades back. CPU uses float surfaces; Vulkan records each layer's pass into a host-supplied command buffer outside the main pass, into renderer-owned half-float surfaces with completion-safe reuse. The [Inspector](../guides/inspector-hierarchy.md#reorder-the-hierarchy) dims a dragged row as one layer, and the [Win32 Vulkan menu](../architecture/rendering.md#implemented-win32-vulkan-example-host) composes its Preview pill as one.

Masks, shape clips, blend modes, backdrop blur and post effects are valid follow-up directions on those surfaces. Choose them when they deliver useful visuals, defining surfaces, composition order, clip/transform interaction, alpha, resource budgets and completion-safe lifetime.

Backdrop blur reads earlier drawing results; it is not merely another local SDF fill. A render graph or broader material system is available when it simplifies those needs, rather than a prerequisite for gradients or procedural fills.

Static SVG paths and bounded Lottie shape/keyframe playback can be evaluated independently. Format compatibility and complete effect catalogs are not implicit release requirements.

## Shader ownership and execution policy

Logical asset identities and typed frame data cross the core boundary. Backend/host integration owns compiled artifacts and caches, identified by shader revision, entry point, target and ABI. In-flight pipelines, descriptors and referenced images obey the existing completion/`retire` boundary.

Declare the execution profile and trust model. Texture access, storage writes, atomics, loops and multipass work need appropriate validation and resource/work bounds when exposed; successful Slang compilation alone does not establish safety. A host may select trusted assets or a narrower profile. This is not a claim that user-supplied GPU code is sandboxed.

Time-dependent looks request frames through host scheduling. Visibility and actual changes should guide redraw requests rather than adding an unconditional busy loop. Measure batch/pipeline switches, overdraw, draw calls, GPU time and resource memory on representative screens before adding caches or mandatory offscreen passes.

## Representative showcase

Grow one useful screen rather than a separate demo for every effect:

- An Inspector/Hierarchy-style control area demonstrates ordinary interaction.
- A card/dashboard area demonstrates gradient, shadow and border.
- An effect preview demonstrates analytic/SDF shapes and procedural fill as those capabilities land.

Capture a static frame and specified host-time frames; exercise parameter updates, resize and device scale. Make selected fallbacks visible and identifiable when a backend lacks a capability.

An existing application can supply the first card. A dedicated showcase is optional, and neither an Inspector dependency nor completion of the whole showcase blocks an independently useful Look feature. Samples demonstrate usability; contracts and the support matrix remain the sources of specification and implementation evidence.

## Verification and promotion

Follow [verification ownership](../guides/testing.md#look-and-effect-verification): affected contract tests, a small reference/image set and one representative consumer. Test built-in coverage numerically, shader failures explicitly and GPU behavior with actual rendering. Fix time, seed, uniforms, shader/asset identity and capture conditions for reproducible observations.

Use CPU comparisons where a corresponding implementation exists; declare tolerances and explicit fallbacks elsewhere. GPU-only effects do not require arbitrary CPU shader execution or universal pixel identity. Select visual/GPU checks under the existing CI tiers; expand coverage or introduce performance gates from measured regression risk, not an unbounded snapshot catalog.

Promote a slice by its visible result, affected contract and relevant evidence. Gradient, shadow, SDF, Slang and motion may be reordered or combined when that better serves an application. [Backlog](../roadmap/backlog.md#unscheduled--look-and-visual-rendering) tracks candidates; [current work](../roadmap/current.md) changes only when work is selected.

Rendering details belong to [rendering](../architecture/rendering.md), time behavior to [animation](animation.md), implemented capabilities/limits to [support](../reference/support-matrix.md), and execution results to PR/CI. This proposal is not a second implementation-status inventory.
