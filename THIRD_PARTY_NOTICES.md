# Third-party notices

Tessera does not vendor or redistribute these dependencies. The optional Vulkan module consumes a locally installed SDK and system loader; retain the applicable upstream notices if distributing their code/binaries in a later package. Dependency constraints and acquisition are owned by [dependencies](docs/reference/dependencies.md).

- Vulkan SDK 1.4.350.0 C headers used by the optional backend carry Khronos Group copyright 2015-2026 and SPDX `Apache-2.0`. See [Vulkan-Headers license](https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md) and the notices in the installed headers. Other header versions may offer different licensing choices; consult that version's files.
- Vulkan loader: [Apache-2.0 license and notices](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/LICENSE.txt). The host uses the installed system loader/driver; Tessera ships no loader or driver binary.
- Slang compiler: [Apache-2.0 WITH LLVM-exception](https://github.com/shader-slang/slang/blob/master/LICENSE). Build-time tool only. Compiler binaries and their bundled dependencies are not distributed by Tessera.
- Khronos Vulkan Validation Layers: [Apache-2.0](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/LICENSE.txt). SPIR-V Tools: [Apache-2.0](https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE). Development tools only, obtained with the installed SDK.
- Windows SDK/Win32 is provided by the installed Microsoft toolchain under its SDK terms; no SDK code/binary is copied into this repository.

The primitive shader and procedural 2x2 pixel fixture are Tessera source/test data. No external font, icon, texture, or image asset is introduced.
