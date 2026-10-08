# Grayscale glyph references

These seven small PGM coverage images are generated directly from the committed [Noto font fixtures](../fonts) by [generate_reference.py](generate_reference.py), without calling Tessera. The generator requires FreeType 2.13.3, uses 72 dpi, a 26.6 physical size, no hinting, autohinting, or stem darkening, no embedded bitmap, default variation coordinates, and normal grayscale rendering. [reference.json](reference.json) records glyph indices, physical sizes, dimensions, and baseline bearings. Glyph indices also appear in the independently derived shaping fixtures; the references cover a Latin outline, negative bearing/descender, ligature, combining mark, fractional size, Japanese CFF outline, and `.notdef`.

To regenerate with the adopted local Windows acquisition:

```powershell
python tests/fixtures/glyphs/generate_reference.py C:/dev/vcpkg/installed/x64-windows/bin/freetype.dll
```

Pass the matching shared-library path for another acquisition. Regeneration is a deliberate reference update, not part of a test run. Review the dimensions, bearings, and pixels before accepting changes. The Noto fixture versions/provenance and licenses remain in [dependencies](../../../docs/reference/dependencies.md#adopted-text-choices); Tessera grants no additional third-party font rights. The generator uses only Python's standard library and the existing FreeType installation, with no downloads.

[Glyph raster checks](../../text/glyph_raster_tests.cpp) compare every coverage byte and the baseline bearings, and emit actual PGM files plus a wrapped mixed-script contact image to the build directory's `modules/fonts/artifacts`. The contact image uses rounded baseline positions for inspection; it is not a CPU renderer or GPU/pixel-snapping reference. The backend-neutral cache contract is in [text](../../../docs/design/text.md).
