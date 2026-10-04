# Dependency policy

Status: Planned dependency boundaries. No dependencies, versions, or toolchains have been adopted or validated.

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

- C++ standard and minimum CMake/compiler versions.
- Dependency acquisition, reproducibility, and offline behavior.
- Text library/font fixtures and default fallback strategy.
- Vulkan requirements and Slang compiler/artifact targets.
- Serialized parser library and schema validation approach.

Record final choices here and validated combinations in the [support matrix](support-matrix.md). Hydra-merlin's pins are reference-project choices and must not be copied as Tessera validation evidence.
