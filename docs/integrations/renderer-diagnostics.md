# Renderer diagnostics UI

The Proposed sections define a consumer workload: express an interactive renderer diagnostics panel through Tessera's ordinary C++ authoring, commands, input, inspection and rendering boundaries. [Current work](../roadmap/current.md) owns scheduled operations and acceptance; this design does not activate its entire inventory. Implementation and validation evidence belongs only in [support](../reference/support-matrix.md).

## Proposed consumer workflow

Use the hydra-toon viewport's Telemetry overlay as a concrete authoring reference. A developer composes named timing rows, counters, a bounded frame-history plot, disclosure sections, choices, toggles and numeric controls without importing ImGui or assembling component/runtime internals in ordinary panel code. Matching information and operations matters more than reproducing ImGui's appearance or API.

The reference separates frame observations from requested controls in `OverlayFrame` and `OverlayControls`. Preserve that separation with application-owned snapshots and ordinary command requests; those external structs are source references, not new Tessera public types. The source-review provenance and inventory are recorded in [support](../reference/support-matrix.md#renderer-diagnostics-source-review--2026-10-17).

| Consumer group | UI expression | Host-owned information or operation |
| --- | --- | --- |
| Frame summary | Scene/device, extent, presentation mode, frame time/FPS/p99 and bounded history plot | Frame identity, sampling window, units and history samples |
| CPU/GPU/latency | Repeated labeled rows with mean/p95/max/stddev; unavailable states and explanatory text | Measurements, summaries, timestamp availability and latency meaning |
| Draws/uploads/session | Counter rows and small aligned tables; collapsible groups | Draw/triangle/pipeline counts, per-frame/session transfers and validation observations |
| Render controls | MSAA choice, outlines/self-shadow toggles, material-view choice, bounded scalar/vector controls and reset | Supported values, accepted settings and renderer-side execution |
| Animation | Play/pause, start/step, time, speed and loop with explicit eligibility | Time range, playback clock and deterministic-capture policy |
| Materials/morphs | Bounded repeated object groups, numeric values, override state and release actions | Stable object IDs, evaluated values, edits and override lifetime |
| Skeleton/avatar diagnostics | Repeated diagnostic rows, selection, errors and optional scene visualization | Joint/runtime identities, transforms, bindings and projected scene marks |

This mapping is a consumer inventory, not a requirement to implement a generic Table, TreeView, Combo, Plot, docking system or every editor. Small tables and expanded sections can be composed from ordinary Rows, Columns, Text and Buttons. Add the minimum reusable control only when a promoted operation demonstrates why existing composition cannot express it. Domain-specific editors and scene visualization need their own bounded operation before core work.

## Proposed data and command boundary

- The host owns renderer telemetry, sampling/aggregation, bounded history storage, scene data, accepted settings and frame scheduling. Capture immutable owned display data at an update point; borrowed renderer records must be copied or remain alive through synchronous preparation. The panel does not query a GPU or renderer while layout, paint or semantic projection traverses it.
- Pass backend-neutral values with stable row/object names, units, availability and sample identity. An unsupported GPU timer or an empty sample series displays an explicit unavailable marker, distinct from a measured zero. Do not label input-to-submit/presentation API measurements as measured display latency.
- Host observations may update at a readable cadence independently of renderer sampling. Publish timing rows, counters, history and settings from one accepted snapshot; retain disclosure, selection, focus and scroll state by stable identity across refreshes. A telemetry refresh must not execute render-setting commands.
- Commands request setting changes after dispatch. The host validates finite numbers, ranges, supported sample counts/enums, object identity and live eligibility before proposing accepted settings. Numeric editing must preserve the last accepted value on invalid input. Presentation failure retains the accepted panel/settings and produces actionable diagnostics; it does not undo an external GPU operation. A fallible or asynchronous renderer change needs an explicit host acknowledgement/error policy before integration.
- Pointer, keyboard/gamepad and semantic operations use the same commands and eligibility. Normalize platform input at the host boundary and arbitrate panel versus camera input there. Consumed panel input must not also operate the camera. Native capture, file dialogs and device polling remain host adapters.
- Use the ordinary [full-tree lifecycle](../architecture/overview.md#update-and-snapshot-rules), [authoring contract](../architecture/ui-model.md#implemented-c-form-authoring), [input](../architecture/input.md), [commands](../architecture/commands.md), [layout](../architecture/layout.md), [styling](../architecture/styling.md) and [rendering](../architecture/rendering.md). Core APIs and serialized UI definitions contain no Hydra, OpenUSD, Vulkan or ImGui types.

## Proposed bounded reference panel

Separate reusable panel composition and a small host-facing data/command interface from deterministic fixture machinery. An independent core-only consumer must be able to copy or import that composition, inject snapshots and observe requested operations without a live renderer, window or optional SDK. Exact public syntax is an implementation choice constrained by ordinary authoring and ownership.

Begin with a frame summary, CPU/GPU timing rows, a frame-history plot, draw/upload/session counters and collapsible sections in a constrained viewport. Keep history bounded and pair the chart with inspectable textual summary and sample identities; do not require a new scene renderer or general charting framework. Host offsets and focus reveal follow the same [scrolling boundary](settings-app.md#implemented-constrained-scrolling-boundary) as Settings.

Use representative controls to prove editing rather than only rendering a static mockup: samples 1/2/4/8 subject to host capabilities, outlines, scene-light selection, direct/ambient strength in [0,4], a key-direction vector with components in [-1,1], material-view choice and reset. Numeric operations need direct value entry and pointer/keyboard/gamepad adjustment; their minimal composition/control design must declare step, bounds, commit and cancellation rules before implementation. Enum choices may use repeated Buttons instead of requiring a popup mechanism.

Changing a control proposes a host request and displays the accepted result. External refreshes can change telemetry while the user expands a section or edits a value; retain the relevant interaction state and keep draft numeric text separate from accepted renderer settings. The synthetic host must expose request counts and accepted settings so read-only refresh, rejection and once-only operation are observable.

Playback, object override editors, skeleton/avatar rows and scene-space marks describe further use of this boundary. A representative panel is sufficient before porting the complete hydra-toon overlay. The host's scene renderer retains scene drawing; Tessera paints the panel through its own renderer contract. Translating ImGui draw lists or hydra-toon's overlay triangle format is not a substitute for Tessera-authored semantics and input.

## Verification requirements

Declare initial snapshots, stable identities, available capabilities, bounded sample sequence, normalized inputs, clock, viewport/scale, shaper/fonts and capture backend under the [testing guide](../guides/testing.md). Verify:

- Text/inspection agrees with the supplied statistics, units, counter totals and unavailable/zero distinctions. Refreshing telemetry preserves focused control, disclosure/selection and valid scroll offsets without issuing setting commands.
- Expanded rows and the history plot agree with one accepted sample sequence. Small aligned tables preserve readable labels and values; clipping and scrolling agree across paint, hit testing and inspection. Keyboard/gamepad navigation reaches and reveals lower controls.
- Representative choices, toggles, numeric entry/adjustment and reset reach the same host operation through pointer, keyboard/gamepad and semantic routes. Invalid/non-finite/out-of-range values, unsupported choices, disabled owners and stale requests reject without changing accepted settings.
- Returned presentation errors retain accepted observations and host settings. Pending numeric input has a declared retention/cancellation policy during refresh and section collapse. Input consumed by the panel produces no camera request in the synthetic host arbitration check.
- Repeated accepted-view replay, semantic/layout/paint comparisons and selective CPU captures reproduce under declared inputs. State clearly whether replay executes the command host or reconstructs accepted replacement views; synthetic timings are fixture data, not performance measurements.
- Independent source consumption and affected checks run in Debug and Release. Native renderer/GPU/window integration needs its own evidence, including device/resource ownership and completion-safe submission; the core fixture does not establish hydra-toon migration or live GPU measurements.

Stop subsystem expansion when the promoted panel operations, coherent refresh, input and observation boundaries pass. Full ImGui feature parity, native window management, generalized tables/plots/virtualization, scene editing and renderer performance work are separate consumer decisions.
