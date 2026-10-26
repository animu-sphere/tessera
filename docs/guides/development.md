# Development

Read [architecture](../architecture/overview.md), [UI model](../architecture/ui-model.md), and [current work](../roadmap/current.md) first. Adopted tool/dependency requirements are owned by [dependencies](../reference/dependencies.md); validated configurations and results are owned by [support](../reference/support-matrix.md).

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

The default configuration enables the core, tests, and examples. Build output is under `build/Debug`; CTest logs are under `build/Testing/Temporary`. Examples describe their own output and placeholders. [CMakeLists.txt](../../CMakeLists.txt) owns target/options/test registration; do not mirror its complete inventory here.

The [tool-panel example](../../examples/tool-panel/main.cpp) uses synthetic keyboard navigation and placeholder text with no native window. It projects host-owned selection through a [reactive source and calculation](../architecture/reactive-runtime.md#implemented-in-process-graph-contract), explicitly flushes each update batch, then publishes selection-dependent [command eligibility](../architecture/commands.md#implemented-in-process-command-boundary), lowers disabled state into the shared UI document, and resolves an inspection command for the [host execution adapter](../architecture/commands.md#implemented-host-execution-and-result-boundary), which rechecks the invocation, consumes its sequence once, and validates the returned object identity. Selection and deselection rebuild the full UI snapshot; descriptor/query/argument inputs are owned C++ fixtures, not a command recording format. Item details live in a [fault boundary](../architecture/reactive-runtime.md#implemented-subtree-fault-boundary): the selected item has no host record, so flush reports a contained fault and the host prints fallback text while the panel still publishes. After each successful rebuild it [publishes](../architecture/reactive-runtime.md#implemented-effect-and-publication-boundary) the settled graph and delivers a notify-phase effect to a synthetic host status line. Inventory rows use [keyed component mounting](../architecture/ui-model.md#implemented-keyed-component-mounting) to register named pinned state automatically; their owners also hold a name observation, a label binding, and a synthetic thumbnail cleanup: a host reorder keeps the pinned row, its focus, and its binding without recalculation, removal delivers the row's cleanup, and reinsertion starts a new lifetime. Its panel owner also registers a synthetic host subscription's [external cleanup](../architecture/reactive-runtime.md#implemented-external-cleanup-boundary); shutdown closes the owner before the host explicitly delivers that operation and the status effect's last cleanup.

MSVC uses the ordinary Windows SDK C++ toolchain. A core-only workflow requires no graphics/font/editor SDK or dependency download. Restricted build environments may need access to the toolchain's SDK configuration directories; dated environment issues belong in support evidence.

The tool panel also exercises the [transaction boundary](../architecture/commands.md#implemented-synchronous-transaction-boundary) with host-owned item amounts and history. Its [gesture adapter](../architecture/commands.md#implemented-edit-gesture-boundary) begins on the first edit and retains one token across three caption updates through ordinary components and the full presentation pipeline. A normalized pointer release commits once; a subsequent Escape-cancelled edit restores the amount before publishing its restoration. Inspection and editing share one command sequence stream. Native capture/lifecycle integration follows the [transaction proposal](../architecture/commands.md#proposed-transaction-boundary).

Inventory rows use [nested invocation descriptions](../architecture/ui-model.md#implemented-nested-component-invocations): the host supplies one inventory root and ordered keyed row records with weak label arguments and typed height overrides. [Invocation mounting](../architecture/ui-model.md#implemented-component-invocation-mounting) creates/reuses their scoped owners and local pinned state from those same records; [initial setup](../architecture/ui-model.md#implemented-component-initial-setup) attaches row-owned observations and calculations only when a fresh lifetime is created. [Declared cleanup](../architecture/ui-model.md#implemented-declared-component-cleanup) binds the row's thumbnail operation at mounting. The host attaches their weak labels to the invocation arguments, flushes, then calls `lower_component_tree` to compose document/styles before full-tree presentation and focus recovery.

The inventory and row descriptions load embedded document templates with opaque fixture source identities. [Component source mapping](../architecture/ui-model.md#implemented-component-source-mapping) carries their authored node locations through the invocation tree into ordinary inspection. Instance captions and focusable values have no template value span; a separate embedded invocation fixture supplies the row's call, caption-expression and complete-style origins. These identities refer to the embedded fixtures; the example opens no source file through inspection.

Their names use keyed row-owned [typed async observations](../architecture/ui-model.md#implemented-typed-async-view-observations). The host explicitly declares initial cache values and leaves equal item data alone. A synthetic replacement reports progress, fails while retaining the accepted name, retries under a fresh attempt, rejects the old completion and accepts the complete new name in one host batch. Mounting creates its [declared async observation and detachment](../architecture/ui-model.md#implemented-declared-component-async-observations) before fresh setup initializes the cache; a loading name survives reorder, its removal delivers the host attempt address once through cleanup, and its late completion is rejected against both old and recreated rows. The row caption calculation projects that value into the ordinary invocation arguments; no file/network execution or automatic retry runs in core.

Its panel-scoped [keyboard shortcut](../architecture/commands.md#implemented-keyboard-shortcut-boundary) routes a synthetic logical Control+I `KeyDown` to the same live inspection activation owner, with the current selection arguments and shared execution sequence. The host rebuilds the shortcut map with each replacement tree and executes its returned invocation after routing. Native key conventions are a host policy.

Inventory rows inherit text color and size from a host-built [theme](../architecture/styling.md#implemented-theme-variable-boundary). The host switches its typed variables during reorder and resolves the full presentation again with the same keyed owners and component overrides; a zero-size theme candidate is rejected before replacing the accepted presentation. The example uses one table for its tree, with an alias for row ink and no external theme loading.

Before publishing each selection generation, the tool panel captures its full-tree inspection snapshot and evaluates five [UI invariants](../architecture/inspection.md#implemented-in-process-ui-invariant-boundary): inspection-button visibility, selection-dependent enabled/focusable state, command label, and containment within the host-declared logical viewport. Evaluation returns owned observations; the host requires `report.passed()` before publication.

The same scope declares a synthetic [gamepad shortcut](../architecture/commands.md#implemented-gamepad-shortcut-boundary): left shoulder held while pressing west. Its normalized request resolves the same inspection command, owner and arguments as Control+I, then executes with a fresh sequence. Physical button mapping and polling belong to the host. The inspection button's event comes from a [declared component action](../architecture/ui-model.md#implemented-component-actions-and-events) supplied by a panel-owned string source; the host lowers the settled component before applying command eligibility and building the presentation.

The tool-panel rows bind a [declared notification effect](../architecture/ui-model.md#implemented-declared-component-effects) to their pinned state through a fresh-setup factory. After accepting the full presentation, the host publishes and delivers notifications; compatible reorder retains the original registration, changed state replaces its previous application cleanup, and row removal or shutdown delivers the last cleanup explicitly. The printed notification count and host observations follow component lifetimes.

## Editor panel fixture

The [editor prototype](../../examples/editor-prototype/main.cpp) is a scripted core-only consumer of ordinary components, inspection and commands. Its [consumer boundary](../integrations/path-finder.md#implemented-in-process-panel-fixture) describes the panels, string Inspector and bounded source label reload. The preview loads the committed JSON path supplied by CMake, so execution does not depend on the working directory.

After the core build above, run it directly or through CTest:

```powershell
& .\build\Debug\tessera_editor_prototype.exe
ctest --test-dir build -C Debug -R '^editor_prototype$' --output-on-failure
```

Use `Release` for the other configuration. The executable checks panel invariants, schema/value/source observations, metadata preservation, shared eligibility, scalar editing, gesture history/cancellation, label Undo/Redo and branch truncation, label edit/removal/type replacement/restoration, parse/validation/layout/paint rejection, keyed state/focus retention, stale targets, async removal/recreation/shutdown and deterministic replay/CPU comparison. It prints a compact result and exits nonzero on a failed assertion. Source edits and images stay in memory; it opens no window and changes no fixture file. Evidence and configuration limits belong to [support](../reference/support-matrix.md#editor-label-history-evidence--2026-10-16).

## Settings form fixture

The [Settings example](../../examples/settings/main.cpp) uses the [C++ form authoring API](../architecture/ui-model.md#implemented-c-form-authoring) for composition, named state and derived text. The application form and reusable row function are separate from its scripted host/verification code. The host executes commands after normalized input dispatch and publishes only after preparing the complete presentation.

Run it through the ordinary core build:

```powershell
& .\build\Debug\tessera_settings.exe
ctest --test-dir build -C Debug -R '^(authoring|settings)$' --output-on-failure
```

Use `Release` for the other configuration. The executable prints its command/frame observations and exits nonzero on a failed check. It uses placeholder text and synthetic glyph coverage, captures in memory, and opens no window. The [consumer boundary](../integrations/settings-app.md#implemented-form-skeleton-boundary) owns its operations and replay limits; [support](../reference/support-matrix.md#c-form-authoring-evidence--2026-10-16) owns configuration/results.

## Focused checks and timing

Use the [verification ownership and selection policy](testing.md#verification-ownership) to choose checks. After configuring the core build above, an authoring/Diagnostics change can build and run just the affected executables:

```powershell
cmake --build build --config Debug --target tessera_authoring_tests tessera_renderer_diagnostics tessera_renderer_diagnostics_tests
ctest --test-dir build -C Debug -R '^(authoring|renderer_diagnostics|renderer_diagnostics_contract)$' --output-on-failure --no-tests=error
```

Choose other target/test names from [CMake registration](../../CMakeLists.txt), including [independent consumers](../../tests/cmake/register.cmake), rather than copying this selection for every change. `ctest -R` selects execution only; an unrestricted build still compiles all enabled targets. A source-import CTest configures/builds its own host project. Use Release when that configuration is affected or collecting wider integration/release evidence; documentation-only changes use the [documentation check](testing.md#documentation-verification).

For a baseline, configure/build with the ordinary workflow and collect the inventory and complete test results in each configuration, without adding the focused regex:

```powershell
ctest --test-dir build -C Debug -N
ctest --test-dir build -C Debug --output-on-failure --no-tests=error --output-junit "$PWD/build/ctest-debug.xml" --output-log "$PWD/build/ctest-debug.log"
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release --output-on-failure --no-tests=error --output-junit "$PWD/build/ctest-release.xml" --output-log "$PWD/build/ctest-release.log"
```

Build each configuration before its run. Time configure, build and CTest as separate invocations, for example with PowerShell `Measure-Command`, retaining command output and native exit codes. Record fresh versus reused build/cache conditions, toolchain, enabled options and parallelism. Keep JUnit, `Testing/Temporary/CTestCostData.txt`, failure logs and consumer phase logs as artifacts; do not interpret the independent consumers' 600-second timeout as elapsed time. Serial execution is a starting measurement, not evidence for safe maximum parallelism.

Collect hosted queue/job/step durations and 20-30 historical runs when available separately from local timing. Put baseline statistics, artifacts and missing observations only in [support](../reference/support-matrix.md), following the [measurement policy](testing.md#measurement-and-safe-reduction). A documentation update or source inventory is not a measured CI baseline.

### CI scopes and reproducible measurements

The [PR workflow](../../.github/workflows/ci.yml) reports documentation/metadata and a Windows x64 Debug core job on every PR. Its [scope selector](../../scripts/select-ci.ps1) skips the native phases only for recognized Markdown-only changes. Ordinary implementation/example C++ changes run all `contract` and `smoke` checks; build files, public headers, unknown paths and unavailable diffs select the unfiltered core suite, including both imports. Selection occurs inside the reporting job. The [selector regression checks](../../scripts/test-select-ci.ps1) cover mixed changes, missing input and conservative fallback.

The [integration workflow](../../.github/workflows/integration.yml) runs the complete core suite in Debug and Release on main pushes or manual dispatch, including both source imports. The [release workflow](../../.github/workflows/release.yml) retains tag/manual release verification and publication. Optional GPU/font/native evidence follows its separate procedures. Cancel superseded PR/integration runs; do not cancel tag publication through their concurrency groups.

Run the same measured phases locally from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-select-ci.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite full
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Release -Suite full
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/measure-core-ci.ps1 -BuildDirectory build-ci -Configuration Debug -Suite pr -TestParallelism 2
```

The measurement script configures a Windows x64 core-only Visual Studio build with tests/examples enabled and optional modules disabled. Use a new build directory for fresh measurements; a reused `CMakeCache.txt` is recorded explicitly. It saves configure/build/test commands, durations and exit codes in `<build>/ci-<configuration>-<suite>/phases.json`, alongside phase logs, selected CTest inventory, JUnit and cost data. Native failures stop the run with a nonzero exit while retaining completed phase measurements. `pr` selects the two core labels, `full` has no filter, and `release` preserves the release gate's `_FetchContent$` exclusion. Build parallelism defaults to two. Keep full/import execution serial until independent hosted concurrency is measured; PR runtime checks can use two test workers. These settings are measurement conditions, not universal speed guarantees.

## Scalar CPU capture workflow

The [CPU renderer and capture adapter](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) are part of `tessera::core`, with no optional dependency discovery. Run their pixel/resource/runner checks through the ordinary core workflow, or isolate them:

```powershell
cmake -S . -B build-v030-cpu -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_VULKAN=OFF -DTESSERA_BUILD_FONTS=OFF
cmake --build build-v030-cpu --config Debug --parallel 2
ctest --test-dir build-v030-cpu -C Debug -R '^cpu_renderer$' --output-on-failure
```

Use `Release` for the other configuration. `CpuRenderer` borrows host target bytes; `CpuFrameCapture` returns owned RGBA8 images with an optional borrowed glyph cache. Supply `frames.backend()` and `&frames` to `run_offscreen` explicitly. A glyph list without an injected cache fails; applications choose the rasterizer and image assets.

To compare both Vulkan paths with CPU output, use the [optional Vulkan workflow](#optional-vulkan-workflow) and its `vulkan` CTest check. `cpu-compare-{reference,batch}-{1,2,3,4}.ppm` and the premultiplied-image scene's `cpu-compare-premultiplied-{reference,batch}-{1,2,3,4}.ppm` appear in `<build>/backends/vulkan/artifacts`. The fixture fixes bilinear clamp sampling, target format, scales, glyph data and boundary conditions; it requires a real eligible GPU and does not replace a failing device with CPU rendering. Recorded configuration and tolerances are in [support](../reference/support-matrix.md#scalar-cpu-rendering-evidence--2026-10-12).

## Library-only workflow

Use a fresh build directory for a clean library-only check:

```powershell
cmake -S . -B build-core -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF
cmake --build build-core --config Debug
```

This produces `build-core/Debug/tessera_core.lib`. Test and example options are independently selectable. Build directories are ignored by Git.

Historical evidence also uses a separate directory:

```powershell
cmake -S . -B build-paint-core -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF
cmake --build build-paint-core --config Debug
```

These are procedures, not claims that this edit reran them. Consult support for their recorded outcomes and limits.

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

Standalone CTest invokes both imports automatically; enabling Vulkan adds backend consumer checks. The [driver](../../tests/cmake/run-consumer.cmake) forwards the configured generator, platform, toolset, toolchain and optional SDK paths into isolated host builds. Per-phase logs, executables and libraries are under `<build>/source-consumers/<import>[-vulkan]/<configuration>`; fixture tests are separate from runtime GPU tests. Configuration evidence and limits belong to [support](../reference/support-matrix.md#source-consumption-evidence--2026-10-11).

The hosted [release workflow](../../.github/workflows/release.yml) uses `ctest --test-dir build -C <configuration> --output-on-failure --no-tests=error -E '_FetchContent$'` to exclude FetchContent consumer checks from CI while retaining the add_subdirectory consumers. Local CTest without that filter exercises both import forms.

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

Use the actual installed paths; explicit overrides avoid mixing an older `VULKAN_SDK` environment value with another compiler on PATH. CMake rejects compiler versions outside the adoption record. `tessera_vulkan_shaders` generates `build-vulkan/backends/vulkan/shaders/primitive.{vertex,batch,fragment,image}.spv`; the backend target depends on these artifacts. Shaders are build artifacts and are not loaded from implicit filesystem paths by the renderer: the host passes their words at construction.

The offscreen fixture requires an eligible Vulkan 1.1 graphics device, RGBA8 sRGB attachment/blending/sampling/transfer capabilities, coherent readback memory, and the installed Khronos validation layer. It enables synchronization validation and fails on validation errors. PPM images appear under `build-vulkan/backends/vulkan/artifacts`, including `glyph-scale-{1,2,3,4}.ppm`, `glyph-transforms.ppm`, `glyph-rejected.ppm`, `glyph-blank.ppm`, `placeholder-menu.ppm`, and offscreen runner frames `runner-<run>-<generation>.ppm`; logs are under `build-vulkan/Testing/Temporary`. Placeholder marks need no installed fonts. This procedure does not open a window or run a native menu.

For standalone SPIR-V environment checks with the selected SDK:

```powershell
foreach ($stage in 'vertex', 'batch', 'fragment', 'image') {
    & C:/VulkanSDK/1.4.350.0/Bin/spirv-val.exe --target-env vulkan1.1 `
        "build-vulkan/backends/vulkan/shaders/primitive.$stage.spv"
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $stage" }
}
```

For a fresh core-only independence check after backend changes:

```powershell
cmake -S . -B build-vulkan-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-vulkan-core --config Debug
```

That configuration performs no Vulkan/Slang dependency discovery or shader compilation. Actual results/configurations belong in [support](../reference/support-matrix.md#vulkan-primitive-evidence--2026-10-05).

The same CTest workflow exercises reference and adjacent-batch paths and writes `batch-{reference,adjacent}-{1,3,5,7}.ppm` plus `batch-empty.ppm` beside the primitive artifacts. A separate fresh directory reproduces the batch slice's core independence check:

```powershell
cmake -S . -B build-batch-core -G "Visual Studio 18 2026" -A x64 `
    -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build-batch-core --config Debug
```

### Win32 Vulkan menu host

With `TESSERA_BUILD_VULKAN=ON` and examples enabled on Windows, the build above also produces the [example host](../architecture/rendering.md#implemented-win32-vulkan-example-host); CTest registers its smoke as `vulkan_menu_smoke`. Run it from the repository root:

```powershell
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe          # interactive; click Quit or close the window
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe --smoke  # scripted; exits 0 on success
```

The executable loads the SPIR-V artifacts from the build tree's shader directory. The smoke needs an interactive desktop session, the Khronos validation layer, and a device presenting an sRGB swapchain with transfer-source usage. It writes `vulkan-menu-{1,2,3,4}.ppm` presentation captures to `build-vulkan/backends/vulkan/artifacts` and fails after 30 seconds if the sequence stalls. Physical mouse activity over the window during the smoke can perturb it; the smoke reads no game controller.

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

1. Rerun full core Debug/Release and both independent source-import modes in fresh build directories, plus the optional Vulkan/font/native workflows needed by the release's claims, and record results and unavailable environments in support. Resolve persistent integration/periodic-suite failures before acceptance; the [tier policy](testing.md#validation-tiers) owns gate responsibilities.
2. In the release pull request, set `VERSION` and turn the changelog's `## Unreleased` section into `## vX.Y.Z — YYYY-MM-DD`.
3. Before merging, check the metadata locally:

   ```powershell
   cmake -P cmake/release.cmake
   ```

4. After merging, tag the merge commit and push the tag:

   ```powershell
   git tag -a vX.Y.Z -m "Tessera vX.Y.Z"
   git push origin vX.Y.Z
   ```

The [release workflow](../../.github/workflows/release.yml) then checks that the tag matches `VERSION` and has a dated changelog section, builds and tests the core in Debug and Release with Vulkan disabled, and publishes a source-only GitHub Release whose notes are that changelog section. 0.x releases are marked as pre-releases. Hosted runners have no GPU or interactive session, so the GPU fixtures and native menu smoke are not part of this gate; their evidence comes from step 1. Manual dispatch performs the same verification without publishing. PR metadata/core checks use the [PR workflow](../../.github/workflows/ci.yml); full Debug/Release and both imports use the [integration workflow](../../.github/workflows/integration.yml).

No binaries or installed packages are published; consumers build from source. Packaging scope is owned by the [backlog](../roadmap/backlog.md).

## Adding workflows

When a backend, native host, real-font fixture, or package/export consumer lands, add the concrete procedure here. Record the shell/generator/configuration, required tools, expected output location, and reproducible clean-build steps. Record actual results and configuration limits only in support.

Each example should prove one boundary and identify placeholder behavior. Scheduled examples/features belong in current or backlog, rather than a second proposed directory list here. Follow [testing](testing.md) for verification and [documentation checks](testing.md#documentation-verification) for docs-only changes.
