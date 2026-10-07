# Text

## Implemented boundary

[text.hpp](../../include/tessera/text/text.hpp) defines the abstraction. `TextShaper::measure(utf8, style, constraints)` returns `TextMetrics`; `TextShaper::shape(utf8, style, constraints)` returns a `GlyphRun` whose `metrics` equal `measure` for the same inputs. Constraints default to absent, preserving two-argument calls; implementations override the three-argument signatures. Shapers are borrowed by layout and paint for each call and may cache internally, so the operations are non-const. Results are owned values; no FreeType/HarfBuzz or other implementation type crosses the boundary.

- `TextStyle`: host-assigned `FontId` (0 selects the host default font), finite positive size in logical units per em, optional positive line height (absent selects the shaper default), weight 1-1000.
- `TextConstraints::max_width`: absent preserves LF-only lines; a finite, non-negative logical width enables wrapping according to the shaper's algorithm below. Zero is valid; an indivisible unit wider than the limit remains on one overflowing line. Negative width is `out_of_range`, non-finite width is `invalid_number`, at `/constraints/max_width`. Constraints are call inputs, not serialized or inherited styles.
- `TextMetrics`: widest line advance by `lines * line_height`, first baseline below the top of the text box, line height, and line count. Empty text is one empty line.
- `Glyph`: font-specific glyph ID, cluster as a UTF-8 byte offset into the source, baseline pen position relative to the text box top left, and the `FontId` whose face the ID indexes. That is the run's `font` (the requested `TextStyle::font`) unless a shaper's fallback selected another face, so a run can mix faces.
- Failures return no value and diagnostics: `invalid_utf8` at `/text`, `text_too_long` above 4 GiB, and style errors (`invalid_number`/`out_of_range`) under `/style`. `validate_text_input` performs these shared checks for every shaper; an implementation may add its own failures.

`PlaceholderTextShaper` uses no font data. Each Unicode scalar advances 0.5 em, LF starts a new line and emits no glyph, the default line height is 1.25 em, and a 0.8 em ascent is centered in each line. With a width constraint it starts a new line before the next scalar would overflow a non-empty line; one oversized scalar stays on the line. This placeholder does not combine graphemes. Glyph IDs are Unicode scalar values, and every glyph names the run's font. It derives `measure` from `shape`, so the two cannot disagree. Its widths are not representative of any real font, including for Japanese text.

Both shapers preserve whitespace and explicit empty/trailing LF lines, reset horizontal pens at each visual line, and keep clusters relative to the whole source text. [Layout](layout.md#implemented-prototype-algorithm) supplies the assigned content width, and paint shapes with that same width.

The optional Vulkan backend can explicitly enable [placeholder Text rasterization](rendering.md#implemented-vulkan-placeholder-text) for default-font runs. That contract owns its bitmap shapes, placement, missing marks, and submission rules. It does not change shaping metrics or establish real font/script coverage.

## Implemented font shaper

[font_shaper.hpp](../../modules/fonts/include/tessera/fonts/font_shaper.hpp) defines `FontShaper`, a `TextShaper` in the optional `tessera::fonts` module that shapes with HarfBuzz under the [adoption record](../reference/dependencies.md#adopted-text-choices). HarfBuzz types stay inside the module, and a core-only build does not discover HarfBuzz.

- Faces: `set_face(font, bytes, face_index)` registers or replaces the one face used for a `FontId`, including the default `FontId` 0, from TTF/OTF bytes or one face of a TTC/OTC collection. The host supplies and loads the bytes through its [asset boundary](rendering.md#assets); the shaper owns its copy and does no filesystem access. Empty data, data that is not a font, an out-of-range face index, a face without `head` or glyphs, and a face whose ascender-to-descender extent is not positive fail as `invalid_font` at `/font` and leave the previous face in place. Replacing a face changes later results; the host lays out and paints again. Shaping a `FontId` with no face fails as `unknown_font` at `/style/font`.
- Fallback: `set_fallback(font, fallbacks)` sets the ordered `FontId`s tried for clusters the face of `font` cannot map; an empty list clears it. Each entry needs a registered face and may appear once, excluding `font`: a missing face is `unknown_font` at `/font` or `/fallback/<i>`, and a repeat is `duplicate_font` at `/fallback/<i>`; a rejected stack leaves the previous one in place. Stacks are not transitive, so a fallback's own stack is not consulted. After a line is shaped with the requested face, each fallback face in order reshapes every maximal byte range of clusters that still contain glyph 0, in the line's direction with the rest of the line as shaping context, and its glyphs replace that range. A face that maps none of a range still replaces it with its own glyph 0, which the next face reshapes. Shaping across a fallback boundary (kerning, ligatures) does not occur, and each glyph names the `FontId` that supplied it. Replacing a fallback face changes later results.
- Shaping: each visual line is one HarfBuzz segment with default features (kerning, ligatures, and mark positioning when the font provides them), direction and script guessed from the line, and the fixed language `und`, so the process locale does not affect results. LF emits no glyph. Bidirectional reordering, script itemization, and a declared locale are not implemented.
- Wrapping: without constraints, visual lines are the LF-separated lines. With `max_width`, shape the remaining LF segment through the fallback stack. If its float advance fits, emit it. Otherwise take its shaped cluster boundaries in source order and shape successive prefixes as independent lines through the same stack, accepting prefixes until the first overflow. Keep at least the first cluster even if oversized. Emit the accepted prefix and repeat with the remaining text. This preserves shaped ligatures and base/mark clusters; kerning and context never extend across a selected wrap. Width comparisons use the same float representation as `TextMetrics::size.width`, so supplying an exact measured width does not introduce a rounding-induced wrap. A constrained segment whose guessed direction is not LTR fails as `unsupported_wrapping_direction` at `/text`, even when it fits. This is cluster wrapping for the Latin/Japanese prototype, with no word preference, Unicode line-break/kinsoku rules, hyphenation, whitespace collapsing, or full grapheme segmentation independent of shaping. Candidate reshaping can take quadratic work; no throughput claim is made.
- Glyphs: glyph IDs are the face's glyph indices. Clusters are UTF-8 byte offsets into the whole text, at HarfBuzz's default grapheme-monotone level, so a ligature or a mark shares the cluster of the first byte of its source. Pen positions accumulate in font units per face and are scaled by each face's `size / units-per-em` per glyph, so measurement involves no accumulated rounding even when a line mixes faces; glyph offsets are added to the pen, with HarfBuzz's upward Y offset converted to the downward box axis.
- Metrics: the requested face's HarfBuzz horizontal font extents (OS/2 typographic values when the font sets `USE_TYPO_METRICS`, otherwise `hhea`). The default line height is ascender minus descender plus line gap; the ascender-to-descender extent is centered in each line (half-leading) for both default and explicit line heights. Fallback faces do not change line height or baseline, so line metrics do not depend on which faces a line uses; their glyphs sit on the requested face's baseline. Width is the widest line's advance, as in the boundary above.
- Missing glyphs: a cluster that no face in the stack can map shapes as glyph 0 (`.notdef`) of the last face tried, the last fallback when a stack exists, with that glyph's advance. The result keeps its value and adds one `missing_glyph` warning at `/text` with the count and the first missing byte offset in the message; `byte_offset` stays empty because it refers to document sources. Paint relocates the warning under the Text node; layout uses the metrics.
- Limits: one face per `FontId`; fallback stacks name `FontId`s, with no families, aliases, or per-weight faces; `weight` is validated but selects no face; variable fonts use their default instance. An LF segment longer than HarfBuzz's buffer accepts fails as `text_too_long`. Measurement is derived from shaping, so the two cannot disagree; no result cache exists, so there is nothing to invalidate. Missing-glyph warnings count emitted glyphs only, excluding provisional wrap candidates.

No glyph rasterization exists for these runs: the [Vulkan placeholder](rendering.md#implemented-vulkan-placeholder-text) contract accepts only placeholder-shaper runs, so a host must not enable it for `FontShaper` output.

## Boundary and ownership

```text
external font discovery -> font face -> shaping -> GlyphRun
-> glyph cache / atlas or vector representation -> GPU drawing
```

Text is an independent subsystem. Layout requests metrics through an interface; paint requests positioned glyphs. Neither should expose FreeType/HarfBuzz types or couple measurement to a concrete renderer.

Keep three layers distinct beneath ordinary components such as labels, text inputs, rich text, and code views:

- Font selection: faces, families, stacks, logical aliases, fallback, and variation instances.
- Text layout: Unicode/script runs, shaping, line breaking, and caret/selection geometry.
- Glyph resources: raster cache and atlas allocation, independent of any backend; backends own only page uploads and sampling under the [rendering](rendering.md) resource lifetime. See the [glyph raster strategy](#proposed-glyph-raster-strategy).

Components never call font or shaping libraries directly.

Conceptual operations:

```text
measureText(text, font/style, constraints) -> TextMetrics
shapeText(text, font/style, shaping options) -> GlyphRun
```

Index units are explicit: clusters are UTF-8 byte offsets. Unicode code points, grapheme clusters, and glyph indices are not interchangeable with them; any later grapheme-aware API must state its own unit.

## Candidate implementation

The baseline is HarfBuzz for shaping and OpenType features and FreeType for rasterization, accepting TTF, OTF, and TTC collections. Both are [adopted](../reference/dependencies.md#adopted-text-choices); the implemented shaper above uses HarfBuzz, and FreeType rasterization arrives with the glyph cache. ICU or an equivalent dependency should be adopted only for a demonstrated Unicode requirement. Font discovery/loading goes through the host asset boundary defined in [rendering](rendering.md#assets).

Web hosts are intended to run the same shaping and layout code in WASM instead of measuring DOM text, so native and Web geometry come from one implementation. Browser font serving remains a host concern.

Begin with UTF-8, Latin, Japanese, and basic fallback fonts. Specify missing-glyph behavior and diagnostics, invalid UTF-8 handling, font/style fallback, line metrics, and wrapping before making coverage claims. Bidirectional layout, grapheme segmentation, line breaking, and IME support need explicit scope; shaping alone does not prove them.

Japanese verification requires declared font fixtures and real mixed-script examples. Do not infer coverage of every script or writing mode from a library choice. Vertical writing and rich-text editor suites are outside early scope. Editing/selection/IME and clipboard contracts are owned by [input](input.md#proposed-editing-ime-and-clipboard-boundary); shaping alone cannot establish editing support.

## Proposed font sources and selection

A host-side font database registers faces from separate sources:

| Source | Origin | Constraint |
| --- | --- | --- |
| Bundled | Fonts shipped with Tessera or its examples | Redistributable only; see [font licensing](../reference/dependencies.md#font-licensing) |
| Application | Fonts supplied as the application's own assets | Distribution rights are the application developer's responsibility |
| User | Font files the user owns, loaded by path or host asset ID | Not redistributed by Tessera; the default route for commercial fonts |
| System | Fonts installed in the OS | Vary by machine; excluded from deterministic fixtures |
| Platform provider | Optional OS font services | Optional host adapter, never a core dependency |

Styles refer to families, stacks, and logical aliases rather than physical files. Proposed aliases are `ui-sans`, `ui-serif`, `ui-mono`, `heading`, `body`, `code`, `emoji`, and `cjk`. Applications can rebind an alias to their own or licensed fonts without modifying Tessera; bundled defaults can change behind the alias. The implemented `FontId` remains a host-assigned identity; alias and stack resolution produce it rather than leaking file paths into documents.

Fallback is required. A stack such as Latin face, CJK face, emoji face, then a system or bundled sans resolves per shaping run, or per cluster when a run lacks coverage. Latin, CJK, and emoji coverage must not depend on one large font. Missing-glyph results and the selected face are observable for diagnostics.

OpenType variable fonts are in scope. A face and its variation instance (axis values such as `wght`, `wdth`, `slnt`, `opsz`) are separate identities, and the instance belongs in shaping and glyph cache keys. Emoji arrives in stages: monochrome fallback first, then COLR/CPAL, bitmap color, and SVG-in-OpenType glyphs as separate evidence.

## Proposed deterministic font profile

Snapshot and agent-driven tests declare a font profile: fixed redistributable fonts and versions, fixed fallback chain, device scale, shaping options, rasterizer settings, and rendering backend configuration. System fonts are never a CI default. The profile is one of the declared inputs under [determinism](architecture.md#determinism) and the [snapshot bundle](inspection.md#proposed-runner-and-snapshot-bundle).

## Measurement and rendering agreement

Measurement and paint must use compatible shaping and line-break results. Cache keys include the text, font identity/version, relevant style, shaping options, and width constraints. Asset replacement and font fallback changes invalidate both geometry and glyph resources as appropriate.

## Proposed glyph raster strategy

Glyph rendering is hybrid, chosen per use rather than as one global mode:

| Use | Raster mode |
| --- | --- |
| Small text | Grayscale coverage bitmaps, hinted where the face supports it |
| Ordinary and scalable UI text, icons, symbols | MSDF (later MTSDF), preferred over single-channel SDF because it keeps sharp corners |
| Very large or strongly transformed text | MSDF, with an optional vector/path fallback |
| Emoji and color fonts | Color glyphs (bitmap or vector), staged as under [font sources](#proposed-font-sources-and-selection) |

The size threshold between grayscale and MSDF is a tunable policy over device scale, size, face, and hinting quality, not a fixed constant. Grayscale bitmap glyphs come first for ordinary UI sizes; MSDF follows for zoomable canvases, large type, and transformed text.

A text-side glyph manager owns face, metrics, and raster caches and allocates atlas pages per raster mode (grayscale, MSDF, color), allowing several pages per mode. The cache key includes face, glyph ID, raster mode, size or scale class, and variation coordinates. This layer has no Vulkan, WebGPU, or CPU-backend dependency; the renderer receives positioned glyphs with their paint and never interprets Unicode or shaping. GPU backends upload pages and sample them, and the [CPU reference backend](rendering.md#proposed-cpu-reference-backend) samples the same rasters, so both draw the same glyph data.

Glyphs are rasterized on demand; pre-rasterizing whole character sets, such as all CJK glyphs, is excluded. Overflow eviction is LRU- or generation-based. Atlas placement must not affect final pixels: padding and sampling must make a glyph render identically wherever it is placed, so image fixtures do not depend on insertion order. Page allocation, the exact eviction policy, texture arrays or bindless resources, and the interface by which a backend obtains rasters remain open. Glyph resources follow the backend submission/completion lifetime in [rendering](rendering.md); reusing a live atlas region while an earlier frame references it must be prevented.

Text antialiasing is grayscale: coverage on CPUs, distance threshold with derivative width and MSDF median reconstruction on GPUs. Subpixel LCD rendering is not a portable default; see [primitive semantics](rendering.md#proposed-primitive-semantics).

## Incremental delivery

Use the placeholder boundary as a reference, then introduce real font abstraction, shaping, glyph cache, fallback, and wrapping. Measurement and paint must share results throughout. Editing/IME follows the input boundary; expanded scripts or glyph techniques require separate evidence. Scheduled scope is owned by [backlog](../roadmap/backlog.md).

Placeholders must be described as placeholders in examples and the [support matrix](../reference/support-matrix.md). An early menu can use placeholder text while validating geometry and interaction.

## Verification

Test measurement/paint agreement, empty strings, invalid encoding, missing glyphs, fallback boundaries, wrapping, and cache invalidation. Use the pinned redistributable [fixture fonts](../reference/dependencies.md#adopted-text-choices) toward the [deterministic font profile](#proposed-deterministic-font-profile). [Font shaper checks](../../tests/text/font_shaper_tests.cpp) take expected glyph IDs, clusters, and advances from the fixture fonts independently of the shaper. Add a small glyph image regression set; document font versions because changing a font can change layout.
