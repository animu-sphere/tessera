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

Use the actual installed paths; explicit overrides avoid mixing an older `VULKAN_SDK` environment value with another compiler on PATH. CMake rejects compiler versions outside the adoption record. `tessera_vulkan_shaders` generates `build-vulkan/backends/vulkan/shaders/primitive.{vertex,fragment,image}.spv`; the backend target depends on these artifacts. Shaders are build artifacts and are not loaded from implicit filesystem paths by the renderer: the host passes their words at construction.

The offscreen fixture requires an eligible Vulkan 1.1 graphics device, RGBA8 sRGB attachment/blending/sampling/transfer capabilities, coherent readback memory, and the installed Khronos validation layer. It enables synchronization validation and fails on validation errors. PPM images appear under `build-vulkan/backends/vulkan/artifacts`; logs are under `build-vulkan/Testing/Temporary`. This procedure does not open a window or run a native menu.

For standalone SPIR-V environment checks with the selected SDK:

```powershell
foreach ($stage in 'vertex', 'fragment', 'image') {
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

## Adding workflows

When a backend, native host, real-font fixture, or package/export consumer lands, add the concrete procedure here. Record the shell/generator/configuration, required tools, expected output location, and reproducible clean-build steps. Record actual results and configuration limits only in support.

Each example should prove one boundary and identify placeholder behavior. Scheduled examples/features belong in current or backlog, rather than a second proposed directory list here. Follow [testing](testing.md) for verification and [documentation checks](testing.md#documentation-verification) for docs-only changes.
