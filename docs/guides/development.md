# Development

Read [architecture](../design/architecture.md), [UI model](../design/ui-model.md), and [current work](../roadmap/current.md) first. Adopted tool/dependency requirements are owned by [dependencies](../reference/dependencies.md); validated configurations and results are owned by [support](../reference/support-matrix.md).

## Core build workflow

Run these PowerShell commands from the repository root, with the toolchain recorded in support installed:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
& .\build\Debug\tessera_hello_ui.exe
& .\build\Debug\tessera_flex_layout.exe
& .\build\Debug\tessera_pointer_menu.exe
```

The default configuration enables the core, tests, and examples. Build output is under `build/Debug`; CTest logs are under `build/Testing/Temporary`. Examples describe their own output and placeholders. [CMakeLists.txt](../../CMakeLists.txt) owns target/options/test registration; do not mirror its complete inventory here.

MSVC uses the ordinary Windows SDK C++ toolchain. A core-only workflow requires no graphics/font/editor SDK or dependency download. Restricted build environments may need access to the toolchain's SDK configuration directories; dated environment issues belong in support evidence.

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

The offscreen fixture requires an eligible Vulkan 1.1 graphics device, RGBA8 sRGB attachment/blending/sampling/transfer capabilities, coherent readback memory, and the installed Khronos validation layer. It enables synchronization validation and fails on validation errors. PPM images appear under `build-vulkan/backends/vulkan/artifacts`, including `glyph-scale-{1,2,3,4}.ppm`, `glyph-transforms.ppm`, `glyph-rejected.ppm`, `glyph-blank.ppm`, and `placeholder-menu.ppm`; logs are under `build-vulkan/Testing/Temporary`. Placeholder marks need no installed fonts. This procedure does not open a window or run a native menu.

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

With `TESSERA_BUILD_VULKAN=ON` and examples enabled on Windows, the build above also produces the [example host](../design/rendering.md#implemented-win32-vulkan-example-host); CTest registers its smoke as `vulkan_menu_smoke`. Run it from the repository root:

```powershell
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe          # interactive; click Quit or close the window
& .\build-vulkan\backends\vulkan\Debug\tessera_vulkan_menu.exe --smoke  # scripted; exits 0 on success
```

The executable loads the SPIR-V artifacts from the build tree's shader directory. The smoke needs an interactive desktop session, the Khronos validation layer, and a device presenting an sRGB swapchain with transfer-source usage. It writes `vulkan-menu-{1,2,3,4}.ppm` presentation captures to `build-vulkan/backends/vulkan/artifacts` and fails after 30 seconds if the sequence stalls. Physical mouse activity over the window during the smoke can perturb it; the smoke reads no game controller.

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

CMake rejects a HarfBuzz version other than the pinned one. The toolchain copies the HarfBuzz and FreeType DLLs beside `build-fonts/modules/fonts/<config>/tessera_font_shaper_tests.exe`; with another acquisition, put the runtime libraries on `PATH`. The `font_shaper` test reads the committed fixtures from `tests/fixtures/fonts` and needs no installed or system fonts. A fresh configure with the option OFF and no toolchain file, as in the library-only workflow, confirms that the core discovers no font library.

## Release workflow

[VERSION](../../VERSION) holds the single `MAJOR.MINOR.PATCH` value; CMake reads it as the project version. A release is cut only after the active milestone's exit criteria are accepted against [support](../reference/support-matrix.md); 0.x versions carry no API/ABI compatibility guarantee. Patch releases fix defects in released scope; otherwise development continues on `main` without maintenance branches.

1. Rerun the core and optional Vulkan workflows above in fresh build directories and record the result in support.
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

The [release workflow](../../.github/workflows/release.yml) then checks that the tag matches `VERSION` and has a dated changelog section, builds and tests the core in Debug and Release with Vulkan disabled, and publishes a source-only GitHub Release whose notes are that changelog section. 0.x releases are marked as pre-releases. Hosted runners have no GPU or interactive session, so the GPU fixtures and native menu smoke are not part of this gate; their evidence comes from step 1. Pull requests that change release inputs run the same checks against `v<VERSION>` without publishing.

No binaries or installed packages are published; consumers build from source. Packaging scope is owned by the [backlog](../roadmap/backlog.md).

## Adding workflows

When a backend, native host, real-font fixture, or package/export consumer lands, add the concrete procedure here. Record the shell/generator/configuration, required tools, expected output location, and reproducible clean-build steps. Record actual results and configuration limits only in support.

Each example should prove one boundary and identify placeholder behavior. Scheduled examples/features belong in current or backlog, rather than a second proposed directory list here. Follow [testing](testing.md) for verification and [documentation checks](testing.md#documentation-verification) for docs-only changes.
