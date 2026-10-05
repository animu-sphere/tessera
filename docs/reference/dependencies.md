# Dependency policy

## Adopted foundation choices

- Core language: C++20, including defaulted equality, heterogeneous string lookup, and floating-point `std::from_chars`/`std::to_chars`. Toolchain compatibility evidence belongs in [support](support-matrix.md).
- Build: CMake 3.20 declared minimum, `tessera::core` library target, CTest for standalone checks. Declared requirements are distinct from configurations actually exercised in support.
- Serialization: a bounded in-repository JSON reader/writer in `src/ui/serialization.cpp`, no parser dependency or runtime filesystem access. Encoding policy is in [JSON v1](../../formats/tessera-ui/README.md).
- Acquisition: no automatic network fetches or vendored libraries. Core-only builds are offline once the host C++ toolchain/CMake are installed, without graphics SDKs or external fonts. CMake/CTest and compiler tools are build/development requirements, not runtime dependencies.

Optional Vulkan tools/libraries are adopted below. [Third-party notices](../../THIRD_PARTY_NOTICES.md) records their upstream licenses; no SDK binaries, headers, compiler, fonts, or external image assets are vendored or redistributed.

## Adopted Vulkan choices

| Dependency | Owner and purpose | Constraint and acquisition | License and runtime use |
| --- | --- | --- | --- |
| Vulkan headers and loader import library | Optional `tessera::vulkan`, explicit C API device/command integration | Installed Vulkan SDK, CMake `find_package(Vulkan 1.3 REQUIRED)`; explicit include/library cache overrides permitted; runtime device API >= 1.1 | Selected SDK 1.4.350.0 headers and upstream loader are Apache-2.0; host provides its system Vulkan loader/driver at runtime |
| Slang `slangc` | Vulkan shader build only | Pinned compiler 2026.8, found locally or via `TESSERA_SLANGC`; configure rejects a different version; emits SPIR-V 1.3 | Apache-2.0 WITH LLVM-exception; no Slang runtime link or compiler distribution |
| Khronos validation layer / SPIR-V Tools | GPU fixture diagnostics and artifact verification only | Installed SDK tooling, validation layer required by GPU fixture; tools are not linked into core | Apache-2.0; development-only, not a backend runtime requirement |
| Windows SDK / Win32 | `tessera_vulkan_menu` example host, window/pointer/DPI/surface ownership | Existing host toolchain SDK; per-monitor-v2 DPI APIs (Windows 10 1703 or newer); built only with the Vulkan option and examples on Windows; no GLFW/SDL or new window framework selected | Microsoft SDK terms apply; platform APIs remain in the example host |

The backend option defaults OFF and performs no Vulkan/Slang discovery then. With it ON, installed tools are used without downloads. This optional module requires CMake >= 3.23 for [SDK version discovery](https://cmake.org/cmake/help/v3.23/module/FindVulkan.html); the core retains its declared 3.20 floor. [Rendering](../design/rendering.md#implemented-vulkan-primitive-boundary) owns target, color, image, synchronization and lifetime rules; [development](../guides/development.md#optional-vulkan-workflow) owns shader output paths and commands. Actual SDK/compiler/GPU/layer combinations are recorded only in [support](support-matrix.md#vulkan-primitive-evidence--2026-10-05). Win32 is an example-host choice, not a core platform requirement or a native-host support claim.

## Module boundaries

| Module | Candidate requirements | Boundary |
| --- | --- | --- |
| Core UI/style/layout/input and common contracts | C++ standard library; optional fmt if justified | No GPU SDK, browser, engine/editor SDK, or font implementation dependency |
| Text implementation | FreeType, HarfBuzz; ICU/equivalent only if needed | Public text/layout contracts hide implementation types |
| Shader build | Adopted Slang compiler above | Build tool below the rendering boundary; not an authoring/runtime language requirement |
| Vulkan backend | Adopted Vulkan SDK/toolchain above | SDK types remain within the concrete backend and host integration |
| WebGPU backend | Implementation/toolchain to evaluate | Later module; no current WGSL, browser, or WASM claim |
| Web host / WASM bridge | Compiler/packager and browser facilities to evaluate under [Web host](../design/web-host.md) | DOM, browser editing, JS bindings and worker APIs stay in optional host adapters |
| Inspection/testing/agent tooling | Image encoder, CLI and optional RPC/MCP implementation to evaluate under [inspection](../design/inspection.md) | No agent SDK, transport server or image encoder required by core; production can exclude development facilities |
| Example host | Win32 [Vulkan menu host](../design/rendering.md#implemented-win32-vulkan-example-host) | Window ownership remains outside the UI core and the backend library |
| Additional authoring frontends | Compiler/parser to evaluate after IR stabilization | Runtime IR remains independent of source syntax; JSON v1 uses the adopted in-repository parser |

OpenUSD, Chromium/WebView, a JavaScript VM, Qt, and large application frameworks are not foundational dependencies. Optional adapters must not introduce them transitively into core-only consumption.

## Adoption requirements

For each adopted dependency, record the owner module, purpose, pinned or constrained version, license/redistribution terms, acquisition method, and tested toolchain/platform combinations. Explain whether it is needed at build time, application runtime, or only for development.

Prefer independently selectable modules. A core-only build must configure, build, and test without GPU or text implementation SDKs. Do not silently fetch large dependencies as a side effect of unrelated targets.

Fonts, icons, and example images also need provenance and redistributable license records. Add third-party notices when actual assets or libraries are introduced; candidate names here do not imply redistribution.

## Decisions still required

- Additional compiler/toolchain validation and optional-module dependency acquisition.
- Text library/font fixtures and default fallback strategy.
- Deployment/redistribution policy for packaged native hosts and future SDK/compiler upgrades.
- Explicit JSON version migrations and expanded property schemas, when a consumer requires them.

Record final choices here and validated combinations in the [support matrix](support-matrix.md). Hydra-merlin's pins are reference-project choices and must not be copied as Tessera validation evidence.
