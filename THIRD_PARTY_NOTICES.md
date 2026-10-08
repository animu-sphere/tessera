# Third-party notices

Tessera does not vendor or redistribute these dependencies, except the fixture fonts listed last. The optional Vulkan module consumes a locally installed SDK and system loader, and the optional fonts module consumes locally installed libraries; retain the applicable upstream notices if distributing their code/binaries in a later package. Dependency constraints and acquisition are owned by [dependencies](docs/reference/dependencies.md).

- Vulkan SDK 1.4.350.0 C headers used by the optional backend carry Khronos Group copyright 2015-2026 and SPDX `Apache-2.0`. See [Vulkan-Headers license](https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md) and the notices in the installed headers. Other header versions may offer different licensing choices; consult that version's files.
- Vulkan loader: [Apache-2.0 license and notices](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/LICENSE.txt). The host uses the installed system loader/driver; Tessera ships no loader or driver binary.
- Slang compiler: [Apache-2.0 WITH LLVM-exception](https://github.com/shader-slang/slang/blob/master/LICENSE). Build-time tool only. Compiler binaries and their bundled dependencies are not distributed by Tessera.
- Khronos Vulkan Validation Layers: [Apache-2.0](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/LICENSE.txt). SPIR-V Tools: [Apache-2.0](https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE). Development tools only, obtained with the installed SDK.
- Windows SDK/Win32 is provided by the installed Microsoft toolchain under its SDK terms; no SDK code/binary is copied into this repository.
- HarfBuzz 10.2.0 (optional fonts module): [Old MIT license](https://github.com/harfbuzz/harfbuzz/blob/10.2.0/COPYING), copyright Google, Red Hat, Behdad Esfahbod, and the other holders listed there. Distributed binaries must include that copyright and permission notice.
- FreeType 2.13.3 (linked directly by the optional fonts module and by the installed HarfBuzz library): used under the [FreeType License](https://gitlab.freedesktop.org/freetype/freetype/-/blob/VER-2-13-3/docs/FTL.TXT), one of its `FTL OR GPL-2.0-or-later` choices. Documentation of distributed binaries must state: "Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved."

The primitive shader and procedural 2x2 pixel fixture are Tessera source/test data. No external icon, texture, or image asset is introduced.

## Fixture fonts

These fonts are redistributed unmodified in [tests/fixtures/fonts](tests/fixtures/fonts) for tests only, each with its license file. They are not bundled defaults. Provenance and hashes are recorded in [dependencies](docs/reference/dependencies.md#adopted-text-choices).

- Noto Sans Regular 2.015 (`NotoSans-Regular.ttf`): Copyright 2022 The Noto Project Authors (https://github.com/notofonts/latin-greek-cyrillic). Licensed under the SIL Open Font License, Version 1.1; see `NotoSans-OFL.txt`.
- Noto Sans JP Regular 2.004 (`NotoSansJP-Regular.otf`): © 2014-2021 Adobe (http://www.adobe.com/). Licensed under the SIL Open Font License, Version 1.1; see `NotoSansJP-OFL.txt`. Noto is a trademark of Google Inc.
