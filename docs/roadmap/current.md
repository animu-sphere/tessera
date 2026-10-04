# Current

Updated: 2026-10-05. Phase 0 is active. Its tree/document/serialization slice is implemented and verified; the remaining subsystem contracts must land before moving to Phase 1. Delivery history is in the [changelog](../../CHANGELOG.md), with configuration evidence in the [support matrix](../reference/support-matrix.md).

## Documentation foundation

Objective: turn the implementation strategy into a compact set of canonical subsystem designs and reviewable milestones.

Delivered:

- [x] Root README and documentation index.
- [x] Design pages for architecture, UI model, layout, styling, rendering, text, input, and Path-finder integration.
- [x] Development/testing guides and dependency/support references.
- [x] Separate current work, backlog, and phase/milestone overview.
- [x] Contributor and agent guidance consistent with the strategy.

These items describe documentation delivery, not implementation or platform validation.

## Phase 0 — Runtime foundation

Objective: **create a tree -> inspect it -> validate it -> serialize it**, without a renderer or platform SDK.

### Work

- [x] Select C++20, CMake 3.20 minimum, feature-based compiler requirements, and `tessera::core`; see [development](../guides/development.md).
- [x] Introduce the UI include/source, serialization tests, format, and example boundaries from [architecture](../design/architecture.md); unused subsystem directories remain deferred.
- [x] Define Box/Text node kinds, ownership, runtime handles, author IDs, and ordered child storage.
- [x] Define `UiDocument`, typed properties, format version, and validation diagnostics.
- [x] Select JSON v1 and implement deterministic load/save behavior.
- [ ] Define resolved-style and `LayoutBox` inputs/outputs needed by the next phase.
- [ ] Define normalized base event types; action-name validation exists, but registration/dispatch and callback lifetime contracts remain to define.
- [ ] Define a backend-neutral draw-list/frame contract with resource/submission lifetime rules; concrete GPU execution remains later work.
- [ ] Define the text measurement/shaping boundary, without requiring real font libraries yet.
- [x] Add one small tree/serialization smoke and focused failure/round-trip checks.
- [x] Document verified build/test commands and the Windows x64/MSVC configuration.

### Exit criteria

- A clean core-only configuration builds and runs without Vulkan/WebGPU, Slang, FreeType/HarfBuzz, OpenUSD, browser, or editor SDKs.
- One ordered tree can be built in code, inspected, serialized, loaded, and compared for semantic equivalence.
- Invalid property types, duplicate IDs, invalid references, and unsupported versions yield actionable diagnostics.
- Repeated serialization is deterministic under the declared formatting/ordering policy.
- Core public contracts expose no concrete GPU/platform/font implementation types.
- Node/resource lifetimes and selected format/version rules are recorded in their owning design pages.

Evidence must name the revision, configuration, checks, and limitations in the [support matrix](../reference/support-matrix.md). A conceptual backend interface alone does not complete a backend.

### Scope limits

Do not implement the full stylesheet engine, all candidate primitives, reactive component runtime, real text stack, editor UI, or GPU optimization in this phase. The focus is the stable representation beneath them.

## Immediately next — Phase 1 layout prototype

After the foundation exit criteria pass, implement fixed-size Box/placeholder Text and stack/flex rows/columns with margin, padding, and gap. Use numeric geometry fixtures to prove deterministic output without any renderer. Follow [layout](../design/layout.md).

Vulkan and public milestone candidates remain in the [backlog](backlog.md) until this foundation is established.
