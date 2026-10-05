# Web host and browser integration

## Proposed shared runtime

Native and Web applications should share the same component model, backend-neutral document/IR, layout, style, semantic projection, and action rules. Web is an intended application host with WASM runtime and WebGPU canvas rendering. This is architectural direction, not a selected toolchain, supported browser configuration, or delivery commitment; [support](../reference/support-matrix.md) owns those claims.

The intended workload is dense application UI: inspectors, timelines, node graphs, CAD/DCC tools, dashboards, and UI composed with a 3D viewport. Content sites, SEO, HTML/CSS compatibility, and a React-compatible API are outside the target. Host-owned viewport resources can compose with UI through [rendering boundaries](rendering.md); the world scene remains external to the UI tree under [architecture](architecture.md#ecosystem-ownership).

```text
Authoring frontends / C++ construction -> common UI model -> WASM runtime
                                                           /        \
                                                   UiDrawList    SemanticTree
                                                       |              |
                                                 WebGPU canvas   semantic DOM adapter
                                                                      |
                                                           accessibility / focus
Browser input / editing adapter -> normalized input and declared actions
```

The WebGPU backend consumes resolved draw data. The browser host owns the canvas, device/queue, input, frame scheduling, external assets, and lifecycle. SDK/DOM types stay outside core. WASM packaging, native WebGPU, and browser WebGPU are separate validation targets.

## Proposed semantic DOM bridge

DOM supplies browser accessibility and integration alongside GPU visuals. It projects the common [SemanticTree](semantics.md), not a second component model or visual renderer. Map roles/names/state/actions and semantic order from the settled generation. Bounds and visibility derive from shared logical geometry; the bridge must account for canvas placement and device scale.

Synchronize browser focus with Tessera focus through [input](input.md#focus-and-navigation). Browser and runtime changes must not produce duplicate activation, recursive focus updates, or stale targets after reload. Removal, disabled/hidden content, modal scopes, and virtualized items require defined projection and focus recovery. Screen-reader behavior and browser navigation each need adapter evidence; creating DOM nodes alone proves neither.

## Proposed text editing adapter

A TextInput may use a browser input/textarea or an evaluated EditContext adapter for platform editing while Tessera paints its visuals. The authoring model exposes the shared editing contract, not a browser-specific widget. Choice, capability detection, and fallback require browser evidence; no particular API is assumed universally available.

The adapter maps committed text, composition, replacement ranges, selection, and caret geometry through the [editing boundary](input.md#proposed-editing-ime-and-clipboard-boundary). Translate browser index units explicitly into the runtime's declared units. Synchronize Japanese IME, focus loss/cancellation, clipboard, asynchronous paste, and readonly/disabled behavior against a document revision. Deliver each edit once, even when keyboard, composition, and input events overlap.

Mobile keyboard, autofill, and password-manager integration are separate requirements to evaluate with a consumer. Password/private field values must not leak into semantic inspection, state exports, or snapshots. A semantic DOM projection does not by itself provide working text input.

## Proposed JavaScript host bridge

A thin JS bridge may load a validated document, supply host-declared state and assets, normalize input, advance updates, and receive action requests after dispatch. JS does not maintain a second virtual DOM or reconciliation engine. Application logic and browser services remain host-owned; the core needs no JavaScript VM.

Specify ownership/copying across WASM memory, typed input validation, state-schema versions, asynchronous completion/cancellation, disposal, and stale action rejection before freezing an API. Network fetch, filesystem access, and clipboard operations are explicit host services. Authoring frontend choice does not change this boundary.

## Proposed worker split

Begin with a correct single-thread host. A Worker/OffscreenCanvas arrangement is a later measured option: browser input, semantic DOM, editing and accessibility remain on the main thread, while runtime/layout/paint/render work may move to a worker.

Before moving work, define ordered messages, generation-tagged geometry/semantics, coherent viewport/scale changes, input-to-frame synchronization, transferred-resource ownership, backpressure, and shutdown. A worker must not create competing clocks or action paths. Feature availability and main-thread fallback need separate evidence; this design does not promise a multithreaded core API.

## Verification

Replay the same controlled fixtures through native and Web hosts. Compare document/state semantics, geometry, supported actions, paint output, and selective GPU images under declared tolerances. Test fractional scale, resize, focus round trips, Japanese composition, semantic DOM cleanup, invalid bridge input, teardown, and device/resource failure. Record fonts, toolchain, browser/version, backend/device, and threading mode in support.

Measure application workloads and edit-to-snapshot latency through [inspection tooling](inspection.md#proposed-diagnostics-and-comparisons). Large/high-frequency UI and 60/120 Hz consumers motivate budgets; visual-element counts and DOM-framework speedup ratios are not support claims or universal targets. Scheduling and parity exit criteria belong in [backlog](../roadmap/backlog.md#later--webgpu-wasm-and-world-space-ui).
