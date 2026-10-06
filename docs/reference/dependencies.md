# Dependency policy

## Adopted foundation choices

- Core language: C++20, including defaulted equality, heterogeneous string lookup, and floating-point `std::from_chars`/`std::to_chars`. Toolchain compatibility evidence belongs in [support](support-matrix.md).
- Build: CMake 3.20 declared minimum, `tessera::core` library target, CTest for standalone checks. Declared requirements are distinct from configurations actually exercised in support.
- Serialization: a bounded in-repository JSON reader/writer in `src/ui/serialization.cpp`, no parser dependency or runtime filesystem access. Encoding policy is in [JSON v1](../../formats/tessera-ui/README.md).
- Acquisition: no automatic network fetches or vendored libraries. Core-only builds are offline once the host C++ toolchain/CMake are installed, without graphics SDKs or external fonts. CMake/CTest and compiler tools are build/development requirements, not runtime dependencies.
- Release automation: GitHub-hosted runners with `actions/checkout` and `actions/upload-artifact` pinned to full commit SHAs in the [release workflow](../../.github/workflows/release.yml); the GitHub CLI preinstalled on hosted runners publishes releases. These are repository infrastructure, not build or runtime dependencies. The runner's toolchain is whatever its image provides; it is not a validated configuration until its results are recorded in support.

Optional Vulkan and text tools/libraries are adopted below. [Third-party notices](../../THIRD_PARTY_NOTICES.md) records their upstream licenses. No SDK or library binaries, headers, compiler, or external image assets are vendored or redistributed; the [text fixture fonts](#adopted-text-choices) are the only third-party files in the repository.

## Adopted Vulkan choices

| Dependency | Owner and purpose | Constraint and acquisition | License and runtime use |
| --- | --- | --- | --- |
| Vulkan headers and loader import library | Optional `tessera::vulkan`, explicit C API device/command integration | Installed Vulkan SDK, CMake `find_package(Vulkan 1.3 REQUIRED)`; explicit include/library cache overrides permitted; runtime device API >= 1.1 | Selected SDK 1.4.350.0 headers and upstream loader are Apache-2.0; host provides its system Vulkan loader/driver at runtime |
| Slang `slangc` | Vulkan shader build only | Pinned compiler 2026.8, found locally or via `TESSERA_SLANGC`; configure rejects a different version; emits SPIR-V 1.3 | Apache-2.0 WITH LLVM-exception; no Slang runtime link or compiler distribution |
| Khronos validation layer / SPIR-V Tools | GPU fixture diagnostics and artifact verification only | Installed SDK tooling, validation layer required by GPU fixture; tools are not linked into core | Apache-2.0; development-only, not a backend runtime requirement |
| Windows SDK / Win32 | `tessera_vulkan_menu` example host, window/pointer/keyboard/DPI/surface ownership and XInput gamepad polling | Existing host toolchain SDK; per-monitor-v2 DPI APIs (Windows 10 1703 or newer); XInput 1.4 through the SDK's `xinput` import library; built only with the Vulkan option and examples on Windows; no GLFW/SDL or new window framework selected | Microsoft SDK terms apply; platform APIs remain in the example host |

The backend option defaults OFF and performs no Vulkan/Slang discovery then. With it ON, installed tools are used without downloads. This optional module requires CMake >= 3.23 for [SDK version discovery](https://cmake.org/cmake/help/v3.23/module/FindVulkan.html); the core retains its declared 3.20 floor. [Rendering](../design/rendering.md#implemented-vulkan-primitive-boundary) owns target, color, image, synchronization and lifetime rules; [development](../guides/development.md#optional-vulkan-workflow) owns shader output paths and commands. Actual SDK/compiler/GPU/layer combinations are recorded only in [support](support-matrix.md#vulkan-primitive-evidence--2026-10-05). Win32 is an example-host choice, not a core platform requirement or a native-host support claim.

## Adopted text choices

| Dependency | Owner and purpose | Constraint and acquisition | License and runtime use |
| --- | --- | --- | --- |
| HarfBuzz | Optional `tessera::fonts`, [font shaper](../design/text.md#implemented-font-shaper) shaping and face parsing | Exactly 10.2.0: configure reads `hb-version.h` and rejects another version, because shaping fixtures are exact. Found with `find_package(harfbuzz CONFIG)` from an existing installation; the build fetches nothing. Recorded acquisition: vcpkg classic mode, `harfbuzz[core]` port `10.2.0#3`, consumed through the vcpkg toolchain file | MIT ("Old MIT"); runtime library of the optional module (a DLL with dynamic vcpkg triplets) |
| FreeType | Glyph rasterization for the planned glyph cache; not yet called by Tessera | 2.13.3, `freetype[core]` (no brotli/bzip2/png/zlib) by the same procedure. The vcpkg HarfBuzz build links it, so it is already a runtime dependency of that HarfBuzz library | FreeType License, selected from `FTL OR GPL-2.0-or-later`; distributed binaries must credit the FreeType Project in their documentation |
| Noto Sans 2.015 | Latin [fixture font](../../tests/fixtures/fonts) (TrueType outlines) | Unmodified `NotoSans/unhinted/ttf/NotoSans-Regular.ttf` from release `NotoSans-v2.015` of `notofonts/latin-greek-cyrillic`, SHA-256 `f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8` | OFL-1.1 with no Reserved Font Name; license text beside the font; test data, not a bundled default |
| Noto Sans JP 2.004 | Japanese fixture font (CFF outlines, Japanese subset) | Unmodified `Sans/SubsetOTF/JP/NotoSansJP-Regular.otf` at tag `Sans2.004` of `notofonts/noto-cjk`, SHA-256 `dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073` | OFL-1.1 with no Reserved Font Name; license text beside the font; test data, not a bundled default |

The `TESSERA_BUILD_FONTS` option defaults OFF and performs no HarfBuzz discovery then. Classic-mode vcpkg installs nothing during configure while the repository has no vcpkg manifest; adding one would make configuration fetch dependencies and requires a new decision. Other acquisitions are acceptable when they provide the pinned version and a `harfbuzz::harfbuzz` CMake package; their runtime libraries must be locatable by the tests. Fixture fonts are committed byte-identical to upstream and marked binary in `.gitattributes`. Changing HarfBuzz or a fixture font requires re-deriving the [font shaper checks](../../tests/text/font_shaper_tests.cpp) and a new record here. [Development](../guides/development.md#optional-fonts-workflow) owns the commands; validated combinations are recorded only in [support](support-matrix.md#font-shaper-evidence--2026-10-06).

## Module boundaries

| Module | Candidate requirements | Boundary |
| --- | --- | --- |
| Core UI/style/layout/input and common contracts | C++ standard library; optional fmt if justified | No GPU SDK, browser, engine/editor SDK, or font implementation dependency |
| Text implementation | Adopted HarfBuzz/FreeType above; ICU/equivalent only if needed | Public text/layout contracts hide implementation types |
| Shader build | Adopted Slang compiler above | Build tool below the rendering boundary; not an authoring/runtime language requirement |
| Vulkan backend | Adopted Vulkan SDK/toolchain above | SDK types remain within the concrete backend and host integration |
| CPU reference backend | C++ standard library; ISA-specific SIMD paths optional beside the scalar reference | No GPU SDK, window, or image encoder; presentation and encoding stay in hosts and tooling |
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

## Font licensing

Tessera provides the mechanism for using fonts, not rights to them. The font engine and font licenses stay separate:

- Tessera guarantees only technical handling of supported formats: loading, shaping, layout, rasterization, glyph caching, and GPU drawing.
- Tessera grants no right to redistribute, embed in applications or games, serve as webfonts, use on servers or cloud rendering, convert, or subset a third-party font. Licenses often treat these as separate rights; Tessera does not infer them.
- The repository and any Tessera distribution contain only fonts whose licenses permit redistribution with Tessera, such as OFL, with provenance recorded in [third-party notices](../../THIRD_PARTY_NOTICES.md). These serve examples, CI, and snapshot fixtures.
- Commercial fonts and fonts with unclear terms are never bundled. They enter only as [application or user fonts](../design/text.md#proposed-font-sources-and-selection); the application developer or user verifies the applicable license.

User-facing documentation for font loading should carry a notice equivalent to:

> Tessera can load supported third-party font files, including commercial fonts. Tessera does not grant any rights to redistribute, embed, serve, convert, subset, or otherwise distribute those fonts. Developers are responsible for complying with the applicable font license.

A later packaging step may record font asset metadata (source, family, license identifier, attribution, redistribution status) and warn when a font marked non-redistributable is packaged. Such metadata assists developers; it does not certify license compliance.

## Decisions still required

- Additional compiler/toolchain validation and optional-module dependency acquisition.
- Text: an emoji fixture font, further fixture faces needed by fallback/weight tests, the bundled default fallback chain under [font licensing](#font-licensing), and an MSDF generation implementation (library or in-repository) when MSDF glyphs are scheduled. HarfBuzz/FreeType for other platforms or triplets need their own records.
- Deployment/redistribution policy for packaged native hosts and future SDK/compiler upgrades.
- Explicit JSON version migrations and expanded property schemas, when a consumer requires them.

Record final choices here and validated combinations in the [support matrix](support-matrix.md). Hydra-merlin's pins are reference-project choices and must not be copied as Tessera validation evidence.
