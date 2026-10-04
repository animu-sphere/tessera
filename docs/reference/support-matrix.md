# Support matrix

Checked: 2026-10-05. The Phase 0 tree/document/serialization slice and subsystem boundary contracts are validated on the Windows x64/MSVC Debug configuration below. Other runtime capabilities remain planned.

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
| UI tree, properties, serialization | Validated for Box/Text foundation only | CTest round trip, validation failures, deterministic representation, metadata and handle lifetime |
| Fixed/stack/flex layout | `LayoutInput`/`LayoutBox`/resolved-style contract validation implemented; algorithms planned | Numeric geometry fixtures without GPU |
| Paint list and Vulkan primitives | Draw-list and frame-info validation implemented; paint generation and backends planned | Command validation, shader build, runtime images |
| Pointer input and hit testing | Normalized event validation implemented; hit testing/dispatch planned | Synthetic events plus interactive host smoke |
| Focus, keyboard/gamepad navigation | Planned | Deterministic navigation and recovery fixtures plus menu smoke |
| Text shaping and Latin/Japanese fallback | Measurement/shaping boundary and deterministic placeholder shaper implemented; real shaping planned | Declared fonts, metrics/wrapping fixtures, glyph images |
| Stylesheets, selectors, pseudo states | Planned | Cascade, inheritance, state invalidation fixtures |
| Component state/reconciliation | Planned | Key identity, update and cleanup evidence |
| Image assets, themes, live reload | Planned | Resource lifetime, theme validation, valid/invalid reload cases |
| Path-finder integration | Planned | Shared-document authoring/preview round trip |
| WebGPU | Planned | Toolchain/artifact checks and matching runtime fixtures |
| Accessibility metadata/adapters | `labelled_by` relationship storage/reference validation implemented; adapters planned | Existence validation only; no role/name/state schema or native integration |
| Animation and advanced custom paint | Planned | Resolved-value/time/invalidation and draw-list boundary checks |

## Configuration status

| Configuration | Current evidence |
| --- | --- |
| Core-only build | Windows x64/MSVC Debug library-only build plus document/contract tests and example verified; no external runtime dependencies |
| Native Vulkan | No adopted SDK/toolchain, shader artifacts, or GPU runtime tests |
| WebGPU native | No selected implementation or toolchain |
| Browser / WASM | Feasibility candidate only; no build or browser tests |
| Metal / Direct3D 12 | Possible later targets; no scheduled implementation |
| Windows / Linux / macOS | Windows x64/MSVC Debug foundation verified below; Linux/macOS and other Windows toolchains unvalidated |

This is core-only evidence, not native-window, input-device, graphics, text, packaging, or broad Windows support evidence.

## Foundation evidence — 2026-10-05

- Revision: uncommitted implementation on base `547b6d540be6f194e11c6380ec30889f2fc91898`. Identifiable source: `CMakeLists.txt`, `include/tessera/ui`, `src/ui`, `examples/hello-ui`, and `tests/serialization` including the canonical fixture.
- Source fingerprint: SHA-256 `538225599ba7ff4aeb6a3ae8123ab53a3b9a532cf203dd09a141a6c10a53d44a`. Computed by sorting those relative file paths, emitting `path-with-forward-slashes + space + lowercase-file-SHA256`, joining records with LF and no terminal LF, then hashing that UTF-8 manifest. This identifies the tested working-tree code independently of uncommitted documentation edits.
- OS: Windows NT `10.0.26200.0`, x64. Visual Studio Community 2026 `18.9.12112.369`, MSVC `19.51.36256.0` (toolset directory `14.51.36231`), MSBuild `18.9.1+a81b43525`, Windows SDK `10.0.26100.0`.
- Build tools/configuration: CMake/CTest `4.4.3`, `Visual Studio 18 2026`, `-A x64`, Debug. Default static core, standard library only. The declared CMake 3.20 floor and other compilers/configurations are unvalidated.
- Procedure: exact configure/build/CTest/example and clean library-only commands are in [development](../guides/development.md). The library-only build was configured in a fresh `build-core` directory with tests/examples disabled.
- Result: both CTest targets (`document`, `hello_ui`) passed. Direct example execution printed canonical JSON and three ordered restored nodes. The library-only target built successfully. No Vulkan/WebGPU, Slang, FreeType/HarfBuzz, OpenUSD, browser, or editor dependency was configured or fetched.
- Fixtures/artifacts: [menu.json](../../tests/serialization/fixtures/menu.json), [document checks](../../tests/serialization/document_tests.cpp), `build/Testing/Temporary/LastTest.log`, `build/Debug/*`, and `build-core/Debug/tessera_core.lib`. Build artifacts/logs are local ignored files; the source paths reproduce the checks.
- Limits: only Box/Text, minimal semantic properties, action-name validation, and immutable snapshots. No geometry, focus/dispatch, real text metrics, paint/frame contract, GPU execution, live reload, or package installation/consumer test. Codex required approved sandbox escalation for MSBuild's ordinary SDK configuration access; tests ran without escalation.

## Subsystem contract evidence — 2026-10-05

- Revision: uncommitted implementation on base `42d67cdeb4e35a749a35e37f51e9daf0f83657d5`. Identifiable source adds `include/tessera/{layout,style,input,text,render}`, `src/{detail,layout,style,input,text,render}`, `tests/check.hpp`, and `tests/{layout,text,input,render}` to the foundation paths above.
- Source fingerprint: SHA-256 `b76d159f6f9bfdb186dcd93a7408bc8efeb547f858818c45d4b321a8dbcce527`, computed with the foundation procedure over `CMakeLists.txt`, `include`, `src`, `examples`, and `tests`.
- Configuration: the same OS, Visual Studio/MSVC `19.51.36256.0`, CMake/CTest `4.4.3`, `Visual Studio 18 2026`, `-A x64`, Debug configuration as the foundation evidence.
- Procedure: the [development](../guides/development.md) configure/build/CTest commands, plus a fresh `build-core` library-only configure and build.
- Result: all six CTest targets (`document`, `layout_contract`, `text`, `event`, `draw_list`, `hello_ui`) passed. The library-only build succeeded without warnings under `/W4`. No external dependency was configured or fetched.
- Limits: these are type, lifetime, and validation contracts. No layout algorithm, style resolution, hit testing or dispatch, paint generation, real font shaping, or backend execution exists. Placeholder text metrics are not representative of real fonts.

## Recording future evidence

Each claim needs a revision, date, OS/compiler/build configuration, dependency versions, command or procedure, fixture, result/artifact location, and known limitations. GPU claims also need device/driver/backend/target-format information. Separate shader compilation, headless rendering, native-window behavior, installation/consumer checks, and performance measurements.

Update only the rows proven by the delivered work. Milestone labels alone do not establish capability. Active work is in [current](../roadmap/current.md); planned scope is in [backlog](../roadmap/backlog.md).
