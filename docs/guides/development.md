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

## Adding workflows

When a backend, native host, real-font fixture, or package/export consumer lands, add the concrete procedure here. Record the shell/generator/configuration, required tools, expected output location, and reproducible clean-build steps. Record actual results and configuration limits only in support.

Each example should prove one boundary and identify placeholder behavior. Scheduled examples/features belong in current or backlog, rather than a second proposed directory list here. Follow [testing](testing.md) for verification and [documentation checks](testing.md#documentation-verification) for docs-only changes.
