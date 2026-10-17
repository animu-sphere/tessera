# Getting Started

Build a small C++ application with Tessera's form API, then follow [your first app](first-app.md) to understand its composition and host boundary. The [complete source](../../examples/first-app/main.cpp) and [CMake project](../../examples/first-app/CMakeLists.txt) are the copyable starting point. The larger [Settings example](../../examples/settings/main.cpp) adds editing, selection, scrolling and reusable styles.

## Prerequisites

Use CMake 3.20 or newer and a C++20 compiler. The PowerShell commands below use Visual Studio 18 2026 with its x64 C++ tools and Windows SDK. See [dependencies](../reference/dependencies.md) for dependency policy and [support](../reference/support-matrix.md#first-app-onboarding-evidence--2026-10-17) for the exact checked configuration. This core-only example needs no Vulkan SDK, font libraries, package manager or network fetch during configuration.

## Run from the checkout

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_BUILD_FONTS=OFF -DTESSERA_BUILD_VULKAN=OFF
cmake --build build --config Debug --target tessera_first_app --parallel 4 -- /nodeReuse:false
& .\build\Debug\tessera_first_app.exe
ctest --test-dir build -C Debug -R '^first_app$' --output-on-failure --no-tests=error
```

The program prints:

```text
Feature disabled
Feature enabled
Feature disabled
2 host commands; 480x180 RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)
```

It supplies logical keyboard/gamepad events itself, inspects accepted semantic text, and captures CPU pixels in memory. It exits with a nonzero code and a diagnostic if an operation fails. It opens no window and writes no image file. Placeholder text metrics and synthetic glyph marks make this a deterministic host example rather than a real-font UI.

## Copy into your own project

Copy just `examples/first-app/main.cpp` and `examples/first-app/CMakeLists.txt` together. For a reproducible copy from the repository root:

```powershell
New-Item -ItemType Directory -Path build-first-app-source -Force | Out-Null
Copy-Item examples/first-app/main.cpp, examples/first-app/CMakeLists.txt -Destination build-first-app-source
cmake -S build-first-app-source -B build-first-app -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera
cmake --build build-first-app --config Debug --parallel 4 -- /nodeReuse:false
& .\build-first-app\Debug\first_app.exe
ctest --test-dir build-first-app -C Debug --output-on-failure --no-tests=error
cmake --build build-first-app --config Release --parallel 4 -- /nodeReuse:false
& .\build-first-app\Release\first_app.exe
ctest --test-dir build-first-app -C Release --output-on-failure --no-tests=error
```

Replace `C:/dev/tessera` with the absolute path of your checkout. The copy and build directories may live outside it; `main.cpp` has no relative dependency on other example files. The standalone project imports the local source, links only `tessera::core`, and receives C++20 and public include/link requirements from that target. Tessera's embedded defaults exclude its own tests/examples. The host's `first_app` check belongs to the copied project.

The project also accepts `-DTESSERA_IMPORT_MODE=FetchContent` in a separate build directory; it uses the same local checkout without fetching from a network. See [source consumption](development.md#source-consumption-workflow) for both import forms and embedded options. Independent checks copy these same two files and build/run each import in the selected configuration.

## Continue with the source

Read [your first app](first-app.md) for session lifetime, state, composition, command eligibility and presentation publication. Then run the [Settings workflow](development.md#settings-form-fixture), whose [consumer contract](../integrations/settings-app.md) explains its application operations and replay limits. Build/runtime troubleshooting starts with the emitted diagnostic code and path, the actual installed compiler/SDK, and the [development workflow](development.md); configuration evidence belongs in [support](../reference/support-matrix.md).
