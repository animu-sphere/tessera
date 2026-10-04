# Support matrix

Checked: 2026-10-05. **No runtime capability or platform configuration is implemented or validated.** The repository contains design and planning documents only.

## Evidence vocabulary

| Status | Meaning |
| --- | --- |
| Planned | Design/roadmap direction; no implemented capability |
| Implemented | Identifiable code exists; validation scope must be stated separately |
| Validated | A recorded configuration and reproducible check establish the specific claim |
| Unsupported | An implementation explicitly rejects or excludes a declared configuration |

Missing implementation or test evidence is not evidence that a platform fails. Use planned/unvalidated rather than inventing support results.

## Capability status

| Area | Current status | Intended evidence before a support claim |
| --- | --- | --- |
| UI tree, properties, serialization | Planned | Round trip, validation failures, deterministic representation |
| Fixed/stack/flex layout | Planned | Numeric geometry fixtures without GPU |
| Paint list and Vulkan primitives | Planned | Command validation, shader build, runtime images |
| Pointer input and hit testing | Planned | Synthetic events plus interactive host smoke |
| Focus, keyboard/gamepad navigation | Planned | Deterministic navigation and recovery fixtures plus menu smoke |
| Text shaping and Latin/Japanese fallback | Planned | Declared fonts, metrics/wrapping fixtures, glyph images |
| Stylesheets, selectors, pseudo states | Planned | Cascade, inheritance, state invalidation fixtures |
| Component state/reconciliation | Planned | Key identity, update and cleanup evidence |
| Image assets, themes, live reload | Planned | Resource lifetime, theme validation, valid/invalid reload cases |
| Path-finder integration | Planned | Shared-document authoring/preview round trip |
| WebGPU | Planned | Toolchain/artifact checks and matching runtime fixtures |
| Accessibility metadata/adapters | Planned | Metadata schema tests; separate native-adapter evidence |
| Animation and advanced custom paint | Planned | Resolved-value/time/invalidation and draw-list boundary checks |

## Configuration status

| Configuration | Current evidence |
| --- | --- |
| Core-only build | No CMake configuration or executable tests yet |
| Native Vulkan | No adopted SDK/toolchain, shader artifacts, or GPU runtime tests |
| WebGPU native | No selected implementation or toolchain |
| Browser / WASM | Feasibility candidate only; no build or browser tests |
| Metal / Direct3D 12 | Possible later targets; no scheduled implementation |
| Windows / Linux / macOS | No validated OS/compiler/build combinations |

The local Windows documentation workspace is not platform support evidence.

## Recording future evidence

Each claim needs a revision, date, OS/compiler/build configuration, dependency versions, command or procedure, fixture, result/artifact location, and known limitations. GPU claims also need device/driver/backend/target-format information. Separate shader compilation, headless rendering, native-window behavior, installation/consumer checks, and performance measurements.

Update only the rows proven by the delivered work. Milestone labels alone do not establish capability. Active work is in [current](../roadmap/current.md); planned scope is in [backlog](../roadmap/backlog.md).
