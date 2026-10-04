# Development

Status: Phase 0 tree/document/serialization slice, subsystem boundary contracts, and the Phase 1 fixed/stack/flex layout prototype implemented. Style resolution, dispatch, paint generation, real text, and backend implementations are planned.

## Requirements

Read the [architecture](../design/architecture.md), [UI model](../design/ui-model.md), and [current milestone](../roadmap/current.md). The core uses C++20 and the standard library only, including floating-point `std::from_chars`/`std::to_chars`. CMake 3.20 is the declared minimum; that minimum has not itself been tested. Compiler requirements are feature-based, with only the [recorded MSVC configuration](../reference/support-matrix.md) validated so far.

No Vulkan/WebGPU SDK, Slang, font library, browser, engine/editor SDK, or network dependency acquisition is required. MSVC still uses its ordinary Windows SDK C++ toolchain; this does not introduce platform API types in Tessera headers. Builds use the default static `tessera_core` library with the alias `tessera::core`. Packaging/install/export and shared-library ABI support are not established.

## Verified PowerShell workflow

Verified on Windows x64 with Visual Studio Community 2026, MSVC 19.51.36256.0, CMake/CTest 4.4.3, and the `Visual Studio 18 2026` generator:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
& .\build\Debug\tessera_hello_ui.exe
& .\build\Debug\tessera_flex_layout.exe
```

Run from the repository root. The configure step enables the core, tests, and examples. Expected results: `build/Debug/tessera_core.lib`, the `tessera_*_tests.exe` checks, `tessera_hello_ui.exe`, `tessera_flex_layout.exe`, and CTest passing `document`, `layout_contract`, `layout`, `text`, `event`, `draw_list`, `hello_ui`, and `flex_layout`. `hello-ui` emits canonical JSON and reports three restored ordered nodes. `flex-layout` prints the border box of each of eight menu nodes (the panel at `x=200 y=128 w=240 h=104`) computed with placeholder text metrics. Neither example draws or shapes glyphs. Test logs are in `build/Testing/Temporary/LastTest.log`.

A fresh library-only configuration was also verified:

```powershell
cmake -S . -B build-core -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DTESSERA_BUILD_EXAMPLES=OFF
cmake --build build-core --config Debug
```

This builds only `build-core/Debug/tessera_core.lib`. The options are independently selectable. Build directories are ignored by Git.

The local Codex sandbox initially denied MSBuild access to the user's Windows SDK configuration directory; the same CMake configure/build commands succeeded with the approved sandbox escalation. CTest ran in the sandbox. That access failure is an environment restriction, not a Tessera compile failure. No global Git/toolchain settings were changed.

## Current checks and boundaries

[Document tests](../../tests/serialization/document_tests.cpp) cover semantic and canonical round trips, invalid schemas/properties/IDs/references/versions/actions, metadata, UTF-8/Unicode escapes, finite-number precision, input/output/nesting limits, and snapshot/handle lifetime. The [format specification](../../formats/tessera-ui/README.md) owns their encoding policy. [Layout checks](../../tests/layout/layout_tests.cpp) compare numeric `LayoutBox` fixtures for the fixed/stack/flex prototype. Contract checks under [tests/layout](../../tests/layout), [tests/text](../../tests/text), [tests/input](../../tests/input), and [tests/render](../../tests/render) cover resolved-style/layout-input validation and box geometry, placeholder measurement/shaping agreement, normalized event validation, and draw-list stack/handle/frame validation.

The public headers are under `include/tessera/{ui,layout,style,input,text,render}`; source is under the matching `src/` directories, with shared internal checks in `src/detail`. The direct C++ builder uses owned values, and the runtime tree is an immutable validated snapshot. No callbacks, renderer objects, OS input codes, or font implementation types appear in these contracts.

Only Debug on this toolchain has execution evidence. Other generators, compiler versions, Release builds, operating systems, install/consumer workflows, and shared-library builds remain unvalidated.

## Future workflows

Add verified commands when concrete implementations land for:

1. One backend and shader compilation.
2. A standalone native menu host and normalized input.
3. Real text/font fixtures.
4. Installing and consuming exported targets, when packaging is implemented.

For each command, record supported shell/generator/configuration, required tools, expected output, and known limits. Verify commands on a clean build rather than inheriting reference-project presets or executable names.

## Examples to grow with milestones

| Proposed example | Purpose |
| --- | --- |
| `hello-ui` | Small document, inspect/serialize path, later primitive drawing |
| `flex-layout` | Numeric layout and visual geometry demonstration |
| `gamepad-menu` | Focus, keyboard/gamepad navigation, activation/cancel |
| `inventory` | Components, keyed lists, image assets, state/reload |

`hello-ui` proves tree inspection/serialization and `flex-layout` proves numeric layout geometry; neither draws yet. The other names remain planned directories. Each example should prove one boundary and identify placeholder behavior. See [testing](testing.md) for verification scope and [dependencies](../reference/dependencies.md) for adoption policy.
