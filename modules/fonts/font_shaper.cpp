#include <tessera/fonts/font_shaper.hpp>
#include <hb-ot.h>
#include <hb.h>
#include <algorithm>
#include <climits>
#include <cstdint>
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

// Positions stay in font units (font scale = units per em) until converted per glyph,
// so pen advances accumulate exactly.
struct Face {
    HbFace face;
    HbFont font;
    unsigned upem = 0;
    hb_font_extents_t extents{};
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

} // namespace

struct FontShaper::Impl {
    std::unordered_map<std::uint32_t, Face> faces;
    std::unordered_map<std::uint32_t, std::vector<FontId>> fallbacks;
    HbBuffer buffer{hb_buffer_create()};
    // A fixed language keeps shaping independent of the process locale.
    hb_language_t language = hb_language_from_string("und", -1);

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

Result<GlyphRun> FontShaper::shape(std::string_view utf8, const TextStyle& style) {
    auto errors = validate_text_input(utf8, style);
    const auto found = impl_->faces.find(style.font.value);
    if (found == impl_->faces.end())
        errors.push_back({"unknown_font", Severity::error, "/style/font",
                          "No face is registered for FontId " + std::to_string(style.font.value) + "; call set_face.", {}});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    // Faces cannot be removed, so every FontId in a stack still has one.
    const auto stack = impl_->fallbacks.find(style.font.value);
    const std::vector<FontId> no_fallback;
    const auto& fallbacks = stack == impl_->fallbacks.end() ? no_fallback : stack->second;
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
        const float pen_y = baseline + static_cast<float>(lines) * line;
        if (text.size() > INT_MAX) return too_long();
        const auto length = static_cast<std::uint32_t>(text.size());
        glyphs.clear();
        if (length > 0) {
            if (!impl_->shape_segment(face, style.font, text, 0, length, HB_DIRECTION_INVALID, glyphs))
                return too_long();
            const auto direction = hb_buffer_get_direction(impl_->buffer.get());
            // Each fallback face reshapes the clusters still missing, in the line's direction.
            for (const auto fallback : fallbacks) {
                const auto ranges = missing_ranges(glyphs, length);
                if (ranges.empty()) break;
                const Face& next = impl_->faces.at(fallback.value);
                for (const auto& [begin, finish] : ranges) {
                    std::vector<Shaped> replacement;
                    if (!impl_->shape_segment(next, fallback, text, begin, finish, direction, replacement))
                        return too_long();
                    const auto inside = [begin, finish](const Shaped& glyph) {
                        return glyph.cluster >= begin && glyph.cluster < finish;
                    };
                    const auto first = std::find_if(glyphs.begin(), glyphs.end(), inside);
                    const auto at = glyphs.erase(first, std::find_if_not(first, glyphs.end(), inside));
                    glyphs.insert(at, replacement.begin(), replacement.end());
                }
            }
        }
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
            const auto cluster = static_cast<std::uint32_t>(start + glyph.cluster);
            if (glyph.id == 0 && missing++ == 0) first_missing = cluster;
            run.glyphs.push_back({glyph.id, cluster,
                                  {static_cast<float>(pen_x(glyph.face, glyph.x_offset)),
                                   pen_y - static_cast<float>(glyph.y_offset * (size / glyph.face->upem))},
                                  FontId{glyph.font}});
            pen->second += glyph.advance;
        }
        widest = std::max(widest, pen_x(nullptr, 0));
        ++lines;
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

Result<TextMetrics> FontShaper::measure(std::string_view utf8, const TextStyle& style) {
    // Measurement is derived from shaping so the two can never disagree.
    auto shaped = shape(utf8, style);
    if (!shaped) return {std::nullopt, std::move(shaped.diagnostics)};
    return {shaped.value->metrics, std::move(shaped.diagnostics)};
}

} // namespace tessera
