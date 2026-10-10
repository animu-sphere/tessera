# Current

Inactive candidates and deferred scope are owned by [backlog](backlog.md).

## v0.3.0 — Dynamic UI and editor prototype

Objective: support application-driven composition and Path-finder iteration using the same document/schema, and make dynamic UI safe to observe and verify, not only able to change.

Depends on menu/text/style foundations and explicit document identity/versioning.

Consumer fixture: an inventory/tool panel driven by host-owned selection and item data. Inspection starts as a non-editing command; item edits will add discrete and continuous transactions, keyed rows, async resource replacement, compatible reload, and Inspector metadata. Use the same command eligibility and generation checks for pointer, normalized keyboard/gamepad, semantic, and replay requests before extending the fixture.

Remaining work:

- Props/local state/bindings and conditional children over the [reactive runtime](../design/reactive-runtime.md) and its [fault boundaries](../design/reactive-runtime.md#implemented-subtree-fault-boundary); [keyed reconciliation](../design/ui-model.md#state-and-reconciliation), cleanup and completion-safe resource retirement. Use full-tree style/layout/semantic/paint evaluation as the presentation reference, with affected-stage metadata for later incremental work.
- Extend the [command boundary](../design/commands.md#implemented-in-process-command-boundary) with shortcut scopes/conflict diagnostics and host execution/result adapters; integrate the [transaction boundary](../design/commands.md#proposed-transaction-boundary) and build a [command palette](../design/commands.md#proposed-command-palette) from ordinary components.
- [Error boundaries](../design/ui-model.md#proposed-error-boundaries) and [async state](../design/ui-model.md#proposed-async-state) primitives.
- Image assets and theme variables.
- Validate transform agreement between native pointer mapping and rendering when a consumer uses transformed menu paint or hit testing, under [layout coordinates](../design/layout.md#coordinate-spaces) and [rendering conversion](../design/rendering.md#coordinate-conversion).
- Source-aware diagnostics, atomic live reload, compatible-state preservation, Path-finder preview.
- Full property reflection/introspection v1 and a schema-driven Inspector.
- OverlayRoot/Portal and bounded Tooltip/Dropdown/Modal primitives.
- TextInput contract and VirtualList prototype based on stable item keys.
- Scalar [CPU reference backend](../design/rendering.md#proposed-cpu-reference-backend) for the existing draw-list vocabulary and grayscale glyphs, with deterministic window- and GPU-free capture; Vulkan fixtures compared with it under declared tolerances.
- A small inspection/render CLI consumer and versioned snapshot bundle prototype; semantic/layout/counter comparisons and selective visual regression artifacts across declared viewport fixtures, captured with the CPU reference backend by default.
- Reload-to-snapshot linkage and source-edit-to-verification instrumentation shared with Path-finder.
- [UI invariant](../design/inspection.md#proposed-ui-invariants) prototype and [agent capability manifest](../design/inspection.md#proposed-agent-capability-manifest) v0.
- Source consumption by a downstream CMake project through `add_subdirectory`/`FetchContent` with the `tessera::core` and optional `tessera::vulkan` targets, checked by a minimal consumer, as Path-finder's integration path. Installed/exported packages remain v0.5.0 scope.

Exit criteria:

- Core-only chain/diamond and dynamic-branch fixtures settle coherently; equal derived outputs suppress unnecessary downstream evaluation, batched writes coalesce scheduled work, and disposed owners produce no later evaluations/effects. Cycles, reentrant flush, and failed calculations report located diagnostics while preserving valid publication/bookkeeping.
- A successful event batch publishes at most one visible generation; explicit reads during the batch do not deliver partial effects. A continuous edit can span batches with one application transaction, and a non-editing action opens no edit token.
- An inventory/tool panel updates without corrupting identity, focus, bindings, or cleanup.
- Missing/replaced assets and theme variables follow declared fallback/diagnostic rules.
- Invalid edits keep the last valid UI; compatible edits preserve declared state and retire old resources.
- Runtime and Path-finder round-trip the same document, including editor metadata; a minimal downstream project builds and links Tessera from source.
- Inspector properties derive from runtime metadata; portal focus/ownership and list item identity remain coherent.
- Tooling uses host-declared state slots, captures one generation, and emits machine-readable observations/differences; invalid bundle versions or incompatible comparison conditions are diagnosed.
- The CPU reference backend renders the primitive/glyph fixtures identically across runs without a window or GPU, and both Vulkan paths agree with it within declared tolerances.
- Menu, shortcut, palette, and semantic invocations of one command share eligibility; editing gestures produce one application transaction and non-editing requests produce none. A failing subtree or reload keeps the previous valid generation; invariants report located results for one captured generation.

Owners: [reactive runtime](../design/reactive-runtime.md), [UI model](../design/ui-model.md), [commands](../design/commands.md), [rendering](../design/rendering.md), [styling](../design/styling.md), [input](../design/input.md), [layout](../design/layout.md), [Path-finder](../design/path-finder-integration.md), [inspection](../design/inspection.md).

Live capability and validation evidence belongs to the [support matrix](../reference/support-matrix.md); delivery history belongs to the [changelog](../../CHANGELOG.md).
