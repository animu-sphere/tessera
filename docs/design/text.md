# Text

Status: Draft design. Placeholder text is the initial milestone scope.

## Boundary and ownership

```text
external font discovery -> font face -> shaping -> GlyphRun
-> glyph cache / atlas or vector representation -> GPU drawing
```

Text is an independent subsystem. Layout requests metrics through an interface; paint requests positioned glyphs. Neither should expose FreeType/HarfBuzz types or couple measurement to a concrete renderer.

Conceptual operations:

```text
measureText(text, font/style, constraints) -> TextMetrics
shapeText(text, font/style, shaping options) -> GlyphRun
```

Proposed metrics include advance/bounds, baseline, and line information. A glyph run needs positioned glyph identifiers, font references, and mappings back to source text. Decide index units explicitly: UTF-8 byte offsets, Unicode code points, grapheme clusters, and glyph indices are not interchangeable.

## Candidate implementation

FreeType is a rasterization candidate; HarfBuzz is a shaping candidate. ICU or an equivalent dependency should be adopted only for a demonstrated Unicode requirement. Font discovery/loading goes through the host asset boundary defined in [rendering](rendering.md).

Begin with UTF-8, Latin, Japanese, and basic fallback fonts. Specify missing-glyph behavior and diagnostics, invalid UTF-8 handling, font/style fallback, line metrics, and wrapping before making coverage claims. Bidirectional layout, grapheme segmentation, line breaking, and IME support need explicit scope; shaping alone does not prove them.

Japanese verification requires declared font fixtures and real mixed-script examples. Do not infer support for every script or writing mode from a library choice. Vertical writing, rich text editing, selection, and complete international text editing are outside the initial milestones.

## Measurement and rendering agreement

Measurement and paint must use compatible shaping and line-break results. Cache keys include the text, font identity/version, relevant style, shaping options, and width constraints. Asset replacement and font fallback changes invalidate both geometry and glyph resources as appropriate.

Atlas/cache allocation, eviction, raster versus SDF/MSDF selection, and scale policy remain open. Glyph resources follow the backend submission/completion lifetime in [rendering](rendering.md). Reusing a live atlas region while an earlier frame references it must be prevented.

## Incremental delivery

1. Define the text abstraction and deterministic placeholder metrics.
2. Add real font loading, shaping, measurement, fallback, and glyph caching.
3. Add wrapping and render matching Latin/Japanese fixtures.
4. Expand script, editing, or advanced glyph representation support only with separate evidence.

Placeholders must be described as placeholders in examples and the [support matrix](../reference/support-matrix.md). An early menu can use placeholder text while validating geometry and interaction.

## Verification

Test measurement/paint agreement, empty strings, invalid encoding, missing glyphs, fallback boundaries, wrapping, and cache invalidation. Use pinned redistributable test fonts with license records when dependencies are adopted. Add a small glyph image regression set; document font versions because changing a font can change layout.
