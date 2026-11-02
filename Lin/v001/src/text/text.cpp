// File: src/text/text.cpp
// Purpose: The default sans-serif font through fontconfig, shaped with HarfBuzz and rasterized with
//          FreeType. Font files are untrusted input, so every value from them is checked before use.
#include "jCppGUI/v001/text.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include "../c_resource.hpp"

namespace jgui::inline v001 {

namespace {

constexpr FT_UInt font_pixel_size = 14;
constexpr FT_Int32 glyph_load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;

using FontConfig = detail::CResource<FcConfig, FcConfigDestroy>;
using FontPattern = detail::CResource<FcPattern, FcPatternDestroy>;
using FtLibrary = detail::CResource<FT_LibraryRec_, FT_Done_FreeType>;
using FtFace = detail::CResource<FT_FaceRec_, FT_Done_Face>;
using HbFont = detail::CResource<hb_font_t, hb_font_destroy>;
using HbBuffer = detail::CResource<hb_buffer_t, hb_buffer_destroy>;

// fontconfig's text type is unsigned char, and FreeType wants char.
std::string to_string(const FcChar8* text) {
    std::string out;
    for (const FcChar8 unit : std::basic_string_view<FcChar8>{text}) out.push_back(static_cast<char>(unit));
    return out;
}

// The font file and face index of the default sans-serif font.
struct FontFile {
    std::string path;
    FT_Long face_index{};
};

std::expected<FontFile, Error> find_default_font(FcConfig* config) {
    static constexpr FcChar8 family[] = {'s', 'a', 'n', 's', '-', 's', 'e', 'r', 'i', 'f', 0};
    FontPattern pattern{FcPatternCreate()};
    if (!pattern) return make_error(ErrorCode::Font, "fontconfig could not create a pattern");
    if (FcPatternAddString(pattern.get(), FC_FAMILY, family) == FcFalse ||
        FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue) == FcFalse)
        return make_error(ErrorCode::Font, "fontconfig could not build the pattern");
    if (FcConfigSubstitute(config, pattern.get(), FcMatchPattern) == FcFalse)
        return make_error(ErrorCode::Font, "fontconfig could not substitute the pattern");
    FcDefaultSubstitute(pattern.get());

    FcResult result = FcResultNoMatch;
    const FontPattern match{FcFontMatch(config, pattern.get(), &result)};
    if (!match || result != FcResultMatch) return make_error(ErrorCode::Font, "no sans-serif font was found");

    FcChar8* file = nullptr;
    if (FcPatternGetString(match.get(), FC_FILE, 0, &file) != FcResultMatch || file == nullptr)
        return make_error(ErrorCode::Font, "the matched font names no file");
    int index = 0;
    if (FcPatternGetInteger(match.get(), FC_INDEX, 0, &index) != FcResultMatch) index = 0;
    return FontFile{to_string(file), index};
}

}  // namespace

namespace detail {

struct TextEngineImpl {
    FontConfig config{};
    std::vector<std::uint8_t> font_bytes{};  // what a face made from memory reads, so it must outlive the face
    FtLibrary library{};
    FtFace face{};
    HbFont font{};
    HbBuffer buffer{};
    std::int32_t ascent{};
    std::int32_t line_height{};
};

void TextEngineImplDeleter::operator()(TextEngineImpl* impl) const noexcept {
    std::default_delete<TextEngineImpl>{}(impl);
}

}  // namespace detail

namespace {

std::expected<void, Error> start_freetype(detail::TextEngineImpl& impl) {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) return make_error(ErrorCode::Font, "FreeType could not start");
    impl.library.reset(library);
    return {};
}

// The rest of starting an engine, whatever the font came from: every check on it, and HarfBuzz.
std::expected<void, Error> finish_engine(detail::TextEngineImpl& impl, std::string_view name) {
    FT_Face face = impl.face.get();
    if (!FT_IS_SCALABLE(face) || FT_Set_Pixel_Sizes(face, 0, font_pixel_size) != 0)
        return make_error(ErrorCode::Font, "the font cannot be scaled: " + std::string{name});

    const std::int64_t ascender = face->size->metrics.ascender;
    const std::int64_t descender = face->size->metrics.descender;
    impl.ascent = detail::ceil_pixels(ascender);
    impl.line_height = sat_add(impl.ascent, detail::ceil_pixels(-descender));
    if (impl.ascent <= 0 || impl.line_height <= 0 || impl.line_height > max_glyph_side)
        return make_error(ErrorCode::Font, "the font's line metrics are out of range: " + std::string{name});

    impl.font.reset(hb_ft_font_create_referenced(face));
    impl.buffer.reset(hb_buffer_create());
    if (!impl.font || hb_buffer_allocation_successful(impl.buffer.get()) == 0)
        return make_error(ErrorCode::Font, "HarfBuzz could not start");
    hb_ft_font_set_load_flags(impl.font.get(), glyph_load_flags);
    return {};
}

}  // namespace

TextEngine::TextEngine(std::unique_ptr<detail::TextEngineImpl, detail::TextEngineImplDeleter> impl) noexcept
    : impl_{std::move(impl)} {}

std::expected<TextEngine, Error> TextEngine::create() {
    std::unique_ptr<detail::TextEngineImpl, detail::TextEngineImplDeleter> impl{
        std::make_unique<detail::TextEngineImpl>().release()};

    impl->config.reset(FcInitLoadConfigAndFonts());
    if (!impl->config) return make_error(ErrorCode::Font, "fontconfig could not load its configuration");
    auto file = find_default_font(impl->config.get());
    if (!file) return std::unexpected(std::move(file.error()));

    if (auto started = start_freetype(*impl); !started) return std::unexpected(std::move(started.error()));
    FT_Face face = nullptr;
    if (FT_New_Face(impl->library.get(), file->path.c_str(), file->face_index, &face) != 0)
        return make_error(ErrorCode::Font, "FreeType could not open the font " + file->path);
    impl->face.reset(face);
    if (auto finished = finish_engine(*impl, file->path); !finished) return std::unexpected(std::move(finished.error()));
    return TextEngine{std::move(impl)};
}

std::expected<TextEngine, Error> TextEngine::from_font_bytes(std::span<const std::uint8_t> font) {
    std::unique_ptr<detail::TextEngineImpl, detail::TextEngineImplDeleter> impl{
        std::make_unique<detail::TextEngineImpl>().release()};
    impl->font_bytes.assign(font.begin(), font.end());
    if (auto started = start_freetype(*impl); !started) return std::unexpected(std::move(started.error()));
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(impl->library.get(), impl->font_bytes.data(), saturate_to<FT_Long>(impl->font_bytes.size()), 0, &face) != 0)
        return make_error(ErrorCode::Font, "FreeType could not read the font");
    impl->face.reset(face);
    if (auto finished = finish_engine(*impl, "from memory"); !finished) return std::unexpected(std::move(finished.error()));
    return TextEngine{std::move(impl)};
}

Height TextEngine::line_height() const noexcept { return Height{impl_->line_height}; }

std::expected<TextRun, Error> TextEngine::shape(std::string_view text) const {
    if (text.size() > max_caption_bytes) return make_error(ErrorCode::TextTooLong, "caption is over 4096 bytes");
    if (!valid_utf8(text)) return make_error(ErrorCode::InvalidUtf8, "caption is not valid UTF-8");

    hb_buffer_t* buffer = impl_->buffer.get();
    hb_buffer_reset(buffer);
    hb_buffer_add_utf8(buffer, text.data(), saturate_to<int>(text.size()), 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(impl_->font.get(), buffer, nullptr, 0);
    if (hb_buffer_allocation_successful(buffer) == 0) return make_error(ErrorCode::Font, "HarfBuzz ran out of memory");

    unsigned int info_count = 0;
    unsigned int position_count = 0;
    const hb_glyph_info_t* info_data = hb_buffer_get_glyph_infos(buffer, &info_count);
    const hb_glyph_position_t* position_data = hb_buffer_get_glyph_positions(buffer, &position_count);
    if (info_count != position_count) return make_error(ErrorCode::Font, "HarfBuzz gave different glyph and position counts");
    if (auto glyphs = detail::check_run_glyphs(info_count); !glyphs) return std::unexpected(std::move(glyphs.error()));
    auto infos = detail::copy_from_c(info_data, info_count, max_run_glyphs);
    if (!infos) return std::unexpected(std::move(infos.error()));
    auto positions = detail::copy_from_c(position_data, position_count, max_run_glyphs);
    if (!positions) return std::unexpected(std::move(positions.error()));

    // At most max_run_glyphs advances, each under 2^31, so the sum is under 2^44 and cannot overflow 64 bits.
    std::int64_t advance_sum = 0;
    for (const hb_glyph_position_t& position : *positions) advance_sum += std::max(position.x_advance, 0);
    const std::int32_t width = detail::ceil_pixels(advance_sum);
    if (width > max_run_width) return make_error(ErrorCode::GlyphTooLarge, "the shaped line is over the width limit");
    const Size size{Width{width}, Height{impl_->line_height}};
    const std::int64_t area = std::int64_t{width} * std::int64_t{impl_->line_height};
    if (area > max_run_area) return make_error(ErrorCode::GlyphTooLarge, "the shaped line is over the area limit");

    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(area), 0);
    FT_Face face = impl_->face.get();
    std::int64_t pen = 0;
    for (std::size_t i = 0; i < infos->size(); ++i) {
        const hb_glyph_position_t& position = positions->at(i);
        if (FT_Load_Glyph(face, infos->at(i).codepoint, glyph_load_flags) != 0)
            return make_error(ErrorCode::Font, "FreeType could not load a glyph");
        const FT_GlyphSlot slot = face->glyph;
        const std::int64_t limit = std::int64_t{max_glyph_side} * 64;
        if (slot->metrics.width > limit || slot->metrics.height > limit)
            return make_error(ErrorCode::GlyphTooLarge, "a glyph is over the size limit");
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0)
            return make_error(ErrorCode::Font, "FreeType could not render a glyph");
        const FT_Bitmap& bitmap = slot->bitmap;
        if (bitmap.width > static_cast<unsigned int>(max_glyph_side) ||
            bitmap.rows > static_cast<unsigned int>(max_glyph_side))
            return make_error(ErrorCode::GlyphTooLarge, "a glyph bitmap is over the size limit");
        if (bitmap.rows != 0 && bitmap.width != 0) {
            if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY ||
                !detail::glyph_pitch_fits(std::int64_t{bitmap.pitch}, std::int64_t{bitmap.width}))
                return make_error(ErrorCode::Font, "a glyph bitmap has an unsupported layout");
            const std::int64_t left = (pen + position.x_offset) / 64 + slot->bitmap_left;
            const std::int64_t top = std::int64_t{impl_->ascent} - position.y_offset / 64 - slot->bitmap_top;
            // The pitch is checked above and the rows earlier, so this product is small.
            auto rows = detail::copy_from_c<std::uint8_t>(
                bitmap.buffer, static_cast<std::size_t>(bitmap.rows) * static_cast<std::size_t>(bitmap.pitch), detail::max_glyph_bytes);
            if (!rows) return std::unexpected(std::move(rows.error()));
            TextRun::blit(detail::GlyphBitmap{*rows, std::int64_t{bitmap.rows}, std::int64_t{bitmap.width}, bitmap.pitch},
                          left, top, size, coverage);
        }
        pen += std::max(position.x_advance, 0);
    }
    return TextRun{size, std::move(coverage)};
}

}  // namespace jgui::inline v001
