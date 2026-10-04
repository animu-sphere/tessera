# Support matrix

Evidence through: 2026-10-05. This is the sole live implementation/validation status record. Dated evidence below retains the limits of each tested slice; it is not a claim that documentation edits reran those checks. No public version has been released.

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
| Fixed/stack/flex layout | Validated for the single-line prototype with placeholder text; wrapping, absolute, scroll, and grid planned | Numeric geometry fixtures without GPU |
| Paint list and Vulkan primitives | CPU background/border/Text paint generation; optional Vulkan solid/rounded/border/clip/transform/alpha/image execution verified with offscreen GPU fixtures below | Native menu/Text/presentation evidence remains required |
| Pointer input and hit testing | Rectangular targeting, hover/primary activation, cancellation, and snapshot refresh verified with synthetic events; native input planned | Synthetic events plus interactive host smoke |
| Focus, keyboard/gamepad navigation | Planned | Deterministic navigation and recovery fixtures plus menu smoke |
| Text shaping and Latin/Japanese fallback | Measurement/shaping boundary and deterministic placeholder shaper implemented; real shaping planned | Declared fonts, metrics/wrapping fixtures, glyph images |
| Stylesheets, selectors, pseudo states | Planned | Cascade, inheritance, state invalidation fixtures |
| Component state/reconciliation | Planned | Key identity, update and cleanup evidence |
| Image assets, themes, live reload | Low-level Vulkan image binding/sampling/crop/tint/retirement verified; application asset API, themes and live reload planned | Host asset ownership/readiness, theme validation, valid/invalid reload cases |
| Path-finder integration | Planned | Shared-document authoring/preview round trip |
| WebGPU | Planned | Toolchain/artifact checks and matching runtime fixtures |
| Accessibility metadata/adapters | `labelled_by` relationship storage/reference validation implemented; adapters planned | Existence validation only; no role/name/state schema or native integration |
| Animation and advanced custom paint | Planned | Resolved-value/time/invalidation and draw-list boundary checks |
| Property reflection/introspection | Planned; explicit typed values/validation exist, no descriptor API | Schema/default/range/encoded-name agreement and Inspector consumer |
| SemanticTree and semantic actions | Planned; relationship storage is not a semantic projection | Deterministic roles/names/state/actions, eligibility and stale-target checks |
| Deterministic replay tooling | Planned; repeated-run fixtures are not a recording/playback tool | Versioned controlled inputs, expected geometry/semantics/actions/paint |
| DPI/coordinate integration | Vulkan framebuffer/scissor conversion and target resize verified at scales 1/1.25/1.5/2; native DPI/input mapping unvalidated | Native fractional-scale/input/resize integration; pixel snapping remains planned |
| Overlay/Portal and virtualized lists | Planned | Layer geometry/order, ownership/focus cleanup and keyed scroll stability |
| Editing/IME/clipboard | Committed text event validation only; composition/editing and clipboard planned | Replacement/selection units, Japanese composition and platform adapter evidence |

## Configuration status

| Configuration | Current evidence |
| --- | --- |
| Core-only build | Windows x64/MSVC Debug library-only build plus document/contract/layout/paint/pointer tests and examples verified; no external runtime dependencies |
| Vulkan | SDK/Slang artifacts and offscreen GPU runtime fixtures verified below; native window/swapchain/menu unvalidated |
| WebGPU native | No selected implementation or toolchain |
| Browser / WASM | Feasibility candidate only; no build or browser tests |
| Metal / Direct3D 12 | Possible later targets; no scheduled implementation |
| Windows / Linux / macOS | Windows x64/MSVC Debug core and offscreen Vulkan fixtures verified below; Linux/macOS and other Windows toolchains unvalidated |

Core configurations are distinct from the offscreen Vulkan evidence below. Neither establishes native-window/device input, real text, packaging, or broad Windows support.

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

## Layout prototype evidence — 2026-10-05

- Revision: uncommitted implementation on base `72f72205465b6859487e48f84c0f70ba1f3622e9`. Identifiable source adds `src/layout/flex_layout.cpp`, `compute_layout` in `include/tessera/layout/layout_box.hpp`, [layout checks](../../tests/layout/layout_tests.cpp), and `examples/flex-layout` to the paths above.
- Source fingerprint: SHA-256 `55afcb6067b6595c24e8887b9508b2f0bb4309b16111a106c11a2f0cd45cf2a9`, computed with the foundation procedure over `CMakeLists.txt`, `include`, `src`, `examples`, and `tests`. The same procedure reproduces the subsystem contract fingerprint at the base revision.
- Configuration: the same OS, Visual Studio/MSVC `19.51.36256.0`, CMake/CTest `4.4.3`, `Visual Studio 18 2026`, `-A x64`, Debug configuration as the foundation evidence.
- Procedure: the [development](../guides/development.md) configure/build/CTest/example commands, plus a fresh `build-core` library-only configure and build.
- Result: all eight CTest targets (`document`, `layout_contract`, `layout`, `text`, `event`, `draw_list`, `hello_ui`, `flex_layout`) passed. The build, including the new layout source under `/W4`, produced no warnings. `tessera_flex_layout.exe` printed eight boxes, including the centered panel at `x=200 y=128 w=240 h=104`. No external dependency was configured or fetched.
- Limits: single-line flex-like layout only, with no wrapping, absolute positioning, scrolling/clipping, grid, pixel snapping, or dirty-subtree updates. Text sizes come from `PlaceholderTextShaper`, not real fonts. Determinism is checked by repeated runs on this toolchain only.

## Paint and pointer evidence — 2026-10-05

- Revision: uncommitted implementation on base `a3ff459f2056540b1a3a6160ebab9feb73db283a`. Identifiable source adds [paint.hpp](../../include/tessera/render/paint.hpp), `src/render/paint.cpp`, [pointer.hpp](../../include/tessera/input/pointer.hpp), `src/input/pointer.cpp`, shared internal layout snapshot validation, and [paint](../../tests/render/paint_tests.cpp)/[pointer](../../tests/input/pointer_tests.cpp) checks, registers sources/tests in `CMakeLists.txt`, extends `examples/flex-layout`, and adds [pointer-menu](../../examples/pointer-menu/main.cpp).
- Source fingerprint: SHA-256 `1647063ada783c90b9bf15332a8be8223a5c077f79559caac86284b3d4a4a18c`, computed with the foundation procedure over `CMakeLists.txt`, `include`, `src`, `examples`, and `tests`.
- Configuration: the same OS, Visual Studio/MSVC `19.51.36256.0`, CMake/CTest `4.4.3`, `Visual Studio 18 2026`, `-A x64`, Debug configuration as the foundation evidence.
- Procedure: the [development](../guides/development.md) configure/build/CTest commands and direct `tessera_flex_layout.exe`/`tessera_pointer_menu.exe` execution, plus library-only configure/build in the newly created `build-paint-core` directory, reconfigured after adding pointer code. The first fresh configure required approved sandbox escalation for MSBuild's Windows SDK configuration access; builds and CTest ran in the sandbox.
- Result: all eleven CTest targets (`document`, `layout_contract`, `layout`, `text`, `event`, `pointer`, `draw_list`, `paint`, `hello_ui`, `flex_layout`, `pointer_menu`) passed. Both builds produced no warnings under `/W4`. `flex-layout` retained its eight numeric boxes and reported eight backend-neutral paint commands. `pointer-menu` emitted five commands and delivered `activate: start-game from start` and `activate: quit-game from quit` to its synthetic host. The library-only target built successfully; no external dependency was configured or fetched.
- Fixtures/artifacts: numeric/injected-shaper paint cases and synthetic pointer/lifecycle cases above, `build/Testing/Temporary/LastTest.log`, `build/Debug/tessera_{paint,pointer}_tests.exe`, `build/Debug/tessera_{flex_layout,pointer_menu}.exe`, and `build-paint-core/Debug/tessera_core.lib`. Build artifacts/logs are local ignored files.
- Limits: CPU paint/input only, in root logical coordinates, without implicit clips/transforms, images, custom paint, or group opacity. Text uses the deterministic placeholder. Pointer dispatch supports primary activation with first-binding ancestor lookup, not full propagation, capture, logical cancel, focus, or navigation. No shader compilation, glyph rasterization, GPU pixels, native host/device input, or style resolution is demonstrated. Validation checks topology/recorded styles, not every possible stale geometry/text change; callers must recompute layout as specified in [rendering](../design/rendering.md#implemented-paint-generation).

## Vulkan primitive evidence — 2026-10-05

- Revision: uncommitted implementation on base `bdc5c9951ea76e92ddf51ffdc35486323fbe6e2b`. Identifiable source adds [optional module](../../backends/vulkan/CMakeLists.txt), [renderer](../../backends/vulkan/renderer.cpp), [shader](../../backends/vulkan/shaders/primitive.slang), public optional header, and [GPU fixtures](../../tests/render/vulkan_tests.cpp), with root CMake option registration.
- Source fingerprint: SHA-256 `18a82d469cbfe4b83adffb736a4e36d9045c112a3230b716fc2d64ee07ee3d63`; computed with the foundation procedure over `CMakeLists.txt`, `include`, `src`, `examples`, `tests`, and `backends` (47 files, relative paths sorted ordinally).
- Configuration: Windows NT `10.0.26200.0` x64, MSVC `19.51.36256.0`, Visual Studio 18 2026/MSBuild `18.9.1+a81b43525`, Windows SDK `10.0.26100.0`, CMake/CTest `4.4.3`, Debug. Backend header/import library from Vulkan SDK `1.4.350.0`, Slang `2026.8`, SPIR-V 1.3 targeting Vulkan 1.1. Installed Khronos validation layer reports `1.3.290`, with synchronization validation enabled. No dependency download or vendoring occurred.
- GPU/runtime: NVIDIA RTX A5000 (`vendorID=0x10de`, `deviceID=0x2231`), NVIDIA driver `597.16` (raw version `2504261632`), Vulkan device API `1.4.329`; loader instance API `1.4.321`. The fixture requests Vulkan 1.1. RGBA8 sRGB, one sample, clear black/opaque; physical targets 32x32, 40x40, 48x48, 64x64. Pixel assertions use absolute channel tolerance 2/255; shader corners are hard-edged, with no antialiasing.
- Procedure: [optional Vulkan configure/build/CTest and SPIR-V checks](../guides/development.md#optional-vulkan-workflow), followed by fresh `build-vulkan-core` configure/build with Vulkan/tests/examples disabled. Configure/build required approved sandbox escalation for MSBuild's ordinary Windows SDK configuration access; CTest and SPIR-V validation ran in the sandbox.
- Result: all twelve CTest targets passed, including `vulkan` and the eleven existing core/example targets. All three compiled shaders passed `spirv-val --target-env vulkan1.1`. Backend and library-only builds succeeded without warnings; the backend is compiled under `/W4`. Runtime assertions cover ordered linear-light alpha, rounded rectangles/inside borders, clip intersections/restoration/mirroring, affine composition/rotation/skew, image quadrants/crop/tint, scales 1/1.25/1.5/2 and target recreation, rejected calls recording no pixels or consuming frame numbers, image capacity and rebind/unbind retirement (including clipped references). There were no validation-layer errors.
- Artifacts: `build-vulkan/Testing/Temporary/LastTest.log`, `build-vulkan/backends/vulkan/shaders/primitive.{vertex,fragment,image}.spv`, `build-vulkan/backends/vulkan/artifacts/{primitives,rejected,image-crop,transforms,scale-4,scale-5,scale-6,scale-8}.ppm`, and `build-vulkan-core/Debug/tessera_core.lib`. These are local ignored outputs reproduced by the source fixture. The fresh core-only configuration did not discover Vulkan/Slang or build shader/backend targets.
- Limits: offscreen, one device/configuration and RGBA8 sRGB only; the accepted BGRA8 sRGB format is unvalidated. No native window, swapchain/presentation, OS pointer/DPI normalization, full document/Text menu, glyph rasterization, antialiasing, merged batches, device loss, deployment, or performance evidence. Images are host-owned low-level views, not an application asset component. Vulkan queue submission/fences/readback are owned by the fixture host; callers must honor the [renderer boundary](../design/rendering.md#implemented-vulkan-primitive-boundary).

## Recording future evidence

Each claim needs a revision, date, OS/compiler/build configuration, dependency versions, command or procedure, fixture, result/artifact location, and known limitations. GPU claims also need device/driver/backend/target-format information. Separate shader compilation, headless rendering, native-window behavior, installation/consumer checks, and performance measurements.

Update only the rows proven by the delivered work. Milestone labels alone do not establish capability. Active work is in [current](../roadmap/current.md); planned scope is in [backlog](../roadmap/backlog.md).
