#include <tessera/fonts/font_shaper.hpp>
#include <hb-ot.h>
#include <hb.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_PARAMETER_TAGS_H
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace tessera {
namespace {

template<class T, void (*destroy)(T*)>
struct HbRelease {
    void operator()(T* object) const { destroy(object); }
};
using HbBlob = std::unique_ptr<hb_blob_t, HbRelease<hb_blob_t, hb_blob_destroy>>;
using HbFace = std::unique_ptr<hb_face_t, HbRelease<hb_face_t, hb_face_destroy>>;
using HbFont = std::unique_ptr<hb_font_t, HbRelease<hb_font_t, hb_font_destroy>>;
using HbBuffer = std::unique_ptr<hb_buffer_t, HbRelease<hb_buffer_t, hb_buffer_destroy>>;

struct FtLibraryRelease { void operator()(FT_Library object) const { FT_Done_FreeType(object); } };
struct FtFaceRelease { void operator()(FT_Face object) const { FT_Done_Face(object); } };
using FtLibrary = std::unique_ptr<FT_LibraryRec_, FtLibraryRelease>;
using FtFace = std::unique_ptr<FT_FaceRec_, FtFaceRelease>;

// Positions stay in font units (font scale = units per em) until converted per glyph,
// so pen advances accumulate exactly.
struct Face {
    HbFace face;
    HbFont font;
    unsigned upem = 0;
    hb_font_extents_t extents{};
    // FreeType borrows the bytes owned by the HarfBuzz blob retained by face/font.
    // Declared last so this face is released before that blob's final reference.
    FtFace raster_face;
    std::uint64_t revision = 0;
};

// One shaped glyph in the font units of its face, with a cluster relative to its line.
struct Shaped {
    const Face* face;
    std::uint32_t font;
    std::uint32_t id;
    std::uint32_t cluster;
    hb_position_t advance;
    hb_position_t x_offset;
    hb_position_t y_offset;
};

std::vector<Diagnostic> invalid_font(std::string message) {
    return {{"invalid_font", Severity::error, "/font", std::move(message), {}}};
}

std::vector<Diagnostic> validate_font_name(std::string_view name, std::string path) {
    if (name.empty() || name.size() > 256)
        return {{"invalid_font_name", Severity::error, std::move(path), "Expected a font name of 1..256 UTF-8 bytes.", {}}};
    auto errors = validate_text_input(name, {});
    for (auto& error : errors) error.path = path;
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        errors.push_back({"invalid_font_name", Severity::error, std::move(path), "Font names cannot contain ASCII controls.", {}});
    return errors;
}

bool has_table(hb_face_t* face, hb_tag_t tag) {
    HbBlob table(hb_face_reference_table(face, tag));
    return hb_blob_get_length(table.get()) > 0;
}

// Byte ranges of a line, in logical order, whose clusters contain glyph 0. Clusters are
// monotone in one shaped segment, so each range is one contiguous span of glyphs.
std::vector<std::pair<std::uint32_t, std::uint32_t>> missing_ranges(const std::vector<Shaped>& glyphs,
                                                                    std::uint32_t length) {
    std::vector<std::pair<std::uint32_t, bool>> clusters;
    for (const auto& glyph : glyphs) {
        if (clusters.empty() || clusters.back().first != glyph.cluster) clusters.push_back({glyph.cluster, false});
        clusters.back().second = clusters.back().second || glyph.id == 0;
    }
    std::sort(clusters.begin(), clusters.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        if (!clusters[i].second) continue;
        const auto end = i + 1 < clusters.size() ? clusters[i + 1].first : length;
        if (!ranges.empty() && ranges.back().second == clusters[i].first)
            ranges.back().second = end;
        else
            ranges.push_back({clusters[i].first, end});
    }
    return ranges;
}

double advance_width(const std::vector<Shaped>& glyphs, double size) {
    std::vector<std::pair<const Face*, std::int64_t>> advances;
    for (const auto& glyph : glyphs) {
        auto at = std::find_if(advances.begin(), advances.end(),
                               [&](const auto& entry) { return entry.first == glyph.face; });
        if (at == advances.end()) at = advances.insert(advances.end(), {glyph.face, 0});
        at->second += glyph.advance;
    }
    double width = 0;
    for (const auto& [face, units] : advances) width += units * (size / face->upem);
    return width;
}

// A bounded menu wrapping profile, not a Unicode line-break implementation. These
// rules are applied only at shaped cluster boundaries. UTF-8 was validated earlier.
struct Scalar {
    std::uint32_t offset;
    char32_t value;
};

std::vector<Scalar> scalars(std::string_view text) {
    std::vector<Scalar> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        const auto lead = static_cast<unsigned char>(text[i++]);
        const unsigned count = lead < 0x80 ? 0 : lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
        char32_t value = count == 0 ? lead : lead & (0x3f >> count);
        for (unsigned j = 0; j < count; ++j)
            value = (value << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
        result.push_back({static_cast<std::uint32_t>(start), value});
    }
    return result;
}

bool contains(std::u32string_view set, char32_t value) { return set.find(value) != std::u32string_view::npos; }
bool break_space(char32_t value) { return value == U' ' || value == U'\u3000'; }
bool glue(char32_t value) { return contains(U"\u00a0\u202f\u2060\ufeff", value); }
bool opening(char32_t value) { return contains(U"([{‘“〈《「『【〔〖〘〚（［｛｟｢", value); }
bool nonstarter(char32_t value) {
    return contains(U")]}’”〉》」』】〕〗〙〛）］｝｠｣、。，．！？：；・…‥々〻ゝゞヽヾー"
                    U"ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶｧｨｩｪｫｯｬｭｮｰ!?.,:;", value) ||
           (value >= 0x31f0 && value <= 0x31ff);
}
bool japanese_character(char32_t value) {
    return (value >= 0x3040 && value <= 0x30ff) || (value >= 0x31f0 && value <= 0x31ff) ||
           (value >= 0x3400 && value <= 0x4dbf) || (value >= 0x4e00 && value <= 0x9fff) ||
           (value >= 0xf900 && value <= 0xfaff) || (value >= 0xff66 && value <= 0xff9f);
}

enum class Break { prohibited, emergency, preferred };

Break line_break(const std::vector<Scalar>& text, std::uint32_t offset) {
    const auto right = std::lower_bound(text.begin(), text.end(), offset,
                                        [](const Scalar& scalar, std::uint32_t at) { return scalar.offset < at; });
    if (right == text.end()) return Break::preferred; // End of the LF segment always terminates it.
    if (right == text.begin() || right->offset != offset) return Break::prohibited;
    const auto left = std::prev(right);
    // No emergency split overrides punctuation or explicit no-break characters.
    if (glue(left->value) || glue(right->value) || break_space(right->value) || nonstarter(right->value))
        return Break::prohibited;
    auto before_spaces = left;
    while (break_space(before_spaces->value) && before_spaces != text.begin()) --before_spaces;
    if (opening(before_spaces->value)) return Break::prohibited;
    if (break_space(left->value) || left->value == U'-' || left->value == U'\u2010' ||
        japanese_character(left->value) || japanese_character(right->value)) return Break::preferred;
    return Break::emergency;
}

} // namespace

struct FontShaper::Impl {
    FtLibrary library; // Outlives every raster face (reverse member destruction order).
    std::unordered_map<std::uint32_t, Face> faces;
    std::unordered_map<std::uint32_t, std::vector<FontId>> fallbacks;
    std::unordered_map<std::string, std::vector<FontFamilyFace>> families;
    struct Alias { std::string name; std::vector<std::string> families; };
    std::unordered_map<std::uint32_t, Alias> aliases;
    HbBuffer buffer{hb_buffer_create()};
    // A fixed language keeps shaping independent of the process locale.
    hb_language_t language = hb_language_from_string("und", -1);
    std::uint64_t next_revision = 1;

    Impl() {
        FT_Library initialized = nullptr;
        const auto error = FT_Init_FreeType(&initialized);
        if (error) throw std::runtime_error("FT_Init_FreeType failed: " + std::to_string(error));
        library.reset(initialized);
        FT_Int major = 0, minor = 0, patch = 0;
        FT_Library_Version(library.get(), &major, &minor, &patch);
        if (major != 2 || minor != 13 || patch != 3)
            throw std::runtime_error("FontShaper requires FreeType runtime 2.13.3 for its raster profile.");
    }

    // Shapes line[begin, end) with the rest of the line as context and appends its glyphs; an
    // invalid direction is guessed from the text. Returns false when the buffer cannot hold it.
    bool shape_segment(const Face& face, FontId font, std::string_view line, std::uint32_t begin, std::uint32_t end,
                       hb_direction_t direction, std::vector<Shaped>& out) {
        hb_buffer_t* segment = buffer.get();
        hb_buffer_clear_contents(segment);
        if (line.size() > INT_MAX) return false;
        hb_buffer_add_utf8(segment, line.data(), static_cast<int>(line.size()), begin, static_cast<int>(end - begin));
        if (!hb_buffer_allocation_successful(segment)) return false;
        hb_buffer_set_direction(segment, direction);
        hb_buffer_set_language(segment, language);
        hb_buffer_guess_segment_properties(segment);
        hb_shape(face.font.get(), segment, nullptr, 0);
        unsigned count = 0;
        const auto* infos = hb_buffer_get_glyph_infos(segment, &count);
        const auto* positions = hb_buffer_get_glyph_positions(segment, nullptr);
        for (unsigned i = 0; i < count; ++i)
            out.push_back({&face, font.value, infos[i].codepoint, infos[i].cluster, positions[i].x_advance,
                           positions[i].x_offset, positions[i].y_offset});
        return true;
    }

    bool shape_line(const Face& face, FontId font, const std::vector<FontId>& stack, std::string_view text,
                    std::vector<Shaped>& glyphs, hb_direction_t& direction) {
        glyphs.clear();
        direction = HB_DIRECTION_LTR;
        const auto length = static_cast<std::uint32_t>(text.size());
        if (length == 0) return true;
        if (!shape_segment(face, font, text, 0, length, HB_DIRECTION_INVALID, glyphs)) return false;
        direction = hb_buffer_get_direction(buffer.get());
        for (const auto fallback : stack) {
            const auto ranges = missing_ranges(glyphs, length);
            if (ranges.empty()) break;
            const Face& next = faces.at(fallback.value);
            for (const auto& [begin, finish] : ranges) {
                std::vector<Shaped> replacement;
                if (!shape_segment(next, fallback, text, begin, finish, direction, replacement)) return false;
                const auto inside = [begin, finish](const Shaped& glyph) {
                    return glyph.cluster >= begin && glyph.cluster < finish;
                };
                const auto first = std::find_if(glyphs.begin(), glyphs.end(), inside);
                const auto at = glyphs.erase(first, std::find_if_not(first, glyphs.end(), inside));
                glyphs.insert(at, replacement.begin(), replacement.end());
            }
        }
        return true;
    }
};

FontShaper::FontShaper() : impl_(std::make_unique<Impl>()) {}
FontShaper::~FontShaper() = default;
FontShaper::FontShaper(FontShaper&&) noexcept = default;
FontShaper& FontShaper::operator=(FontShaper&&) noexcept = default;

std::vector<Diagnostic> FontShaper::set_face(FontId id, std::vector<std::byte> data, std::uint32_t face_index) {
    if (impl_->aliases.contains(id.value))
        return {{"font_id_conflict", Severity::error, "/font", "This FontId names a logical alias; use a distinct concrete face FontId.", {}}};
    if (data.empty()) return invalid_font("Font data is empty.");
    if (data.size() > UINT_MAX) return invalid_font("Font data exceeds 4 GiB.");
    if (data.size() > LONG_MAX) return invalid_font("Font data exceeds FreeType's memory-face size limit.");
    // The blob owns the bytes; HarfBuzz releases them with the last face reference.
    auto* bytes = new std::vector<std::byte>(std::move(data));
    HbBlob blob(hb_blob_create(reinterpret_cast<const char*>(bytes->data()), static_cast<unsigned>(bytes->size()),
                               HB_MEMORY_MODE_READONLY, bytes,
                               [](void* owned) { delete static_cast<std::vector<std::byte>*>(owned); }));
    const auto count = hb_face_count(blob.get());
    if (count == 0) return invalid_font("Data is not a TTF, OTF, TTC, or OTC font.");
    if (face_index >= count)
        return invalid_font("Face index " + std::to_string(face_index) + " is out of range; the data has " +
                            std::to_string(count) + " face(s).");

    Face face{HbFace(hb_face_create(blob.get(), face_index)), {}, 0, {}, {}, 0};
    if (!has_table(face.face.get(), HB_TAG('h', 'e', 'a', 'd')) || hb_face_get_glyph_count(face.face.get()) == 0)
        return invalid_font("The face has no head table or no glyphs.");
    face.upem = hb_face_get_upem(face.face.get());
    face.font.reset(hb_font_create(face.face.get()));
    hb_ot_font_set_funcs(face.font.get());
    hb_font_set_scale(face.font.get(), static_cast<int>(face.upem), static_cast<int>(face.upem));
    hb_font_get_h_extents(face.font.get(), &face.extents);
    if (face.extents.ascender - face.extents.descender <= 0)
        return invalid_font("The face has no positive ascender-to-descender extent.");
    FT_Face raster_face = nullptr;
    const auto error = FT_New_Memory_Face(impl_->library.get(), reinterpret_cast<const FT_Byte*>(bytes->data()),
                                        static_cast<FT_Long>(bytes->size()), static_cast<FT_Long>(face_index), &raster_face);
    if (error) return invalid_font("FreeType rejected this face (error " + std::to_string(error) + ").");
    face.raster_face.reset(raster_face);
    if (!FT_IS_SCALABLE(raster_face) || raster_face->units_per_EM != face.upem ||
        raster_face->num_glyphs != static_cast<FT_Long>(hb_face_get_glyph_count(face.face.get())))
        return invalid_font("A scalable outline face with matching shaping/raster glyph indices and units per em is required.");
    // Per-face policy overrides FREETYPE_PROPERTIES without changing process state.
    // CFF stem darkening can otherwise change grayscale pixels even with hinting off.
    FT_Bool stem_darkening = false;
    FT_Parameter parameter{FT_PARAM_TAG_STEM_DARKENING, &stem_darkening};
    if (FT_Face_Properties(raster_face, 1, &parameter))
        return invalid_font("FreeType could not disable stem darkening for this face.");
    if (impl_->next_revision == std::numeric_limits<std::uint64_t>::max())
        return invalid_font("Font face revision space is exhausted; create a new text service.");
    face.revision = impl_->next_revision++;
    hb_face_make_immutable(face.face.get());
    hb_font_make_immutable(face.font.get());
    const auto [at, inserted] = impl_->faces.try_emplace(id.value, std::move(face));
    // Retain the old blob until its FreeType face is destroyed. Memberwise assignment
    // would release the old HarfBuzz references before releasing the borrowing FT face.
    if (!inserted) std::swap(at->second, face);
    return {};
}

bool FontShaper::has_face(FontId id) const { return impl_->faces.contains(id.value); }

std::uint64_t FontShaper::face_revision(FontId id) const {
    const auto at = impl_->faces.find(id.value);
    return at == impl_->faces.end() ? 0 : at->second.revision;
}

Result<GlyphBitmap> FontShaper::rasterize(const GlyphRasterRequest& request) {
    auto errors = validate(request);
    const auto at = impl_->faces.find(request.font.value);
    if (at == impl_->faces.end())
        errors.push_back({"unknown_font", Severity::error, "/font", "Register this FontId with set_face before rasterizing.", {}});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto face = at->second.raster_face.get();
    if (request.glyph >= static_cast<std::uint32_t>(face->num_glyphs))
        return {std::nullopt, {{"invalid_glyph", Severity::error, "/glyph", "Glyph index is outside the registered face.", {}}}};
    const auto failed = [](std::string message, std::string path = "/glyph") -> Result<GlyphBitmap> {
        return {std::nullopt, {{"glyph_raster_failed", Severity::error, std::move(path), std::move(message), {}}}};
    };
    // 72 dpi makes the 26.6 point size equal the requested 26.6 physical pixel size.
    auto error = FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(request.pixel_size_64), 72, 72);
    if (error) return failed("FT_Set_Char_Size failed: " + std::to_string(error), "/pixel_size_64");
    error = FT_Load_Glyph(face, request.glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP);
    if (error) return failed("FT_Load_Glyph failed: " + std::to_string(error));
    const auto slot = face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) return failed("Only scalable outline glyphs are supported.");
    // Bound the prospective bitmap before FreeType allocates it. A control box encloses
    // the outline, so the actual raster cannot exceed this area.
    FT_BBox box{};
    FT_Outline_Get_CBox(&slot->outline, &box);
    const auto width = std::ceil(box.xMax / 64.0) - std::floor(box.xMin / 64.0);
    const auto height = std::ceil(box.yMax / 64.0) - std::floor(box.yMin / 64.0);
    if (width < 0 || height < 0 || width > 1024 * 1024 || height > 1024 * 1024 || width * height > 1024 * 1024)
        return failed("Glyph outline exceeds the 1 MiB raster bound.");
    error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
    if (error) return failed("FT_Render_Glyph failed: " + std::to_string(error));
    const auto& source = slot->bitmap;
    GlyphBitmap bitmap;
    bitmap.left = slot->bitmap_left;
    bitmap.top = slot->bitmap_top;
    if (source.width == 0 || source.rows == 0) return {std::move(bitmap), {}};
    if (source.pixel_mode != FT_PIXEL_MODE_GRAY || source.num_grays != 256 || !source.buffer ||
        std::abs(static_cast<std::int64_t>(source.pitch)) < source.width ||
        static_cast<std::uint64_t>(source.width) * source.rows > 1024 * 1024)
        return failed("FreeType did not produce bounded 8-bit grayscale coverage.");
    bitmap.width = source.width;
    bitmap.height = source.rows;
    bitmap.coverage.resize(static_cast<std::size_t>(bitmap.width) * bitmap.height);
    for (std::uint32_t row = 0; row < bitmap.height; ++row) {
        // Pitch is the signed offset to the next lower scanline; copy without padding.
        const auto* pixels = source.buffer + static_cast<std::ptrdiff_t>(row) * source.pitch;
        std::copy_n(pixels, bitmap.width, bitmap.coverage.data() + static_cast<std::size_t>(row) * bitmap.width);
    }
    return {std::move(bitmap), {}};
}

std::vector<Diagnostic> FontShaper::set_fallback(FontId font, std::vector<FontId> fallbacks) {
    const auto unknown = [](FontId id, std::string path) {
        return Diagnostic{"unknown_font", Severity::error, std::move(path),
                          "No face is registered for FontId " + std::to_string(id.value) + "; call set_face first.", {}};
    };
    std::vector<Diagnostic> errors;
    if (!has_face(font)) errors.push_back(unknown(font, "/font"));
    for (std::size_t i = 0; i < fallbacks.size(); ++i) {
        auto path = "/fallback/" + std::to_string(i);
        const auto earlier = fallbacks.begin() + static_cast<std::ptrdiff_t>(i);
        if (fallbacks[i] == font || std::find(fallbacks.begin(), earlier, fallbacks[i]) != earlier)
            errors.push_back({"duplicate_font", Severity::error, std::move(path),
                              "FontId " + std::to_string(fallbacks[i].value) + " already appears in this stack.", {}});
        else if (!has_face(fallbacks[i]))
            errors.push_back(unknown(fallbacks[i], std::move(path)));
    }
    if (!errors.empty()) return errors;
    if (fallbacks.empty())
        impl_->fallbacks.erase(font.value);
    else
        impl_->fallbacks.insert_or_assign(font.value, std::move(fallbacks));
    return {};
}

std::vector<Diagnostic> FontShaper::set_family(std::string name, std::vector<FontFamilyFace> faces) {
    auto errors = validate_font_name(name, "/name");
    if (faces.empty() || faces.size() > 64) {
        errors.push_back({"out_of_range", Severity::error, "/faces", "Expected 1..64 weighted concrete faces.", {}});
        return errors;
    }
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto path = "/faces/" + std::to_string(i);
        if (!has_face(faces[i].font))
            errors.push_back({"unknown_font", Severity::error, path + "/font", "Register this concrete face with set_face first.", {}});
        if (faces[i].weight < 1 || faces[i].weight > 1000)
            errors.push_back({"out_of_range", Severity::error, path + "/weight", "Expected a declared weight in [1, 1000].", {}});
        const auto earlier = faces.begin() + static_cast<std::ptrdiff_t>(i);
        if (std::any_of(faces.begin(), earlier, [&](const auto& entry) { return entry.weight == faces[i].weight; }))
            errors.push_back({"duplicate_font_weight", Severity::error, path + "/weight", "Each family weight must appear once.", {}});
    }
    if (!errors.empty()) return errors;
    impl_->families.insert_or_assign(std::move(name), std::move(faces));
    return {};
}

std::vector<Diagnostic> FontShaper::set_alias(FontId font, std::string name, std::vector<std::string> families) {
    auto errors = validate_font_name(name, "/name");
    if (has_face(font))
        errors.push_back({"font_id_conflict", Severity::error, "/font", "This FontId names a concrete face; use a distinct alias FontId.", {}});
    const auto previous = impl_->aliases.find(font.value);
    if (previous != impl_->aliases.end() && previous->second.name != name)
        errors.push_back({"font_alias_conflict", Severity::error, "/name", "A logical FontId keeps its original alias name.", {}});
    const auto named = find_alias(name);
    if (named && *named != font)
        errors.push_back({"font_alias_conflict", Severity::error, "/name", "This alias name already belongs to another FontId.", {}});
    if (families.empty() || families.size() > 64) {
        errors.push_back({"out_of_range", Severity::error, "/families", "Expected 1..64 distinct registered families.", {}});
        return errors;
    }
    for (std::size_t i = 0; i < families.size(); ++i) {
        auto path = "/families/" + std::to_string(i);
        auto invalid = validate_font_name(families[i], path);
        errors.insert(errors.end(), std::make_move_iterator(invalid.begin()), std::make_move_iterator(invalid.end()));
        const auto earlier = families.begin() + static_cast<std::ptrdiff_t>(i);
        if (std::find(families.begin(), earlier, families[i]) != earlier)
            errors.push_back({"duplicate_font_family", Severity::error, std::move(path), "Each family must appear once in an alias stack.", {}});
        else if (!impl_->families.contains(families[i]))
            errors.push_back({"unknown_font_family", Severity::error, std::move(path), "Register this family with set_family first.", {}});
    }
    if (!errors.empty()) return errors;
    impl_->aliases.insert_or_assign(font.value, Impl::Alias{std::move(name), std::move(families)});
    return {};
}

std::optional<FontId> FontShaper::find_alias(std::string_view name) const {
    for (const auto& [id, alias] : impl_->aliases)
        if (alias.name == name) return FontId{id};
    return std::nullopt;
}

Result<GlyphRun> FontShaper::shape(std::string_view utf8, const TextStyle& style, const TextConstraints& constraints) {
    auto errors = validate_text_input(utf8, style, constraints);
    FontId primary = style.font;
    std::vector<FontId> fallbacks;
    const auto alias = impl_->aliases.find(style.font.value);
    if (alias != impl_->aliases.end()) {
        std::vector<FontId> selected;
        for (const auto& name : alias->second.families) {
            const auto& family = impl_->families.at(name);
            const auto best = std::min_element(family.begin(), family.end(), [&](const auto& a, const auto& b) {
                const auto da = std::abs(static_cast<int>(a.weight) - style.weight);
                const auto db = std::abs(static_cast<int>(b.weight) - style.weight);
                return da < db || (da == db && a.weight < b.weight);
            });
            if (std::find(selected.begin(), selected.end(), best->font) == selected.end()) selected.push_back(best->font);
        }
        primary = selected.front();
        fallbacks.assign(std::next(selected.begin()), selected.end());
    } else {
        const auto stack = impl_->fallbacks.find(primary.value);
        if (stack != impl_->fallbacks.end()) fallbacks = stack->second;
    }
    const auto found = impl_->faces.find(primary.value);
    if (found == impl_->faces.end())
        errors.push_back({"unknown_font", Severity::error, "/style/font",
                          "No face or alias is registered for FontId " + std::to_string(style.font.value) + "; call set_face or set_alias.", {}});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    // Faces/families cannot be removed. Alias selection changes neither face revisions
    // nor raster identities; every glyph records the concrete face that supplied it.
    // The primary face alone sets line metrics, so they do not depend on which faces a line uses.
    const Face& face = found->second;
    const double size = style.size;
    const double scale = size / face.upem;
    const auto& extents = face.extents;
    const float line = style.line_height.value_or(
        static_cast<float>((extents.ascender - extents.descender + extents.line_gap) * scale));
    // Half-leading: the ascender-to-descender extent is centered in each line.
    const auto baseline = static_cast<float>(
        (line - (extents.ascender - extents.descender) * scale) / 2 + extents.ascender * scale);

    GlyphRun run{style.font, style.size, {}, {}};
    run.metrics.line_height = line;
    run.metrics.baseline = baseline;
    const auto too_long = [] {
        return Result<GlyphRun>{std::nullopt, {{"text_too_long", Severity::error, "/text",
                                                "A line is too long for the shaping buffer.", {}}}};
    };
    std::vector<Shaped> glyphs;
    std::vector<std::pair<const Face*, std::int64_t>> pens;
    double widest = 0;
    std::uint32_t lines = 0;
    std::size_t missing = 0;
    std::uint32_t first_missing = 0;
    for (std::size_t start = 0;;) {
        const auto end = utf8.find('\n', start);
        const auto text = utf8.substr(start, (end == std::string_view::npos ? utf8.size() : end) - start);
        if (text.size() > INT_MAX) return too_long();
        const auto length = static_cast<std::uint32_t>(text.size());
        std::uint32_t consumed = 0;
        do {
            const auto remaining = text.substr(consumed);
            hb_direction_t direction;
            if (!impl_->shape_line(face, primary, fallbacks, remaining, glyphs, direction)) return too_long();
            if (constraints.max_width && direction != HB_DIRECTION_LTR)
                return {std::nullopt, {{"unsupported_wrapping_direction", Severity::error, "/text",
                                        "Width-constrained wrapping currently requires left-to-right text.", {}}}};
            auto taken = static_cast<std::uint32_t>(remaining.size());
            if (constraints.max_width && static_cast<float>(advance_width(glyphs, size)) > *constraints.max_width) {
                // Only shaped cluster boundaries can split a line. Shape each candidate as its
                // own line so kerning/ligatures never include text beyond a selected break.
                std::vector<std::uint32_t> boundaries;
                for (const auto& glyph : glyphs)
                    if (glyph.cluster > 0 && (boundaries.empty() || boundaries.back() != glyph.cluster))
                        boundaries.push_back(glyph.cluster);
                boundaries.push_back(taken);
                const auto characters = scalars(remaining);
                std::vector<Shaped> accepted;
                std::vector<Shaped> preferred;
                std::uint32_t preferred_end = 0;
                for (const auto boundary : boundaries) {
                    const auto opportunity = line_break(characters, boundary);
                    if (opportunity == Break::prohibited) continue;
                    std::vector<Shaped> candidate;
                    if (!impl_->shape_line(face, primary, fallbacks, remaining.substr(0, boundary),
                                           candidate, direction)) return too_long();
                    const bool overflow = static_cast<float>(advance_width(candidate, size)) > *constraints.max_width;
                    if (overflow && !accepted.empty()) break;
                    taken = boundary;
                    accepted = std::move(candidate);
                    if (overflow) break; // One indivisible group still makes progress.
                    if (opportunity == Break::preferred) {
                        preferred_end = boundary;
                        preferred = accepted;
                    }
                }
                if (preferred_end != 0) {
                    taken = preferred_end;
                    glyphs = std::move(preferred);
                } else {
                    glyphs = std::move(accepted);
                }
            }
            const float pen_y = baseline + static_cast<float>(lines) * line;
            // Pens accumulate per face in font units and are scaled per glyph, so measurement
            // involves no accumulated rounding even when a line mixes faces.
            pens.clear();
            const auto pen_x = [&pens, size](const Face* current, std::int64_t offset) {
                double x = 0;
                for (const auto& [owner, units] : pens)
                    x += static_cast<double>(units + (owner == current ? offset : 0)) * (size / owner->upem);
                return x;
            };
            for (const auto& glyph : glyphs) {
                auto pen = std::find_if(pens.begin(), pens.end(), [&](const auto& entry) { return entry.first == glyph.face; });
                if (pen == pens.end()) pen = pens.insert(pens.end(), {glyph.face, 0});
                const auto cluster = static_cast<std::uint32_t>(start + consumed + glyph.cluster);
                if (glyph.id == 0 && missing++ == 0) first_missing = cluster;
                run.glyphs.push_back({glyph.id, cluster,
                                      {static_cast<float>(pen_x(glyph.face, glyph.x_offset)),
                                       pen_y - static_cast<float>(glyph.y_offset * (size / glyph.face->upem))},
                                      FontId{glyph.font}});
                pen->second += glyph.advance;
            }
            widest = std::max(widest, pen_x(nullptr, 0));
            ++lines;
            consumed += taken;
        } while (consumed < length);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    run.metrics.size = {static_cast<float>(widest), static_cast<float>(lines) * line};
    run.metrics.lines = lines;
    std::vector<Diagnostic> warnings;
    if (missing > 0)
        warnings.push_back({"missing_glyph", Severity::warning, "/text",
                            std::to_string(missing) + " glyph(s) missing from FontId " +
                                std::to_string(style.font.value) + (fallbacks.empty() ? "" : " and its fallback stack") +
                                ", first at UTF-8 byte " + std::to_string(first_missing) +
                                "; they use glyph 0 (.notdef).",
                            {}});
    return {std::move(run), std::move(warnings)};
}

Result<TextMetrics> FontShaper::measure(std::string_view utf8, const TextStyle& style, const TextConstraints& constraints) {
    // Measurement is derived from shaping so the two can never disagree.
    auto shaped = shape(utf8, style, constraints);
    if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
    return {shaped.value->metrics, std::move(shaped.diagnostics)};
}

} // namespace tessera
