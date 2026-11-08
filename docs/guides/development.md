# Development

Start with [current work](../roadmap/current.md) and the code/docs near the change. Consult [architecture](../architecture/overview.md) for structural changes and the affected contract, such as [UI model](../architecture/ui-model.md), before changing its boundary. Adopted tool/dependency requirements are owned by [dependencies](../reference/dependencies.md); validated configurations and results are owned by [support](../reference/support-matrix.md).

For a copyable application project, begin with [Getting Started](getting-started.md) and the [first-app walkthrough](first-app.md). The repository workflow below is for core development and broader fixtures.

## Core build workflow

Run these PowerShell commands from the repository root, with the toolchain recorded in support installed:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
& .\build\Debug\tessera_hello_ui.exe
& .\build\Debug\tessera_flex_layout.exe
& .\build\Debug\tessera_pointer_menu.exe
& .\build\Debug\tessera_tool_panel.exe
```

The default configuration enables the core, tests, and examples. Build output is under `build/Debug`; CTest logs are under `build/Testing/Temporary`. Examples describe their own output and placeholders. [CMakeLists.txt](../../CMakeLists.txt) connects the [options](../../cmake/TesseraOptions.cmake), [core/compiler settings](../../cmake/TesseraCore.cmake), [examples](../../cmake/TesseraExamples.cmake), [runtime test registration](../../cmake/TesseraTesting.cmake) and [independent consumers](../../tests/cmake/register.cmake); do not mirror their complete inventory here. These modules are included at the root to preserve existing target output and test working directories.

On the documented Windows x64 Visual Studio 2026 toolchain, the optional [core-dev preset](../../CMakePresets.json) provides the same core-only development configuration:

```powershell
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

The preset is a local convenience, not a platform support claim. Project option defaults live in [TesseraOptions.cmake](../../cmake/TesseraOptions.cmake); the [measurement script](../../scripts/measure-core-ci.ps1) remains the authority for CI configuration, suite selection and parallelism and chooses the installed default Visual Studio generator. The preset pins the documented local generator, does not select a CI suite and runs the full inventory. Use ordinary configure arguments for other toolchains or optional modules.

The [tool-panel example](../../examples/tool-panel/main.cpp) is a scripted, core-only component/command host with placeholder text. Its source demonstrates keyed rows, reactive updates, explicit cleanup/publication and inspection; it opens no window.

## Editor panel fixture

The [editor prototype](../../examples/editor-prototype/main.cpp) is a scripted core-only consumer of ordinary components, inspection and commands. Its [consumer boundary](../integrations/path-finder.md#implemented-in-process-panel-fixture) describes the panels, string Inspector and bounded source label reload. The preview loads the committed JSON path supplied by CMake, so execution does not depend on the working directory.

After the core build above, run it directly or through CTest:

```powershell
& .\build\Debug\tessera_editor_prototype.exe
ctest --test-dir build -C Debug -R '^editor_prototype$' --output-on-failure
```

Use `Release` for the other configuration. The executable checks panel invariants, schema/value/source observations, metadata preservation, shared eligibility, scalar editing, gesture history/cancellation, label Undo/Redo and branch truncation, label edit/removal/type replacement/restoration, parse/validation/layout/paint rejection, keyed state/focus retention, stale targets, async removal/recreation/shutdown and deterministic replay/CPU comparison. It prints a compact result and exits nonzero on a failed assertion. Source edits and images stay in memory; it opens no window and changes no fixture file. Evidence and configuration limits belong to [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#editor-label-history-evidence--2026-10-16).

## Settings form fixture

The [Settings example](../../examples/settings/main.cpp) uses the [C++ form authoring API](../architecture/ui-model.md#implemented-c-form-authoring) for composition, named state and derived text. The application form and reusable row function are separate from its scripted host/verification code. The host executes commands after normalized input dispatch and publishes only after preparing the complete presentation.

Run it through the ordinary core build:

```powershell
& .\build\Debug\tessera_settings.exe
ctest --test-dir build -C Debug -R '^(authoring|settings)$' --output-on-failure
```

Use `Release` for the other configuration. The executable prints its command/frame observations and exits nonzero on a failed check. It uses placeholder text and synthetic glyph coverage, captures in memory, and opens no window. The [consumer boundary](../integrations/settings-app.md#implemented-form-skeleton-boundary) owns its operations and replay limits; [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#c-form-authoring-evidence--2026-10-16) owns configuration/results.

## Settings starter native host

The [copyable Settings starter](settings-starter.md#run-in-a-windows-window) also has a Windows desktop mode, using the Windows SDK with the ordinary CPU renderer. Build `tessera_settings_starter` and run it with `--native` for interaction or `--native-smoke` for an automated window/input/presentation check. The no-argument smoke remains window-free. Configure with `-DTESSERA_TEST_NATIVE_HOST=ON` to register `settings_starter_native` under the `native` label; run it in a desktop session. The starter guide owns the complete copy/build/run commands and host limitations; [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#settings-native-host-evidence--2026-11-03) records executed checks.

## Hierarchy / Inspector reference application

The [Inspector guide](inspector-hierarchy.md) owns the build/run commands and interaction scope for `tessera_inspector_hierarchy`. Its window-free `inspector_hierarchy` smoke checks the select/edit/return operation and shared inspection/semantic/replay outcomes. Windows `--native` reuses the Settings CPU host; the opt-in `inspector_hierarchy_native` CTest checks injected window messages in a desktop session. No engine or OpenUSD dependency is required.

## Focused checks and timing

Use the [verification ownership and selection policy](testing.md#verification-ownership) to choose checks. After configuring the core build above, an authoring/Diagnostics change can build and run just the affected executables:

```powershell
cmake --build build --config Debug --target tessera_authoring_tests tessera_renderer_diagnostics tessera_renderer_diagnostics_tests
ctest --test-dir build -C Debug -R '^(authoring|renderer_diagnostics|renderer_diagnostics_contract\..*)$' --output-on-failure --no-tests=error -j 2
```

The Diagnostics fixture registers one `renderer_diagnostics_contract.<scenario>` check per independent scenario so `ctest -j` spreads its Debug cost; `tessera_renderer_diagnostics_tests` without arguments runs every scenario in one process, and it accepts scenario names (for example `msaa outlines`) to run a subset directly.

Choose other target/test names from [runtime registration](../../cmake/TesseraTesting.cmake), including [independent consumers](../../tests/cmake/register.cmake), rather than copying this selection for every change. `ctest -R` selects execution only; an unrestricted build still compiles all enabled targets. The suite targets below restrict the build as well. An automatic source-import CTest configures/builds its own add_subdirectory host project. Use Release when that configuration is affected or collecting wider integration/release evidence; documentation-only changes use the [documentation check](testing.md#documentation-verification).

For a baseline, configure/build with the ordinary workflow and collect the inventory and complete test results in each configuration, without adding the focused regex:

```powershell
ctest --test-dir build -C Debug -N
ctest --test-dir build -C Debug --output-on-failure --no-tests=error --output-junit "$PWD/build/ctest-debug.xml" --output-log "$PWD/build/ctest-debug.log"
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release --output-on-failure --no-tests=error --output-junit "$PWD/build/ctest-release.xml" --output-log "$PWD/build/ctest-release.log"
```

Build each configuration before its run. Time configure, build and CTest as separate invocations, for example with PowerShell `Measure-Command`, retaining command output and native exit codes. Record fresh versus reused build/cache conditions, toolchain, enabled options and parallelism. Keep JUnit, `Testing/Temporary/CTestCostData.txt`, failure logs and consumer phase logs as artifacts; do not interpret the independent consumers' 600-second timeout as elapsed time. Serial execution is a starting measurement, not evidence for safe maximum parallelism.

Compare available hosted queue/job/step durations separately from local timing, stating conditions and sample count; no fixed sample count gates adoption or release. Put material baseline statistics, artifacts and missing observations in [support](../reference/support-matrix.md), following the [measurement policy](testing.md#measurement-and-safe-reduction). Routine check results belong in the PR. A documentation update or source inventory is not a measured CI baseline.

### CI scopes and reproducible measurements

The [PR workflow](../../.github/workflows/ci.yml) reports documentation/metadata and the existing `Core / windows-x64 / Debug` job on every PR. Its [scope selector](../../scripts/select-ci.ps1) skips configure/build/test for documentation-only changes: Markdown anywhere, `docs/`, issue templates, the documentation checker and the license. Other changes first run `core`: examples OFF, only runtime/widget `contract` checks, four CTest workers. Selection stays inside the reporting job so documentation-only changes cannot leave a required check pending.

Public headers under `include/`, CMake/build/consumer files, scripts/workflows, unknown paths or an unavailable diff additionally run `consumer` in the same build directory. It keeps examples OFF and selects only `source_consumer_add_subdirectory`: an independent minimal host linking `tessera::core`, not a copied sample application. Core objects are reused; the host retains its isolated build and serial execution.

There is no example-specific change detection or PR example build/run, even when examples or their fixtures change. Example compilation, all smokes and Diagnostics/Settings fixtures belong to main integration; regressions specific to those applications may therefore first appear after merge. The [selector regression checks](../../scripts/test-select-ci.ps1) cover ordinary/mixed changes, missing input and conservative core-consumer fallback. Optional Vulkan/font/native-host changes still require their dedicated checks; a core-only run does not validate those modules. FetchContent remains manual only.

The [integration workflow](../../.github/workflows/integration.yml) runs the complete registered core suite in Debug and Release on main pushes or manual dispatch, including add_subdirectory source imports. The [release workflow](../../.github/workflows/release.yml) retains tag/manual release verification and publication. Neither workflow runs FetchContent; automatic coverage no longer detects regressions specific to that import branch. Optional GPU/font/native evidence follows its separate procedures. Cancel superseded PR/integration runs; do not cancel tag publication through their concurrency groups.

Run the same measured phases locally from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-select-ci.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite core -TestParallelism 4
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite consumer -TestParallelism 4
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite full
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Release -Suite full
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite pr
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite fixtures
```

The measurement script configures Windows x64 Visual Studio with testing ON and Vulkan/fonts OFF. Examples are OFF for both PR suites (`core` and `consumer`), ON otherwise. Existing `pr`/`fixtures`/`imports`/`wide`/`full` meanings remain available for manual use. Build selection and CTest selection correspond as follows:

| Suite | Examples | Parent build target | CTest selection |
| --- | --- | --- | --- |
| `core` | OFF | `tessera_pr_tests` | `contract` only |
| `consumer` | OFF | `tessera_core` | Only `source_consumer_add_subdirectory` |
| `pr` | ON | `tessera_pr_tests` | `contract` + `smoke` |
| `fixtures` | ON | `tessera_fixture_tests` | `pr` + `fixture` |
| `imports` | ON | `tessera_pr_tests` | `pr` + `import` |
| `wide` | ON | `tessera_wide_tests` | No filter |
| `full` | ON | Default build (all enabled targets) | No filter |

[Test registration](../../cmake/TesseraTesting.cmake) associates each runtime test's executable and label with its build group in one place. With examples OFF, the PR group depends only on contracts. Fixture targets depend on the PR group; the wide group also covers an opted-in native host. Imports configure/build their own isolated hosts during CTest. The PR `consumer` suite selects only the minimal core host; copied application imports remain in the legacy `imports`/`wide`/`full` selections, not PR CI. Selecting a narrow suite does not delete executables left by an earlier wide build; use a fresh directory to verify that no examples were built or to measure avoided compilation.

It saves configure/build/test commands, durations and exit codes in `<build>/ci-<configuration>-<suite>/phases.json`, alongside phase logs, selected CTest inventory, JUnit and cost data. The JSON also records suite, examples ON/OFF, selected build targets (an empty array means the default build), fresh/reused cache, local/GitHub Actions execution, OS, logical processor count and parallelism. Core and consumer phases have separate artifact directories even when they share the build cache. Native failures stop the run with a nonzero exit while retaining completed phase measurements. Build parallelism defaults to two and test workers to two; the PR workflow explicitly requests four test workers without changing build parallelism. Import checks remain `RUN_SERIAL` in CTest, so nested builds never overlap each other or runtime checks. These settings are measurement conditions, not universal speed guarantees. Record hosted and local timings separately; Debug PR feedback does not replace full Debug/Release integration. [Support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#core-pr-separation-evidence--2026-11-04) records the fresh before/after comparison and its limits.

## Scalar CPU capture workflow

The [CPU renderer and capture adapter](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) are part of `tessera::core`, with no optional dependency discovery. Run their pixel/resource/runner checks through the ordinary core workflow, or isolate them:

```powershell
cmake -S . -B build-v030-cpu -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_VULKAN=OFF -DTESSERA_BUILD_FONTS=OFF
cmake --build build-v030-cpu --config Debug --parallel 2
ctest --test-dir build-v030-cpu -C Debug -R '^cpu_renderer$' --output-on-failure
```

Use `Release` for the other configuration. `CpuRenderer` borrows host target bytes; `CpuFrameCapture` returns owned RGBA8 images with an optional borrowed glyph cache. Supply `frames.backend()` and `&frames` to `run_offscreen` explicitly. A glyph list without an injected cache fails; applications choose the rasterizer and image assets.

To compare both Vulkan paths with CPU output, use the [optional Vulkan workflow](#optional-vulkan-workflow) and its `vulkan` CTest check. `cpu-compare-{reference,batch}-{1,2,3,4}.ppm` and the premultiplied-image scene's `cpu-compare-premultiplied-{reference,batch}-{1,2,3,4}.ppm` appear in `<build>/backends/vulkan/artifacts`. The fixture fixes bilinear clamp sampling, target format, scales, glyph data and boundary conditions; it requires a real eligible GPU and does not replace a failing device with CPU rendering. Recorded configuration and tolerances are in [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#scalar-cpu-rendering-evidence--2026-10-12).

## Library-only workflow

Use a fresh build directory for a clean library-only check:

```powershell
cmake -S . -B build-core -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF
cmake --build build-core --config Debug
```

This produces `build-core/Debug/tessera_core.lib`. Test and example options are independently selectable. Build directories are ignored by Git.

## Source-consumption workflow

Use an existing checkout from an independent CMake project under the [source-consumption boundary](../integrations/path-finder.md#implemented-source-consumption-boundary). For direct inclusion:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyHost LANGUAGES CXX)
add_subdirectory("${TESSERA_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tessera")
add_executable(my_host main.cpp)
target_link_libraries(my_host PRIVATE tessera::core)
```

Set `TESSERA_SOURCE_DIR` to the checkout's absolute path at configure time. For FetchContent, replace `add_subdirectory` with:

```cmake
include(FetchContent)
FetchContent_Declare(tessera SOURCE_DIR "${TESSERA_SOURCE_DIR}")
FetchContent_MakeAvailable(tessera)
```

These forms use local source without a network fetch. Consumers can acquire and pin a source revision under their own policy. Enable `TESSERA_BUILD_VULKAN=ON` before inclusion to link `tessera::vulkan`, using the installed [optional backend prerequisites](#optional-vulkan-workflow). The backend target builds its shader artifacts; the host supplies shader words and Vulkan objects under the [renderer contract](../architecture/rendering.md#implemented-vulkan-primitive-boundary).

`TESSERA_BUILD_TESTING` and `TESSERA_BUILD_EXAMPLES` default ON in a standalone configure and OFF when embedded. Host cache values set before inclusion override these defaults. Tessera tests require both `BUILD_TESTING` and `TESSERA_BUILD_TESTING`; an embedded host opting into them enables CTest in its own root. `BUILD_TESTING=OFF` continues to exclude all Tessera checks in standalone library-only builds. Fonts and Vulkan retain their explicit OFF defaults in either mode. [CMakeLists.txt](../../CMakeLists.txt) owns these controls; they do not modify the host's testing option.

The [minimal consumer fixture](../../tests/cmake/consumer/CMakeLists.txt) has no manually supplied public include paths, C++ language settings or transitive dependencies. To run it separately:

```powershell
cmake -S tests/cmake/consumer -B build-source-consumer -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera -DTESSERA_IMPORT_MODE=add_subdirectory
cmake --build build-source-consumer --config Debug --parallel 2
ctest --test-dir build-source-consumer -C Debug --output-on-failure
```

Use `-DTESSERA_IMPORT_MODE=FetchContent` in a separate build directory for the other import. To exercise optional backend linkage, add `-DCONSUMER_WITH_VULKAN=ON` and the explicit SDK/Slang cache overrides from the optional workflow below. The backend fixture rejects an empty host context and requires no GPU or native window. Use the same build and CTest commands with `Release` for that configuration.

Automatic CTest imports use add_subdirectory only, including when Vulkan is enabled. The [registration](../../tests/cmake/register.cmake) retains each application's independent copied-project check rather than relying on the minimal host for application behavior:

| Consumer | Verification responsibility |
| --- | --- |
| `source_consumer` | Public alias compile/link requirements, embedded development defaults, host project/testing ownership, no core-only Vulkan/Slang discovery, document/tree use |
| `first_app_consumer` | Copied two-file CMake application and its state/composition/input/CPU smoke |
| `settings_starter_consumer` | Copied three-file application, Windows host libraries and window-free edit/Apply/Cancel smoke |
| `renderer_diagnostics_consumer` | Copied panel/host/sample sources and synthetic telemetry/settings smoke |
| `source_consumer_vulkan` (optional) | Backend alias linkage and empty-context rejection without a GPU/window |

The [driver](../../tests/cmake/run-consumer.cmake) forwards the configured generator, platform, toolset, toolchain and optional SDK paths into isolated host builds. Per-phase logs, executables and libraries are under `<build>/source-consumers/<import>[-vulkan]/<configuration>` (copied applications add their starter name before the import); fixture tests are separate from runtime GPU tests. Imports remain `RUN_SERIAL`; this change does not establish safe concurrency.

FetchContent support, its local-source project options and the manual configure/build/test commands above remain available, but no default, PR, integration or release CTest run exercises it. Run the affected consumer manually when changing or certifying that import path, including copied applications through their own guides. Removing those automatic checks reduces repeated independent builds; it is not evidence that FetchContent itself is intrinsically slower. Historical configuration evidence and current limits belong to [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#source-consumption-evidence--2026-10-11).

## Optional Vulkan workflow

Install the [adopted tools](../reference/dependencies.md#adopted-vulkan-choices) first. The option defaults OFF. To reproduce the explicit SDK/compiler selection on Windows:

```powershell
cmake -S . -B build-vulkan -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_VULKAN=ON `
    -DVulkan_INCLUDE_DIR=C:/VulkanSDK/1.4.350.0/Include `
    -DVulkan_LIBRARY=C:/VulkanSDK/1.4.350.0/Lib/vulkan-1.lib `
    -DTESSERA_SLANGC=C:/VulkanSDK/1.4.350.0/Bin/slangc.exe
cmake --build build-vulkan --config Debug
ctest --test-dir build-vulkan -C Debug --output-on-failure
```

Use the actual installed paths; explicit overrides avoid mixing an older `VULKAN_SDK` environment value with another compiler on PATH. CMake rejects compiler versions outside the adoption record. `tessera_vulkan_shaders` generates `build-vulkan/backends/vulkan/shaders/primitive.{vertex,batch,fragment,image}.spv`; the backend target depends on these artifacts. Shaders are build artifacts and are not loaded from implicit filesystem paths by the renderer: the host passes their words at construction. `tessera_add_procedural_fill(<target> <source.slang> <output.spv>)` compiles a host [procedural fill](../architecture/rendering.md#implemented-procedural-fills) against `backends/vulkan/shaders/procedural.slang`; the test fixture builds `procedural.{solid,probe}.spv` and the menu host `procedural.aurora.spv` in the same shader directory, and hosts pass those words to `register_procedural_fill`.

The offscreen fixture requires an eligible Vulkan 1.1 graphics device, RGBA8 sRGB attachment/blending/sampling/transfer capabilities, coherent readback memory, and the installed Khronos validation layer. It enables synchronization validation and fails on validation errors. PPM images appear under `build-vulkan/backends/vulkan/artifacts`, including `glyph-scale-{1,2,3,4}.ppm`, `glyph-transforms.ppm`, `glyph-rejected.ppm`, `glyph-blank.ppm`, `placeholder-menu.ppm`, `procedural-<scene>-{reference,batch}-<width>.ppm`, and offscreen runner frames `runner-<run>-<generation>.ppm`; logs are under `build-vulkan/Testing/Temporary`. Placeholder marks need no installed fonts. This procedure does not open a window or run a native menu.

For standalone SPIR-V environment checks with the selected SDK:

```powershell
foreach ($stage in 'vertex', 'batch', 'fragment', 'image') {
    & C:/VulkanSDK/1.4.350.0/Bin/spirv-val.exe --target-env vulkan1.1 `
        "build-vulkan/backends/vulkan/shaders/primitive.$stage.spv"
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $stage" }
}
foreach ($fill in 'solid', 'probe', 'aurora') {
    & C:/VulkanSDK/1.4.350.0/Bin/spirv-val.exe --target-env vulkan1.1 `
        "build-vulkan/backends/vulkan/shaders/procedural.$fill.spv"
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $fill" }
}
```

For a fresh core-only independence check after backend changes:

```powershell
cmake -S . -B build-vulkan-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-vulkan-core --config Debug
```

That configuration performs no Vulkan/Slang dependency discovery or shader compilation. Actual results/configurations belong in [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#vulkan-primitive-evidence--2026-10-05).

The same CTest workflow exercises reference and adjacent-batch paths. Use the core-only configuration above to check backend independence.

### Latest UI GPU regression

With examples enabled, `vulkan_ui` reuses the ordinary operations in the Stack toolbar, Grid Inspector, Popup/Tooltip, Modal/Dialog, TreeView/ListView, Menu/ContextMenu and Settings starter (Dropdown, Slider and SplitPane). Seven applications expose synchronous borrowed-frame observers only to the fixture build; their standalone core-only entry points and copied Settings starter remain independent of Vulkan.

The fixture captures 31 named accepted states at scales 1, 1.25, 1.5 and 2: 124 cases in RGBA8 sRGB over opaque white. Procedural, scaled 5x7 marks use placeholder metrics, not real font shaping. Every case compares both Vulkan paths over the whole frame within 2/255 encoded channels, repeats the batch capture byte-for-byte, and compares with scalar CPU within the same encoded tolerance outside explicitly recorded raster-boundary pixels. Those exclusions are only pixel centers within one device subpixel unit of an axis-aligned outer/inner edge, restricted by the active clip; more than 95% of CPU pixels must remain. Transforms and rounded geometry require extending this oracle, rather than silently relaxing it. Each operation must change the initial capture.

Run just that fixture after building `tessera_vulkan_tests`:

```powershell
ctest --test-dir build-vulkan -C Debug -R '^vulkan_ui$' --output-on-failure --no-tests=error
```

For local Windows automation, use the [driver](../../scripts/test-vulkan-ui.ps1). It configures both optional modules and examples, builds only the GPU test/menu executables, checks that all four expected tests are registered and enabled, then runs `vulkan`, `vulkan_ui`, `vulkan_menu_smoke` and `vulkan_menu_fonts_smoke` serially in Debug and Release. An interactive desktop, a hardware Vulkan device, validation layers and the installed font dependencies are required; do not move the mouse over the smoke window. There is no CPU/software-device substitution, skip or automatic retry.

```powershell
.\scripts\test-vulkan-ui.ps1 -BuildDirectory build-ui-vulkan -CMakeArguments @(
    '-DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake',
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    '-DVulkan_INCLUDE_DIR=C:\VulkanSDK\1.4.350.0\Include',
    '-DVulkan_LIBRARY=C:\VulkanSDK\1.4.350.0\Lib\vulkan-1.lib',
    '-DTESSERA_SLANGC=C:\VulkanSDK\1.4.350.0\Bin\slangc.exe'
)
```

Use the installed paths. A previously configured combined directory can instead be passed without `CMakeArguments`; `-Configuration Debug` selects only that configuration. No dependencies are installed or fetched. Each invocation gets a distinct `gpu-regression-<UTC>-<id>` directory under the build, containing commands/exit codes/revision/worktree status in `run.json`, the CMake cache, configure/build logs, per-configuration CTest inventory/log/JUnit, original/native captures and `latest-ui.tsv`. UI artifacts include CPU/reference/batch/repeat PPMs and a PGM boundary mask for each case. Failed runs retain their own evidence and report `failed`; later runs cannot overwrite them. `TESSERA_GPU_OUTPUT` redirects the two GPU hosts into this directory and is restored afterward. Hosted CI remains core-only; this is a local hardware suite, not a provisioned GPU runner.

The driver has window/GPU-free failure-path checks:

```powershell
.\scripts\test-vulkan-ui-driver.ps1
```

Executed configurations, measurements and limitations belong to [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#latest-ui-vulkan-regression-evidence--2026-11-05).

### Win32 Vulkan menu host

With `TESSERA_BUILD_VULKAN=ON` and examples enabled on Windows, the build above also produces the [example host](../architecture/rendering.md#implemented-win32-vulkan-example-host); CTest registers its smoke as `vulkan_menu_smoke`. Run it from the repository root:

```powershell
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe          # interactive; click Quit or close the window
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe --smoke  # scripted; exits 0 on success
```

The executable loads the SPIR-V artifacts, including its aurora procedural backdrop, from the build tree's shader directory. The smoke needs an interactive desktop session, the Khronos validation layer, and a device presenting an sRGB swapchain with transfer-source usage. It writes `vulkan-menu-{1,2,3,4}.ppm` presentation captures to `build-vulkan/backends/vulkan/artifacts` and fails after 30 seconds if the sequence stalls. Physical mouse activity over the window during the smoke can perturb it; the smoke reads no game controller.

For the original Bluetooth Joy-Con L/R pair, run the interactive executable with `--joycons` and bring the menu window to the foreground. Both sides must be connected; use the left directions/stick to move, right A to activate, and right B to cancel. This explicitly selects the pair even when a DualSense or XInput controller is also attached. Device mapping and limits belong to the [input contract](../architecture/input.md#host-boundary).

A separate fresh directory reproduces the host slice's independence check:

```powershell
cmake -S . -B build-host-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-host-core --config Debug
```

A separate fresh directory reproduces the placeholder Text slice's independence check:

```powershell
cmake -S . -B build-glyph-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-glyph-core --config Debug
```

## Optional fonts workflow

Install the [adopted text libraries](../reference/dependencies.md#adopted-text-choices) once, before configuring. With a local vcpkg checkout whose ports provide the pinned versions, in classic mode:

```powershell
& <vcpkg-root>\vcpkg.exe install "freetype[core]:x64-windows" "harfbuzz[core]:x64-windows"
```

Then configure a separate build directory through the vcpkg toolchain file. The option defaults OFF:

```powershell
cmake -S . -B build-fonts -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_FONTS=ON `
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-fonts --config Debug
ctest --test-dir build-fonts -C Debug --output-on-failure
```

CMake requires the pinned HarfBuzz and FreeType versions; the text service also checks the FreeType runtime version. The toolchain copies their DLLs beside `build-fonts/modules/fonts/<config>/tessera_{font_shaper,font_profile,glyph_raster}_tests.exe`; with another acquisition, put the runtime libraries on `PATH`. These tests read the committed fonts from `tests/fixtures/fonts`, with no installed or system fonts. `font_profile` checks sealed [profile services](../architecture/text.md#implemented-in-process-font-profile), repeat real-font replay/session inspection, and atlas preparation at explicit scales without a window or renderer. Raster checks compare the independent PGM references in `tests/fixtures/glyphs`, then write actual images and a wrapped mixed-script contact image to `build-fonts/modules/fonts/artifacts`; the test sets a conflicting CFF stem-darkening environment default to check the fixed per-face policy. The core `glyph_cache` test uses an injected rasterizer without FreeType. A fresh configure with the option OFF and no toolchain file, as in the library-only workflow, confirms that the core discovers no font library.

### Combined font and Vulkan glyph workflow

Enable both modules to include the real atlas image fixtures in `vulkan`. Use the same installed font dependencies and Vulkan/Slang prerequisites as above:

```powershell
cmake -S . -B build-atlas-vulkan -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_FONTS=ON -DTESSERA_BUILD_VULKAN=ON `
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-atlas-vulkan --config Debug --parallel 4
ctest --test-dir build-atlas-vulkan -C Debug --output-on-failure
cmake --build build-atlas-vulkan --config Release --parallel 4
ctest --test-dir build-atlas-vulkan -C Release --output-on-failure
```

The vcpkg toolchain places font DLLs beside the Vulkan test and menu executables. The offscreen fixture host uploads prepared pages as white RGBA with coverage in alpha and a linear sampler, reads completed frames back, and writes `real-glyph-{reference,batch}-{4,5,6,8}.ppm` under `build-atlas-vulkan/backends/vulkan/artifacts`. Its real-font offscreen runner adapter prepares atlas pages per generation and writes `runner-real-<run>-<generation>.ppm` beside them. The two paths use different page sizes to challenge placement independence. Tests preserve the primitive/placeholder and original native smoke fixtures. The `glyph_atlas` core test requires neither optional module.

On Windows with examples enabled, the combined configuration also registers `vulkan_menu_fonts_smoke`, passing the committed Noto assets explicitly. It requires the same desktop/validation/presentation capabilities as the original smoke, plus linearly sampled RGBA8 UNORM transfer destinations and coherent upload memory. It writes `vulkan-menu-real-{1,2,3,4,5}.ppm` beside the original `vulkan-menu-{1,2,3,4,5}.ppm` captures. Both native smokes share a CTest resource lock to avoid overlapping foreground focus and mouse capture. Run the real menu directly from the repository root:

```powershell
& .\build-atlas-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe --fonts `
    tests/fixtures/fonts/NotoSans-Regular.ttf tests/fixtures/fonts/NotoSansJP-Regular.otf
& .\build-atlas-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe --smoke --fonts `
    tests/fixtures/fonts/NotoSans-Regular.ttf tests/fixtures/fonts/NotoSansJP-Regular.otf
```

Paths may include Unicode characters and spaces (quote them as usual). Omitting `--fonts` selects the placeholder menu even in a combined build. The smoke's mixed-script wrapping/image expectations require the [declared fixture profile](../architecture/text.md#declared-grayscale-fixture-profile); interactive use can provide other scalable faces subject to the shaper's limitations. Unavailable or rejected explicit assets fail with a diagnostic. A fresh library-only independence check uses:

```powershell
cmake -S . -B build-atlas-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF `
    -DTESSERA_BUILD_FONTS=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-atlas-core --config Debug --parallel 4
```

## Release workflow

[VERSION](../../VERSION) holds the single `MAJOR.MINOR.PATCH` value; CMake reads it as the project version. A release is cut only after the active milestone's exit criteria are accepted against [support](../reference/support-matrix.md); 0.x versions carry no API/ABI compatibility guarantee. Patch releases fix defects in released scope; otherwise development continues on `main` without maintenance branches.

1. Rerun full core Debug/Release and independent add_subdirectory consumers in fresh build directories, plus manual FetchContent checks when that import path is affected or recertified and the optional Vulkan/font/native workflows needed by the release's claims. Keep executed results in the release PR/CI and update support only for changed claims or limits. Resolve persistent integration/periodic-suite failures before acceptance; the [tier policy](testing.md#validation-tiers) owns gate responsibilities.
2. In the release pull request, set `VERSION` and turn the changelog's `## Unreleased` section into `## vX.Y.Z — YYYY-MM-DD`. Add `docs/releases/vX.Y.Z.md` with links to that entry and the published release, and link it from the [release index](../releases/README.md).
3. Before merging, check the metadata locally:

   ```powershell
   cmake -P cmake/release.cmake
   ```

4. After merging, tag the merge commit and push the tag:

   ```powershell
   git tag -a vX.Y.Z -m "Tessera vX.Y.Z"
   git push origin vX.Y.Z
   ```

The [release workflow](../../.github/workflows/release.yml) then checks that the tag matches `VERSION` and has a dated changelog section, builds and tests the core in Debug and Release with Vulkan disabled, and publishes a source-only GitHub Release whose notes are that changelog section. 0.x releases are marked as pre-releases. Hosted runners have no GPU or interactive session, so the GPU fixtures and native menu smoke are not part of this gate; their evidence comes from step 1. Manual dispatch performs the same verification without publishing. PR metadata/core checks use the [PR workflow](../../.github/workflows/ci.yml); full Debug/Release and add_subdirectory imports use the [integration workflow](../../.github/workflows/integration.yml). FetchContent remains manual only.

No binaries or installed packages are published; consumers build from source. Packaging scope is owned by the [backlog](../roadmap/backlog.md).

## Adding workflows

When a backend, native host, real-font fixture, or package/export consumer lands, add the concrete procedure here. Record required tools, commands and output locations. Keep actual results in PR/CI; update support when the supported scope or limits change.

Each example should prove one boundary and identify placeholder behavior. Scheduled examples/features belong in current or backlog, rather than a second proposed directory list here. Follow [testing](testing.md) for verification and [documentation checks](testing.md#documentation-verification) for docs-only changes.

## Documentation checks

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-docs.ps1
```
