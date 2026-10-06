#include <tessera/fonts/font_shaper.hpp>
#include <hb-ot.h>
#include <hb.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <unordered_map>

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

// Positions stay in font units (font scale = units per em) until converted per glyph,
// so pen advances accumulate exactly.
struct Face {
    HbFace face;
    HbFont font;
    unsigned upem = 0;
    hb_font_extents_t extents{};
};

std::vector<Diagnostic> invalid_font(std::string message) {
    return {{"invalid_font", Severity::error, "/font", std::move(message), {}}};
}

bool has_table(hb_face_t* face, hb_tag_t tag) {
    HbBlob table(hb_face_reference_table(face, tag));
    return hb_blob_get_length(table.get()) > 0;
}

} // namespace

struct FontShaper::Impl {
    std::unordered_map<std::uint32_t, Face> faces;
    HbBuffer buffer{hb_buffer_create()};
    // A fixed language keeps shaping independent of the process locale.
    hb_language_t language = hb_language_from_string("und", -1);
};

FontShaper::FontShaper() : impl_(std::make_unique<Impl>()) {}
FontShaper::~FontShaper() = default;
FontShaper::FontShaper(FontShaper&&) noexcept = default;
FontShaper& FontShaper::operator=(FontShaper&&) noexcept = default;

std::vector<Diagnostic> FontShaper::set_face(FontId id, std::vector<std::byte> data, std::uint32_t face_index) {
    if (data.empty()) return invalid_font("Font data is empty.");
    if (data.size() > UINT_MAX) return invalid_font("Font data exceeds 4 GiB.");
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

    Face face{HbFace(hb_face_create(blob.get(), face_index)), {}, 0, {}};
    if (!has_table(face.face.get(), HB_TAG('h', 'e', 'a', 'd')) || hb_face_get_glyph_count(face.face.get()) == 0)
        return invalid_font("The face has no head table or no glyphs.");
    face.upem = hb_face_get_upem(face.face.get());
    face.font.reset(hb_font_create(face.face.get()));
    hb_ot_font_set_funcs(face.font.get());
    hb_font_set_scale(face.font.get(), static_cast<int>(face.upem), static_cast<int>(face.upem));
    hb_font_get_h_extents(face.font.get(), &face.extents);
    if (face.extents.ascender - face.extents.descender <= 0)
        return invalid_font("The face has no positive ascender-to-descender extent.");
    hb_face_make_immutable(face.face.get());
    hb_font_make_immutable(face.font.get());
    impl_->faces.insert_or_assign(id.value, std::move(face));
    return {};
}

bool FontShaper::has_face(FontId id) const { return impl_->faces.contains(id.value); }

Result<GlyphRun> FontShaper::shape(std::string_view utf8, const TextStyle& style) {
    auto errors = validate_text_input(utf8, style);
    const auto found = impl_->faces.find(style.font.value);
    if (found == impl_->faces.end())
        errors.push_back({"unknown_font", Severity::error, "/style/font",
                          "No face is registered for FontId " + std::to_string(style.font.value) + "; call set_face.", {}});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    const Face& face = found->second;
    const double scale = static_cast<double>(style.size) / face.upem;
    const auto& extents = face.extents;
    const float line = style.line_height.value_or(
        static_cast<float>((extents.ascender - extents.descender + extents.line_gap) * scale));
    // Half-leading: the ascender-to-descender extent is centered in each line.
    const auto baseline = static_cast<float>(
        (line - (extents.ascender - extents.descender) * scale) / 2 + extents.ascender * scale);

    GlyphRun run{style.font, style.size, {}, {}};
    run.metrics.line_height = line;
    run.metrics.baseline = baseline;
    hb_buffer_t* buffer = impl_->buffer.get();
    std::int64_t widest = 0;
    std::uint32_t lines = 0;
    std::size_t missing = 0;
    std::uint32_t first_missing = 0;
    for (std::size_t start = 0;;) {
        const auto end = utf8.find('\n', start);
        const auto length = (end == std::string_view::npos ? utf8.size() : end) - start;
        const float pen_y = baseline + static_cast<float>(lines) * line;
        std::int64_t pen = 0;
        if (length > 0) {
            hb_buffer_clear_contents(buffer);
            if (length <= INT_MAX)
                hb_buffer_add_utf8(buffer, utf8.data() + start, static_cast<int>(length), 0, static_cast<int>(length));
            if (length > INT_MAX || !hb_buffer_allocation_successful(buffer))
                return {std::nullopt, {{"text_too_long", Severity::error, "/text",
                                        "A line is too long for the shaping buffer.", {}}}};
            hb_buffer_set_language(buffer, impl_->language);
            hb_buffer_guess_segment_properties(buffer);
            hb_shape(face.font.get(), buffer, nullptr, 0);
            unsigned count = 0;
            const auto* infos = hb_buffer_get_glyph_infos(buffer, &count);
            const auto* positions = hb_buffer_get_glyph_positions(buffer, nullptr);
            for (unsigned i = 0; i < count; ++i) {
                const auto cluster = static_cast<std::uint32_t>(start + infos[i].cluster);
                if (infos[i].codepoint == 0 && missing++ == 0) first_missing = cluster;
                run.glyphs.push_back({infos[i].codepoint, cluster,
                                      {static_cast<float>(static_cast<double>(pen + positions[i].x_offset) * scale),
                                       pen_y - static_cast<float>(positions[i].y_offset * scale)}});
                pen += positions[i].x_advance;
            }
        }
        widest = std::max(widest, pen);
        ++lines;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    run.metrics.size = {static_cast<float>(static_cast<double>(widest) * scale), static_cast<float>(lines) * line};
    run.metrics.lines = lines;
    std::vector<Diagnostic> warnings;
    if (missing > 0)
        warnings.push_back({"missing_glyph", Severity::warning, "/text",
                            std::to_string(missing) + " glyph(s) missing from FontId " +
                                std::to_string(style.font.value) + ", first at UTF-8 byte " +
                                std::to_string(first_missing) + "; they use glyph 0 (.notdef).",
                            {}});
    return {std::move(run), std::move(warnings)};
}

Result<TextMetrics> FontShaper::measure(std::string_view utf8, const TextStyle& style) {
    // Measurement is derived from shaping so the two can never disagree.
    auto shaped = shape(utf8, style);
    if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
    return {shaped.value->metrics, std::move(shaped.diagnostics)};
}

} // namespace tessera
