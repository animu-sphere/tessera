# Text

## Implemented boundary

[text.hpp](../../include/tessera/text/text.hpp) defines the abstraction. `TextShaper::measure(utf8, style)` returns `TextMetrics`; `TextShaper::shape(utf8, style)` returns a `GlyphRun` whose `metrics` equal `measure` for the same inputs. Shapers are borrowed by layout and paint for each call and may cache internally, so the operations are non-const. Results are owned values; no FreeType/HarfBuzz or other implementation type crosses the boundary.

- `TextStyle`: host-assigned `FontId` (0 selects the host default font), finite positive size in logical units per em, optional positive line height (absent selects the shaper default), weight 1-1000.
- `TextMetrics`: widest line advance by `lines * line_height`, first baseline below the top of the text box, line height, and line count. Empty text is one empty line.
- `Glyph`: font-specific glyph ID, cluster as a UTF-8 byte offset into the source, and baseline pen position relative to the text box top left.
- Failures return no value and diagnostics: `invalid_utf8` at `/text`, `text_too_long` above 4 GiB, and style errors (`invalid_number`/`out_of_range`) under `/style`.

`PlaceholderTextShaper` uses no font data. Each Unicode scalar advances 0.5 em, LF starts a new line and emits no glyph, there is no wrapping, the default line height is 1.25 em, and a 0.8 em ascent is centered in each line. Glyph IDs are Unicode scalar values. It derives `measure` from `shape`, so the two cannot disagree. Its widths are not representative of any real font, including for Japanese text.

Width constraints and wrapping are not part of the interface yet; they arrive with real line breaking.

The optional Vulkan backend can explicitly enable [placeholder Text rasterization](rendering.md#implemented-vulkan-placeholder-text) for default-font runs. That contract owns its bitmap shapes, placement, missing marks, and submission rules. It does not change shaping metrics or establish real font/script coverage.

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

Index units are explicit: clusters are UTF-8 byte offsets. Unicode code points, grapheme clusters, and glyph indices are not interchangeable with them; any later grapheme-aware API must state its own unit.

## Candidate implementation

FreeType is a rasterization candidate; HarfBuzz is a shaping candidate. ICU or an equivalent dependency should be adopted only for a demonstrated Unicode requirement. Font discovery/loading goes through the host asset boundary defined in [rendering](rendering.md).

Begin with UTF-8, Latin, Japanese, and basic fallback fonts. Specify missing-glyph behavior and diagnostics, invalid UTF-8 handling, font/style fallback, line metrics, and wrapping before making coverage claims. Bidirectional layout, grapheme segmentation, line breaking, and IME support need explicit scope; shaping alone does not prove them.

Japanese verification requires declared font fixtures and real mixed-script examples. Do not infer coverage of every script or writing mode from a library choice. Vertical writing and rich-text editor suites are outside early scope. Editing/selection/IME and clipboard contracts are owned by [input](input.md#proposed-editing-ime-and-clipboard-boundary); shaping alone cannot establish editing support.

## Measurement and rendering agreement

Measurement and paint must use compatible shaping and line-break results. Cache keys include the text, font identity/version, relevant style, shaping options, and width constraints. Asset replacement and font fallback changes invalidate both geometry and glyph resources as appropriate.

Atlas/cache allocation, eviction, raster versus SDF/MSDF selection, and scale policy remain open. Glyph resources follow the backend submission/completion lifetime in [rendering](rendering.md). Reusing a live atlas region while an earlier frame references it must be prevented.

## Incremental delivery

Use the placeholder boundary as a reference, then introduce real font abstraction, shaping, glyph cache, fallback, and wrapping. Measurement and paint must share results throughout. Editing/IME follows the input boundary; expanded scripts or glyph techniques require separate evidence. Scheduled scope is owned by [backlog](../roadmap/backlog.md).

Placeholders must be described as placeholders in examples and the [support matrix](../reference/support-matrix.md). An early menu can use placeholder text while validating geometry and interaction.

## Verification

Test measurement/paint agreement, empty strings, invalid encoding, missing glyphs, fallback boundaries, wrapping, and cache invalidation. Use pinned redistributable test fonts with license records when dependencies are adopted. Add a small glyph image regression set; document font versions because changing a font can change layout.
