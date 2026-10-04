# Dependency policy

Status: The foundation slice uses only C++20 standard-library code at runtime. Graphics/text dependencies remain candidates. Tested tools are recorded in the [support matrix](support-matrix.md).

## Adopted foundation choices

- Core language: C++20, including defaulted equality, heterogeneous string lookup, and floating-point `std::from_chars`/`std::to_chars`. Compiler compatibility is currently validated only with the recorded MSVC configuration; no other minimum compiler version is claimed.
- Build: CMake 3.20 declared minimum, `tessera::core` library target, CTest for the standalone checks. The declared minimum itself has not been exercised; the validated CMake version is recorded separately.
- Serialization: a bounded in-repository JSON reader/writer in `src/ui/serialization.cpp`, no parser dependency or runtime filesystem access. Encoding policy is in [JSON v1](../../formats/tessera-ui/README.md).
- Acquisition: no network fetches, vendored libraries, external fonts, or graphics SDKs. Core-only builds are offline once the host C++ toolchain/CMake are installed. CMake/CTest and compiler tools are build/development requirements, not runtime dependencies.

No third-party code/assets have been introduced, so there are no adopted library/font redistribution notices in this slice. Optional dependencies still require the adoption records below when introduced.

## Module boundaries

| Module | Candidate requirements | Boundary |
| --- | --- | --- |
| Core UI/style/layout/input and common contracts | C++ standard library; optional fmt if justified | No GPU SDK, browser, engine/editor SDK, or font implementation dependency |
| Text implementation | FreeType, HarfBuzz; ICU/equivalent only if needed | Public text/layout contracts hide implementation types |
| Shader build | Slang | Build tool below the rendering boundary; not an authoring/runtime language requirement |
| Vulkan backend | Vulkan SDK/toolchain | SDK types remain within the concrete backend and host integration |
| WebGPU backend | Implementation/toolchain to evaluate | Later module; no current WGSL, browser, or WASM claim |
| Example host | Minimal window/input integration to select | Window ownership remains outside the UI core |
| Serialized frontend | Parser/encoding to select | Runtime IR remains independent of source syntax |

OpenUSD, Chromium/WebView, a JavaScript VM, Qt, and large application frameworks are not foundational dependencies. Optional adapters must not introduce them transitively into core-only consumption.

## Adoption requirements

For each adopted dependency, record the owner module, purpose, pinned or constrained version, license/redistribution terms, acquisition method, and tested toolchain/platform combinations. Explain whether it is needed at build time, application runtime, or only for development.

Prefer independently selectable modules. A core-only build must configure, build, and test without GPU or text implementation SDKs. Do not silently fetch large dependencies as a side effect of unrelated targets.

Fonts, icons, and example images also need provenance and redistributable license records. Add third-party notices when actual assets or libraries are introduced; candidate names here do not imply redistribution.

## Decisions still required

- Additional compiler/toolchain validation and optional-module dependency acquisition.
- Text library/font fixtures and default fallback strategy.
- Vulkan requirements and Slang compiler/artifact targets.
- Explicit JSON version migrations and expanded property schemas, when a consumer requires them.

Record final choices here and validated combinations in the [support matrix](support-matrix.md). Hydra-merlin's pins are reference-project choices and must not be copied as Tessera validation evidence.
