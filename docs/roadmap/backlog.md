# Backlog

Inactive, unfinished candidates; [current](current.md) owns selected work and [support](../reference/support-matrix.md) owns implementation/validation status. Remove implemented operations from these lists; retain only the unfinished scope of partially delivered candidates. Delivery history belongs in the [changelog](../../CHANGELOG.md). Expansion for v0.8.0 and beyond follows consumer needs, without fixed version ordering or committed dates.

Use the selected Inspector / Hierarchy application to identify the next user-visible blocker. Other concrete consumers and useful visual outcomes can also motivate independent slices; the candidates below are not a prerequisite checklist.

## Near-term editing and development experience

- Clipboard follow-ups: asynchronous results and additional consumers beyond the [Inspector's synchronous name copy/cut/paste](../guides/inspector-hierarchy.md#host-and-observation-boundary).
- Japanese input: basic Windows IME composition/commit through TextInput, starting with a concrete input scenario.
- Observation: make selection, focus, layout and command results easier to trace through existing [inspection](../architecture/inspection.md).
- Failed updates beyond name editing: select a concrete non-text operation that needs recoverable rejection feedback.
- Focused reload/development aids: shorten one edit -> update -> inspect round trip when manual repetition becomes a bottleneck.

Select these as individual application slices, not a combined release gate. Broader tooling remains below.

## Validation and CI follow-ups

The [PR Core / main Integration split](../guides/development.md#ci-scopes-and-reproducible-measurements) remains the baseline.

- Further Diagnostics fixture reduction or finer test selection if [measurements](../guides/development.md#focused-checks-and-timing) justify the maintenance cost.
- Independent consumer concurrency after verifying build-directory, cache and resource isolation.
- Required-check changes after validating replacement coverage under the [testing policy](../guides/testing.md#measurement-and-safe-reduction).
- Hosted timing and capacity/cost observations for the current core/integration split.

## Widget candidates beyond the reference application

Label, RadioButton/RadioGroup, Spacer/Separator, Toolbar, Tabs and InspectorRow, selected from application use. See [authoring](../proposals/cpp-authoring.md).

## Layout follow-ups

- Absolute positioning, percentage sizing, aspect ratio and responsive rules.
- Grid child cell selection, spanning, implicit tracks or advanced allocation.
- Additional intrinsic sizing, Flex wrapping, align-self and baseline alignment.
- Anchor placement/nesting extensions for Dropdown or nested menus.
- Docking and virtualized layouts for larger workspaces.

Contracts: [layout](../architecture/layout.md), [input](../architecture/input.md); mechanisms: [overlays](../proposals/overlays-and-portals.md), [virtualization](../proposals/virtualization.md).

## Application UI follow-ups

- Undo/redo, multiline and broader composition support.
- Submenus, menu bars, richer menu items, additional dialogs/Inspector controls, multiple selection and extra focus scopes.
- Shared Portal behavior or nested/stacked modal lifetimes when an overlay operation needs them.
- A practical DevTools/inspection UI.

Consumers: [Settings](../integrations/settings-app.md), [Path-finder](../integrations/path-finder.md).

## Unscheduled — Authoring and Tooling

Make `edit -> reload -> inspect -> interact -> capture -> verify` a practical workflow: source-aware hierarchy/property/style editing, source navigation, actionable reload diagnostics, capture/comparison and a component browser where useful.

Select one [Path-finder](../integrations/path-finder.md) operation. Preserve accepted state on invalid edits and reproduce its observations through ordinary [inspection](../architecture/inspection.md), [reload](../proposals/live-reload.md) and [replay](../architecture/replay.md).

## Unscheduled — Language Bindings and Stable Host Boundary

Follow the application/host work with an independent scripting/tooling consumer when one needs bindings. Evaluate Python, then Rust/Go as needed; define handles, ownership and errors, with a C ABI facade only where useful. Bind the high-level authoring/host model rather than mirroring internals.

Use the common [UI model](../architecture/ui-model.md); compatibility commitments follow downstream evidence.

## Later — Broader Application Model

Consumer-driven application shells, [command palette](../proposals/command-palette.md), docking, TreeTable/DataGrid and editable/resizable/sortable columns; larger collections and typed drag/drop. Add resources, async patterns, context, localization, accessibility or extension boundaries as the application needs them.

See [UI model](../architecture/ui-model.md), [layout](../architecture/layout.md) and [commands](../architecture/commands.md).

## Later — Performance and Scale

Select an identified bottleneck from representative application traces:

- [Incremental evaluation](../proposals/incremental-evaluation.md), stage dirtiness, text caches, virtualization and paint reuse.
- Retained display items, partial uploads, batching and allocation reduction.
- Bounded reactive observations and update-cause traces.
- Host-clock animation, priority queues, visibility-aware work or parallel preparation where measurements justify them.

Compare against full-tree/scalar references and report workload, conditions and timing noise under [testing](../guides/testing.md#image-regression-and-performance).

## Unscheduled editor extensions

- [Lifecycle conveniences](../proposals/automatic-component-lifecycle.md), [contained UI fallback](../proposals/error-boundaries.md), [owned async resources](../proposals/async-resources.md) and provider integration.
- Broader reload migration, property reflection and declaration/default origins.
- Portal/palette operations and large keyed [virtual lists](../proposals/virtualization.md).
- Control+Z/Control+Shift+Z for ordinary label-history commands, preserving editing-first routing.
- Multiline, clipboard/composition and host shortcut adapters.
- [Snapshot bundles/CLI](../proposals/snapshot-bundles.md), [agent discovery](../proposals/agent-capability-manifest.md) and edit-to-verification measurements.

Use concrete friction in the [editor fixture](../integrations/path-finder.md) to choose scope, rather than treating all extensions as required.

## Unscheduled accessibility, automation, and runtime adapters

Native accessibility, external semantic invocation, recording tools, resource/device-loss diagnostics and install/export packaging. [Generation history](../proposals/generation-history.md), bounded JSON-RPC, subscriptions or MCP follow a concrete automation consumer.

Adapters share ordinary [semantics](../architecture/semantics.md), [commands](../architecture/commands.md) and [inspection](../architecture/inspection.md); validate their own platform/lifetime boundary.

## Later — WebGPU, WASM, and world-space UI

Evaluate Web hosting, WebGPU rendering and browser text/input separately: WASM/JS bridge, semantic DOM/focus, Japanese IME/clipboard, then workers/OffscreenCanvas if needed. Use the common runtime and platform-specific parity fixtures under [Web host](../proposals/web-host.md).

World-space adapters, streamed resources and additional native backends follow consumer requirements.

## Later — Localization and international layout

Locale/message identity, typed arguments and provider integration; versioned catalogs, fallback, runtime switching and atomic replacement; plural/select and locale formatting; direction-aware layout/text/icons; inspection and reproducible locale transitions.

See [localization](../architecture/localization.md), [dependency choices](../reference/dependencies.md#proposed-localization-choices) and [fixture selection](../guides/testing.md#localization-fixtures). A concrete localization library stays outside core.

## Later — Typography expansion

Real-font caret/selection geometry and authored font aliases; read-only rich spans, MSDF/variable fonts; Unicode line breaking, script segmentation/BiDi, emoji and advanced OpenType; font metadata/licensing hooks.

See [text](../architecture/text.md), [localization](#later--localization-and-international-layout) and [font licensing](../reference/dependencies.md#font-licensing).

## Unscheduled — Look and visual rendering

Improve ordinary screens and enable distinctive GPU visuals through the [Look proposal](../proposals/look.md):

- Visual basics: radial gradients and useful shadow/stroke/text-decoration improvements.
- Built-in analytic/SDF shapes and coverage; Slang procedural fills with explicit parameters and capabilities.
- Custom SDF and host-clock motion; masks, blur and offscreen composition when a visual outcome needs them.
- A representative screen that grows with the features rather than a demo or snapshot catalog per effect.

This is a suggested progression, not fixed ordering or permanent shader/API limits. Promote useful slices without requiring the whole direction or expanding the v0.7.0 completion gate. Contracts: [rendering](../architecture/rendering.md), [animation](../proposals/animation.md); checks: [Look verification](../guides/testing.md#look-and-effect-verification).

## Later — Analytic primitives and CPU compatibility performance

Shared analytic coverage follows the [Look direction](#unscheduled--look-and-visual-rendering). CPU tiles, damage-based repaint, workers/SIMD and host-selected compatibility fallback follow measured workloads. Compare optimized output with the [scalar reference](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) before measuring performance.

## Later — Graph canvas and node editor

[Graph-editor](../integrations/graph-editor.md) candidates: pan/zoom canvas; nodes/ports/connections and hit testing; selection/drag/history/clipboard; spatial indexing, culling/LOD; subgraphs, layout providers, profiling and deterministic agent operations. Promote stages from a domain application's requirements.

## Later — Conversational and agent workspace

[Conversational workspace](../integrations/conversational-ui.md) candidates: message/composer/streaming foundation; Markdown/code/attachments/tool results; branching/persistence; approvals, terminal/files/artifacts and docking; interactive blocks and collaboration. Select representative long/streaming/tool-heavy workloads for validation.

## Unscheduled extensions

Timeline/plotting, collaboration/presence, [animation](../proposals/animation.md), additional language bindings, optional DSL/JSX frontends, richer paint/path composition and broader editor transports remain consumer-driven candidates.

## Explicitly deferred

See [non-goals](../product/non-goals.md) for product exclusions. No candidate above commits to a release date or makes its full scope a prerequisite for ordinary feature work.
