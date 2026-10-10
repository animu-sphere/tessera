# Text

## Implemented boundary

[text.hpp](../../include/tessera/text/text.hpp) defines the abstraction. `TextShaper::measure(utf8, style, constraints)` returns `TextMetrics`; `TextShaper::shape(utf8, style, constraints)` returns a `GlyphRun` whose `metrics` equal `measure` for the same inputs. Constraints default to absent, preserving two-argument calls; implementations override the three-argument signatures. Shapers are borrowed by layout and paint for each call and may cache internally, so the operations are non-const. Results are owned values; no FreeType/HarfBuzz or other implementation type crosses the boundary.

- `TextStyle`: host-assigned `FontId` (0 selects the host default font), finite positive size in logical units per em, optional positive line height (absent selects the shaper default), weight 1-1000.
- `TextConstraints::max_width`: absent preserves LF-only lines; a finite, non-negative logical width enables wrapping according to the shaper's algorithm below. Zero is valid; an indivisible unit under that shaper's policy (a scalar, shaped cluster, or prohibited-break group) wider than the limit remains on one overflowing line. Negative width is `out_of_range`, non-finite width is `invalid_number`, at `/constraints/max_width`. Constraints are call inputs, not serialized or inherited styles.
- `TextMetrics`: widest line advance by `lines * line_height`, first baseline below the top of the text box, line height, and line count. Empty text is one empty line.
- `Glyph`: font-specific glyph ID, cluster as a UTF-8 byte offset into the source, baseline pen position relative to the text box top left, and the concrete `FontId` whose face the ID indexes. Alias selection or fallback can differ from the run's `font` (the requested `TextStyle::font`), so a run can mix faces.
- Failures return no value and diagnostics: `invalid_utf8` at `/text`, `text_too_long` above 4 GiB, and style errors (`invalid_number`/`out_of_range`) under `/style`. `validate_text_input` performs these shared checks for every shaper; an implementation may add its own failures.

`PlaceholderTextShaper` uses no font data. Each Unicode scalar advances 0.5 em, LF starts a new line and emits no glyph, the default line height is 1.25 em, and a 0.8 em ascent is centered in each line. With a width constraint it starts a new line before the next scalar would overflow a non-empty line; one oversized scalar stays on the line. This placeholder does not combine graphemes. Glyph IDs are Unicode scalar values, and every glyph names the run's font. It derives `measure` from `shape`, so the two cannot disagree. Its widths are not representative of any real font, including for Japanese text.

Both shapers preserve whitespace and explicit empty/trailing LF lines, reset horizontal pens at each visual line, and keep clusters relative to the whole source text. [Layout](layout.md#implemented-prototype-algorithm) supplies the assigned content width, and paint shapes with that same width.

The optional Vulkan backend can explicitly enable [placeholder Text rasterization](rendering.md#implemented-vulkan-placeholder-text) for default-font runs. That contract owns its bitmap shapes, placement, missing marks, and submission rules. It does not change shaping metrics or establish real font/script coverage.

## Implemented font shaper

[font_shaper.hpp](../../modules/fonts/include/tessera/fonts/font_shaper.hpp) defines `FontShaper`, a `TextShaper` in the optional `tessera::fonts` module that shapes with HarfBuzz under the [adoption record](../reference/dependencies.md#adopted-text-choices). HarfBuzz types stay inside the module, and a core-only build does not discover HarfBuzz.

- Faces: `set_face(font, bytes, face_index)` registers or replaces the one face used for a concrete `FontId`, including the default `FontId` 0, from scalable TTF/OTF bytes or one face of a TTC/OTC collection. The host supplies and loads the bytes through its [asset boundary](rendering.md#assets); the service owns its copy and does no filesystem access; the same bytes serve shaping and rasterization. Empty data, data rejected by either library, an out-of-range face index, a face without `head` or glyphs, non-positive ascender-to-descender extent, a non-scalable face, or inconsistent glyph count/units per em fail as `invalid_font` at `/font` and leave the previous face and revision in place. Data must fit both HarfBuzz's unsigned length and FreeType's signed `long` length. A logical alias ID is rejected as `font_id_conflict` at `/font`. Replacing a face changes later results and its revision; the host lays out and paints again. Shaping a `FontId` with neither a face nor an alias fails as `unknown_font` at `/style/font`.
- Fallback: `set_fallback(font, fallbacks)` sets the ordered `FontId`s tried for clusters the face of `font` cannot map; an empty list clears it. Each entry needs a registered face and may appear once, excluding `font`: a missing face is `unknown_font` at `/font` or `/fallback/<i>`, and a repeat is `duplicate_font` at `/fallback/<i>`; a rejected stack leaves the previous one in place. Stacks are not transitive, so a fallback's own stack is not consulted. After a line is shaped with the requested face, each fallback face in order reshapes every maximal byte range of clusters that still contain glyph 0, in the line's direction with the rest of the line as shaping context, and its glyphs replace that range. A face that maps none of a range still replaces it with its own glyph 0, which the next face reshapes. Shaping across a fallback boundary (kerning, ligatures) does not occur, and each glyph names the `FontId` that supplied it. Replacing a fallback face changes later results.
- Shaping: each visual line is one HarfBuzz segment with default features (kerning, ligatures, and mark positioning when the font provides them), direction and script guessed from the line, and the fixed language `und`, so the process locale does not affect results. LF emits no glyph. Bidirectional reordering, script itemization, and a declared locale are not implemented.
- Wrapping: without constraints, visual lines are the LF-separated lines. With `max_width`, shape the remaining LF segment through the fallback stack. If its float advance fits, emit it. Otherwise classify shaped cluster boundaries using the [menu wrapping profile](#implemented-menu-wrapping-profile), skip prohibited boundaries, and shape successive permitted prefixes as independent lines through the same stack until the first overflow. Emit the last fitting preferred prefix; if none exists, emit the last fitting emergency prefix; if neither exists, emit the first permitted prefix even when oversized. Repeat with the remaining text. This preserves shaped ligatures and base/mark clusters; kerning and context never extend across a selected wrap. Width comparisons use the same float representation as `TextMetrics::size.width`, so supplying an exact measured width does not introduce a rounding-induced wrap. A constrained segment whose guessed direction is not LTR fails as `unsupported_wrapping_direction` at `/text`, even when it fits. Candidate reshaping can take quadratic work; no throughput claim is made.
- Glyphs: glyph IDs are the face's glyph indices. Clusters are UTF-8 byte offsets into the whole text, at HarfBuzz's default grapheme-monotone level, so a ligature or a mark shares the cluster of the first byte of its source. Pen positions accumulate in font units per face and are scaled by each face's `size / units-per-em` per glyph, so measurement involves no accumulated rounding even when a line mixes faces; glyph offsets are added to the pen, with HarfBuzz's upward Y offset converted to the downward box axis.
- Metrics: the selected primary face's HarfBuzz horizontal font extents (OS/2 typographic values when the font sets `USE_TYPO_METRICS`, otherwise `hhea`). The default line height is ascender minus descender plus line gap; the ascender-to-descender extent is centered in each line (half-leading) for both default and explicit line heights. Fallback faces do not change line height or baseline, so line metrics do not depend on which faces a line uses; their glyphs sit on the primary face's baseline. Width is the widest line's advance, as in the boundary above.
- Missing glyphs: a cluster that no face in the stack can map shapes as glyph 0 (`.notdef`) of the last face tried, the last fallback when a stack exists, with that glyph's advance. The result keeps its value and adds one `missing_glyph` warning at `/text` with the count and the first missing byte offset in the message; `byte_offset` stays empty because it refers to document sources. Paint relocates the warning under the Text node; layout uses the metrics.
- Limits: one face per concrete `FontId`; direct face requests ignore `weight`, while [logical aliases](#implemented-family-and-logical-alias-selection) select declared family weights. Variable fonts use their default instance. An LF segment longer than HarfBuzz's buffer accepts fails as `text_too_long`. Measurement is derived from shaping, so the two cannot disagree; no result cache exists, so there is nothing to invalidate. Missing-glyph warnings count emitted glyphs only, excluding provisional wrap candidates.

`FontShaper` also implements the grayscale rasterizer below. Real runs use [atlas preparation and image drawing](rendering.md#implemented-frame-local-glyph-atlas) before submission. The [Vulkan placeholder](rendering.md#implemented-vulkan-placeholder-text) contract accepts only placeholder-shaper runs, so a host must not enable it for `FontShaper` output.

### Implemented family and logical-alias selection

[FontShaper](../../modules/fonts/include/tessera/fonts/font_shaper.hpp) owns host-registered families and logical aliases alongside concrete faces. The host loads bytes, declares family weights, resolves an alias name into a `FontId` for C++ styles, and applies changes at update points. No physical path, font-library type, or new serialized field enters core styles or JSON v1.

- **Families.** `set_family(name, faces)` registers or replaces 1..64 `FontFamilyFace` entries, each naming a registered concrete face and a declared weight in 1..1000. Weights must be distinct; the same face can serve multiple weights. Weights are host declarations, not inferred font metadata. Missing faces (including alias IDs) are `unknown_font` at `/faces/<i>/font`; invalid weights are `out_of_range` and repeated weights are `duplicate_font_weight` at `/faces/<i>/weight`. Empty/oversized lists are `out_of_range` at `/faces`. Faces and families cannot be removed.
- **Names.** Family and alias names are exact, case-sensitive UTF-8 of 1..256 bytes with no ASCII controls (U+0000–U+001F or U+007F). Empty/oversized/control-containing names are `invalid_font_name`; malformed encoding is `invalid_utf8`, at `/name` or `/families/<i>`. Family and alias names have separate namespaces. No normalization, system discovery, implicit built-in alias, or filesystem lookup occurs.
- **Aliases.** `set_alias(font, name, families)` registers or rebinds a logical `FontId` and name to an ordered stack of 1..64 distinct registered family names. A `FontId` is either a face or an alias, including ID 0: collision with a face is `font_id_conflict` at `/font`. An existing alias retains its name, and another ID cannot use it (`font_alias_conflict` at `/name`). Empty/oversized stacks are `out_of_range` at `/families`, repeated families are `duplicate_font_family`, and missing families are `unknown_font_family` at `/families/<i>`. `find_alias(name)` returns an optional logical `FontId`; absence is explicit. All rejected registrations preserve prior selection and face revisions.
- **Selection.** Each alias shaping call selects the face with the smallest absolute difference from `TextStyle::weight` in each family; a tie chooses the lower declared weight, independent of registration order. This bounded policy is not CSS weight matching. The first selected face supplies the primary glyphs and metrics. Later distinct selected faces form the existing cluster fallback stack; duplicate selected face IDs are skipped. Concrete-face fallback stacks are ignored for alias requests, so rebinding one alias cannot change another alias's fallback policy. There is no synthetic weight or recursive alias/family expansion.
- **Identity and updates.** `GlyphRun::font` remains the requested logical ID; every `Glyph::font` is the concrete selected or fallback face. Rasterization, `has_face`, and `face_revision` accept/describe concrete faces only; aliases have revision zero and cannot rasterize directly. Family replacement, alias rebinding, or face replacement affects subsequent measure/shape calls. Hosts recompute affected layout/paint; selection changes alone do not alter face revisions or evict unchanged glyph rasters. Already shaped glyph indices still identify their concrete faces after alias rebinding; face replacement requires reshaping under the [raster lifetime contract](#implemented-grayscale-raster-and-cache-contract).

For example, a host registers Latin and Japanese faces, declares `menu-latin` and `menu-cjk` families, then binds logical ID 2 and `ui-sans` to those families. `find_alias("ui-sans")` supplies the inherited C++ style font while glyphs still name concrete IDs 0 and 1. The [native menu](rendering.md#implemented-win32-vulkan-example-host) uses this path. [Numeric fixtures](../../tests/text/font_shaper_tests.cpp) cover weight ties, weighted fallback, rejected updates, independent aliases, rebinding, cache identity, default ID 0, and layout/paint agreement. Italic/stretch matching, variable axes, source/provider metadata, and serialized family syntax remain under the proposal below.

### Implemented menu wrapping profile

The optional [font shaper](../../modules/fonts/font_shaper.cpp) uses this fixed, bounded Latin/Japanese menu profile. It follows selected space, glue, and punctuation principles from [Unicode UAX #14 revision 57](https://www.unicode.org/reports/tr14/tr14-57.html), but does not implement the Unicode line-break algorithm or its property database. The host cannot configure these sets. The core placeholder retains scalar wrapping.

Apply these rules in order at a shaped cluster boundary:

| Boundary | Policy |
| --- | --- |
| End of an LF segment | Always permitted; explicit LF also terminates a prohibited-break group |
| Either adjacent scalar is U+00A0 NBSP, U+202F narrow NBSP, U+2060 word joiner, or U+FEFF | Prohibited |
| Before U+0020 space or U+3000 ideographic space | Prohibited; consecutive spaces stay with the preceding cluster |
| Before a nonstarter from the set below | Prohibited |
| After an opening character below, including intervening U+0020/U+3000 spaces | Prohibited |
| After U+0020/U+3000, ASCII hyphen U+002D, or hyphen U+2010 | Preferred |
| Either adjacent scalar is in the Japanese ranges below | Preferred |
| All other cluster boundaries | Emergency; used when no preferred prefix fits |

Opening characters: `(`, `[`, `{`, `‘`, `“`, `〈`, `《`, `「`, `『`, `【`, `〔`, `〖`, `〘`, `〚`, `（`, `［`, `｛`, `｟`, `｢`.

Nonstarters: `)`, `]`, `}`, `’`, `”`, `〉`, `》`, `」`, `』`, `】`, `〕`, `〗`, `〙`, `〛`, `）`, `］`, `｝`, `｠`, `｣`, `、`, `。`, `，`, `．`, `！`, `？`, `：`, `；`, `・`, `…`, `‥`, `々`, `〻`, `ゝ`, `ゞ`, `ヽ`, `ヾ`, `ー`; small kana `ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶｧｨｩｪｫｯｬｭｮ` and U+31F0–U+31FF; halfwidth prolonged sound mark `ｰ`; and ASCII `!`, `?`, `.`, `,`, `:`, `;`.

Japanese ranges (inclusive): U+3040–U+30FF, U+31F0–U+31FF, U+3400–U+4DBF, U+4E00–U+9FFF, U+F900–U+FAFF, U+FF66–U+FF9F. Classification uses the scalars immediately before/after the byte boundary; opening checks additionally scan back through the two declared spaces. These ranges are fixed profile inputs, not script detection or a font coverage claim.

Whitespace is neither collapsed nor trimmed; it contributes its shaped advance. No character or hyphen is inserted. A word longer than the available width can split at emergency cluster boundaries, but emergency wrapping never overrides a prohibited boundary. Thus `Hello world` at width 80 with the declared 20-unit Noto Sans profile breaks as `Hello ` / `world`; `あい、う` at width 40 breaks as `あ` / `い、` / `う`. At that width, `あ（い）う` has lines `あ` / `（い）` / `う`, with the middle group overflowing at advance 60. Width zero makes progress by emitting at least the first permitted group.

Limits: no complete UAX #14/JIS X 4051 conformance, locale tailoring, supplementary Han classification, hyphenation, tab stops, soft-hyphen/zero-width-space opportunities, Unicode mandatory-break handling beyond LF, or grapheme segmentation independent of shaping. Text that already begins with a nonstarter is retained; no punctuation repair or hanging is attempted. [Numeric fixtures](../../tests/text/font_shaper_tests.cpp) compare expected independent line shapes, fallback faces, source offsets, measurement, and layout/paint placement.

## Implemented grayscale raster and cache contract

[glyph_cache.hpp](../../include/tessera/text/glyph_cache.hpp) defines `GlyphRasterizer`, `GlyphRasterRequest`, owned `GlyphBitmap`, and `GlyphCache` in core, without font-library or backend dependencies. [FontShaper](../../modules/fonts/include/tessera/fonts/font_shaper.hpp) implements the rasterizer in the optional fonts module over the same registered faces as shaping. Operations are single-threaded and non-reentrant; the host applies font changes at update points and reshapes affected runs. A run does not encode a face revision, so old glyph indices must not be submitted against a replaced face.

- **Request and output.** `font` is the actual `Glyph::font`, `glyph` is its face-specific index (0 is valid `.notdef`), and `pixel_size_64` is physical pixels per em in 1/64-pixel units. Its inclusive range is 64 to 32768 (1 to 512 pixels); other sizes are `out_of_range` at `/pixel_size_64`. Hosts check the product's range, then round `run.size * device_scale * 64` to this integer. `GlyphBitmap` owns top-down tightly packed 8-bit linear coverage, width/height in physical pixels, and signed `left`/`top` bearings from the baseline. For logical baseline `(x,y)` and scale `s`, the image rectangle starts at `(x + left/s, y - top/s)` with size `(width/s,height/s)`. Bearings can be negative; they do not change shaped advances or line metrics. Empty glyphs have both dimensions zero and no coverage. Valid coverage is exactly `width * height` bytes, at most 1 MiB; inconsistent or oversized output is `invalid_glyph_bitmap` at `/bitmap`.
- **Raster policy.** FreeType 2.13.3, default variation instance, 72 dpi with the supplied 26.6 size, `FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP`, `FT_RENDER_MODE_NORMAL`, and zero baseline phase. Per-face stem darkening is disabled explicitly, overriding the process default in `FREETYPE_PROPERTIES`. Only outline glyphs are accepted; no embedded strikes, LCD, color/SVG rendering, synthetic weight, or configurable hinting. `FontShaper` construction throws `std::runtime_error` on FreeType initialization failure or a runtime version other than the pinned one. Unknown faces are `unknown_font` at `/font`, invalid indices are `invalid_glyph` at `/glyph`, and FreeType failures, unsupported glyph formats, or outlines beyond the prospective 1 MiB bitmap bound are `glyph_raster_failed` at `/glyph` (size setup failures use `/pixel_size_64`). Coverage is copied before the next FreeType slot operation, so images never borrow a font slot. Raster calls do not change HarfBuzz metrics or glyph positions.
- **Identity and invalidation.** `face_revision(font)` is zero for an unknown face and otherwise a nonzero value changed by each successful `set_face`, never reused within that service. Rejected replacements preserve it; fallback-stack changes do not change individual raster faces. Other `GlyphRasterizer` implementations must change revision on face or raster-policy replacement and never reuse one. `GlyphCache` borrows one rasterizer that must stay alive at a stable address through cache use. Cache keys are font/revision/glyph/pixel size. On a valid request it removes obsolete revisions of that font, then finds or rasterizes the current image; other fonts remain resident.
- **Capacity and lifetime.** `GlyphCacheLimits` defaults to 8 MiB of resident coverage and 4096 entries. Least recently used entries are evicted until both limits permit an insertion; hits become most recent. Empty glyphs count toward the entry limit. Either zero limit disables storage; a valid bitmap larger than the byte budget is returned uncached. `usage()` reports resident coverage bytes and entries; externally retained images are excluded. `clear()` drops all resident entries. `get()` returns `shared_ptr<const GlyphBitmap>` plus preserved diagnostics: images stay valid after eviction, clear, face replacement, and destruction of cache/service. Failures or malformed images are never cached, and a rasterizer returning no value without an error receives `glyph_raster_failed` at `/glyph`. This bounded reference cache uses linear lookup; no throughput claim is made.

Atlas allocation and image-command lowering are a separate [frame-local resource preparation contract](rendering.md#implemented-frame-local-glyph-atlas). Backend integrations retain or copy images through submission completion and prevent reuse of live GPU regions under the [frame lifetime](rendering.md#implemented-frame-contract). Atlas placement cannot alter a glyph's pixels. [Cache checks](../../tests/text/glyph_cache_tests.cpp) exercise injected services without FreeType; [raster checks](../../tests/text/glyph_raster_tests.cpp) use the independent [small image references](../../tests/fixtures/glyphs/README.md).

### Declared grayscale fixture profile

The raster fixtures declare Noto Sans 2.015 as `FontId` 0, Noto Sans JP 2.004 as `FontId` 1, fallback `0 -> [1]`, HarfBuzz 10.2.0 with default features/guessed segment direction and fixed `und`, and the fixed FreeType policy above. Font bytes and hashes are in [dependencies](../reference/dependencies.md#adopted-text-choices); no system fonts participate. Standalone image references use the explicit physical sizes in [reference.json](../../tests/fixtures/glyphs/reference.json). The mixed wrapped paint fixture uses 20 logical units, content width 70.1, and scale 1.25; its diagnostic contact image rounds baseline positions to integer pixels. These are fixture inputs, not a serialized runtime font-profile API or a GPU reference. Broader profile/capture requirements remain [proposed](#proposed-deterministic-font-profile).

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

The baseline is HarfBuzz for shaping and OpenType features and FreeType for rasterization, accepting TTF, OTF, and TTC collections. Both are [adopted](../reference/dependencies.md#adopted-text-choices); both serve the implementation above, with atlas integration and further raster modes following the proposal below. ICU or an equivalent dependency should be adopted only for a demonstrated Unicode requirement. Font discovery/loading goes through the host asset boundary defined in [rendering](rendering.md#assets).

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

Future authored styles refer to families, stacks, and logical aliases rather than physical files. Candidate conventional aliases are `ui-sans`, `ui-serif`, `ui-mono`, `heading`, `body`, `code`, `emoji`, and `cjk`; none is automatically registered. Applications can rebind an alias to their own or licensed fonts without modifying Tessera. The [implemented host-built selection](#implemented-family-and-logical-alias-selection) resolves names to host-assigned `FontId`s and selects declared weights/fallback families during shaping; serialized style syntax and bundled default mappings remain proposals.

Fallback is required. A stack such as Latin face, CJK face, emoji face, then a system or bundled sans resolves per shaping run, or per cluster when a run lacks coverage. Latin, CJK, and emoji coverage must not depend on one large font. Missing-glyph results and the selected face are observable for diagnostics.

OpenType variable fonts are in scope. A face and its variation instance (axis values such as `wght`, `wdth`, `slnt`, `opsz`) are separate identities, and the instance belongs in shaping and glyph cache keys. Emoji arrives in stages: monochrome fallback first, then COLR/CPAL, bitmap color, and SVG-in-OpenType glyphs as separate evidence.

## Implemented in-process font profile

[font_profile.hpp](../../modules/fonts/include/tessera/fonts/font_profile.hpp) defines owned `FontProfile` inputs and a sealed `ProfileFontShaper` in the optional fonts module. Hosts load explicit assets before creation; no filesystem, system-font lookup, renderer, or font-library type enters the interface. This is an in-process contract, not a serialized replay or snapshot format.

- **Declarations.** `FontProfile::version` accepts only `font_profile_version` 1, independently of UI/replay versions. Assets have an exact case-sensitive `id`, a host-declared `version`, and owned bytes. IDs and asset versions are 1..256 UTF-8 bytes without ASCII controls. Bytes are authoritative; a version label is not inferred from or checked against font metadata. Faces bind concrete `FontId`s to declared asset IDs and collection indices; several faces can share an asset. Fallbacks bind concrete faces to ordered face stacks. Families declare weighted concrete faces, and aliases bind logical IDs/names to ordered family stacks under the [selection contract](#implemented-family-and-logical-alias-selection). There is no implicit default face or alias; the host declares ID 0 when using default styles.
- **Bounds and identity.** Assets and faces each require 1..64 entries; fallback, family, and alias declarations allow 0..64. Both total asset bytes and expanded per-face bytes are bounded separately to `max_font_profile_bytes` (256 MiB); empty asset bytes fail. Asset IDs, concrete IDs, fallback owners, family names, alias IDs, and alias names must each be unique, so declaration order cannot silently replace earlier input. A concrete fallback stack has at most 64 entries; existing family/alias bounds apply. `FontProfile` equality compares all declarations, their order, and exact asset bytes; it is not a normalized configuration identity or digest. Unreferenced asset bytes are retained but not parsed as faces.
- **Creation and diagnostics.** `ProfileFontShaper::create(profile)` takes an owned value, validates profile structure, then builds a fresh `FontShaper` in face/fallback/family/alias dependency order. Success publishes both the exact profile and the completed service; failure returns no partial service and cannot alter another service. Unsupported versions are `unsupported_version` at `/version`; bounds are `out_of_range` at their declaration; malformed identifiers preserve `invalid_font_name`/`invalid_utf8`. Duplicate assets are `duplicate_font_asset` at `/assets/<i>/id`, duplicate concrete IDs or fallback owners are `duplicate_font` at their `/font`, duplicate families are `duplicate_font_family` at `/families/<i>/name`, and repeated alias IDs/names are `font_alias_conflict` at `/aliases/<i>`. Missing assets are `unknown_font_asset` at `/faces/<i>/asset`. Font parsing/collection-index errors are `invalid_font` at `/faces/<i>`. Other registration diagnostics keep their codes under their declaration (for example `/families/0/faces/0/font`, `/fallbacks/0/faces/0`, or `/aliases/0/families/0`). FreeType initialization/runtime failures still throw `std::runtime_error` as for `FontShaper`; allocation failures are not converted into validation diagnostics.
- **Sealed lifetime and policy.** The service exposes `profile()` by const reference, `find_alias`, shaping/measurement, concrete-face revisions, and rasterization, with no registration/replacement API. Version 1 fixes the existing `und` shaping language, default variation instance, [menu wrapping](#implemented-menu-wrapping-profile), nearest-weight selection, and [grayscale raster policy](#implemented-grayscale-raster-and-cache-contract); changing these profile policies requires a new profile version. Revisions remain local to each service, not asset identity. Hosts keep the service alive at a stable address while replay sessions or glyph caches borrow it, and move/replace it only before those borrows or after releasing them. Operations remain single-threaded and non-reentrant. The profile and face service each retain their owned input storage; this is not a zero-copy asset API.

Hosts pair the profile with a [prototype replay](replay.md#implemented-prototype-playback) recording and pass the sealed service as its ordinary `TextShaper`; a glyph cache borrows the same service for atlas preparation. [Profile fixtures](../../tests/text/font_profile_tests.cpp) compare separate creations with explicit registration for mixed Latin/Japanese shaping, geometry, paint, semantics/actions, windowless session inspection, concrete rasters, and atlas outputs at controlled scales. The [standalone encoding](#implemented-font-profile-json-v1) below preserves exact assets and mappings. A [Replay v1 recording](replay.md#implemented-prototype-playback) identifies the profile by digest and declares device scale and locale without embedding the profile. Backend configuration, host clocks/readiness transitions, and bundled font-source licensing remain separate host or proposed inputs below.

## Implemented font profile JSON v1

[font_profile_serialization.hpp](../../modules/fonts/include/tessera/fonts/font_profile_serialization.hpp) defines `load_font_profile` and `save_font_profile` in the optional fonts module. This self-contained format serializes `FontProfile`; its version is independent of UI JSON and replay versions. It carries no paths, provider lookup, replay steps, host state, readiness, time, locale, scale, or backend settings. Hosts can restore it and create the ordinary sealed service without discovering external assets.

All fields below are required, including empty optional-declaration arrays. Objects reject unknown and duplicate fields. Numbers must be unsigned 32-bit integers; weights additionally require 1..1000. Strings use the in-process UTF-8 identifier rules. Null, coercion, omitted defaults, and extensions are rejected.

| Object | Fields and values |
| --- | --- |
| Root | `version`: 1; `assets`, `faces`, `fallbacks`, `families`, `aliases`: ordered arrays |
| Asset | `id`: asset identity; `version`: host-declared version; `bytes`: non-empty hex string, two digits per byte |
| Face | `font`: concrete ID; `asset`: asset identity; `face_index`: collection index, explicitly 0 for ordinary faces |
| Fallback | `font`: concrete owner ID; `faces`: ordered concrete ID array |
| Family | `name`: family name; `faces`: ordered weighted-face array |
| Weighted face | `font`: concrete ID; `weight`: declared weight |
| Alias | `font`: logical ID; `name`: logical name; `families`: ordered family-name array |

Saving uses lexically sorted object keys, preserved array order, compact JSON, lowercase hex, the shared binary64 JSON number formatter (including exponent notation for large IDs), and one final LF. Loading accepts either hex case, JSON whitespace/escapes, and equivalent integer number notation. Exact decoded bytes are authoritative, including unreferenced assets; labels and encoding spelling are not content digests. Equal ordered profiles serialize identically; equivalent reordered registrations are not normalized.

Both operations reuse complete profile validation and registration into a fresh temporary shaper. Failure returns no partial value and cannot replace a live service. In-process entry, name, reference, asset-byte, expanded-face-byte, font-parsing, and registration constraints apply. Each declaration/nested stack is at most 64 entries; assets/faces require at least one. Source/output is bounded to `max_serialized_font_profile_bytes` (516 MiB: twice the 256 MiB asset bound plus 4 MiB for escaped declarations). Parsing also bounds nesting to 256 and JSON values to `max_font_profile_json_values` (32768). Decoding checks aggregate bytes before allocating each asset. These input bounds do not guarantee peak memory or throughput: source, parsed strings, decoded bytes, and temporary registrations may coexist.

Load errors carry JSON pointers and UTF-8 byte offsets. Schema errors use `missing_field`, `unknown_field`, or `schema_type`; unsupported versions use `unsupported_version`; malformed hex uses `invalid_font_bytes` at `/assets/<i>/bytes`. Source-size/value-count/nesting limits use `size_limit`, `json_value_limit`, and `depth_limit`; duplicate JSON members use `duplicate_member`. Registration errors retain profile paths, such as `invalid_font` at `/faces/<i>` and `unknown_font_family` at `/aliases/<i>/families/<j>`. Missing-field offsets point to the nearest enclosing object. Save errors have no source offset. Initialization/version and allocation exceptions retain the in-process policy.

[Profile fixtures](../../tests/text/font_profile_tests.cpp) exercise mixed-font round trips, canonical re-encoding, Unicode/escaped names, maximum ID precision, arbitrary unreferenced bytes, malformed schemas/hex, parser/entry bounds, located diagnostics, and registration failures. Restored services reproduce prototype replay geometry/paint/semantics/actions and atlas bytes at declared scales. [Replay JSON v1](replay.md#implemented-replay-json-v1) identifies a profile by the SHA-256 of this canonical encoding rather than embedding it; capture bundles remain separate scope.

## Proposed deterministic font profile

Snapshot and agent-driven tests declare a font profile: fixed redistributable fonts and versions, fixed fallback chain, device scale, shaping options, rasterizer settings, and rendering backend configuration. The [in-process profile](#implemented-in-process-font-profile) supplies owned face/mapping inputs and the sealed service; [font profile JSON v1](#implemented-font-profile-json-v1) preserves those inputs. [Replay JSON v1](replay.md#implemented-replay-json-v1) identifies that profile by digest and declares device scale and locale; shaping and rasterizer settings are fixed by profile version 1. The [offscreen runner](inspection.md#implemented-prototype-offscreen-runner) manifest carries that identity with a declared capture backend; a snapshot bundle still needs to identify both. System fonts are never a CI default. The profile is one of the declared inputs under [determinism](architecture.md#determinism) and the [snapshot bundle](inspection.md#proposed-runner-and-snapshot-bundle).

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
