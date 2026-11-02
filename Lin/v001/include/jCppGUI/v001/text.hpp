// File: include/jCppGUI/v001/text.hpp
// Purpose: One font, one size, one line. TextEngine shapes validated UTF-8 into a TextRun, a coverage
//          bitmap that Canvas blends. The FreeType, HarfBuzz and fontconfig objects stay in src/text.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"
#include "geometry.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t max_caption_bytes = 4096;
inline constexpr std::int32_t max_glyph_side = 512;
inline constexpr std::int32_t max_run_width = 1 << 18;
inline constexpr std::int64_t max_run_area = std::int64_t{64} * 1024 * 1024;
inline constexpr std::size_t max_run_glyphs = 8192;

namespace detail {

// A font's substitution rules can expand one character into many glyphs, so the caption's length does not
// limit the shaped run. This does, before anything adds up or renders its glyphs.
[[nodiscard]] constexpr std::expected<void, Error> check_run_glyphs(std::size_t count) {
    if (count > max_run_glyphs) return make_error(ErrorCode::GlyphTooLarge, "the shaped line has too many glyphs");
    return {};
}

// A glyph bitmap's row stride, limited before it is multiplied by anything.
inline constexpr std::int64_t max_glyph_pitch = std::int64_t{max_glyph_side} * 4;
inline constexpr std::size_t max_glyph_bytes = static_cast<std::size_t>(max_glyph_side) * static_cast<std::size_t>(max_glyph_pitch);

// True when each row holds its pixels and the pitch is within the limit. A negative pitch is refused.
[[nodiscard]] constexpr bool glyph_pitch_fits(std::int64_t pitch, std::int64_t width) noexcept {
    return pitch >= width && width >= 0 && pitch <= max_glyph_pitch;
}

// One rendered glyph, as the font library described it.
struct GlyphBitmap {
    std::span<const std::uint8_t> rows;  // pitch bytes per row
    std::int64_t row_count{};
    std::int64_t column_count{};
    std::int64_t pitch{};
};

}  // namespace detail

// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
[[nodiscard]] constexpr bool valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    const auto byte = [&](std::size_t at) { return static_cast<std::uint8_t>(text[at]); };
    while (i < text.size()) {
        const std::uint8_t lead = byte(i);
        std::size_t continuation = 0;
        std::uint8_t low = 0x80;
        std::uint8_t high = 0xBF;
        if (lead < 0x80) {
            continuation = 0;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            continuation = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuation = 2;
            if (lead == 0xE0) low = 0xA0;
            if (lead == 0xED) high = 0x9F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuation = 3;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;
        } else {
            return false;
        }
        if (text.size() - i <= continuation) return false;
        for (std::size_t k = 1; k <= continuation; ++k) {
            const std::uint8_t next = byte(i + k);
            if (next < low || next > high) return false;
            low = 0x80;
            high = 0xBF;
        }
        i += continuation + 1;
    }
    return true;
}

// One shaped line of text, rasterized to 8-bit coverage, row by row.
class TextRun {
public:
    constexpr TextRun() = default;
    // The coverage must hold exactly one byte per pixel of the size. Anything else aborts.
    constexpr TextRun(Size size, std::vector<std::uint8_t> coverage) : size_{size}, coverage_{std::move(coverage)} {
        require(fits(size_, coverage_.size()), "TextRun coverage does not match its size");
    }

    // True when the byte count is exactly one per pixel. Both sides of a Size are at most 2^31 - 1, so the
    // product fits in 64 bits.
    [[nodiscard]] static constexpr bool fits(Size size, std::size_t bytes) noexcept {
        return std::cmp_equal(std::int64_t{size.width().value_} * std::int64_t{size.height().value_}, bytes);
    }

    // Copies a glyph into a run's coverage, with every position clipped to the run. The glyph's position
    // comes from the font, so it is held in 64 bits and may lie anywhere.
    static constexpr void blit(const detail::GlyphBitmap& glyph, std::int64_t left, std::int64_t top, Size run,
                               std::span<std::uint8_t> out) {
        const std::int64_t run_width = run.width().value_;
        const std::int64_t run_height = run.height().value_;
        for (std::int64_t row = 0; row < glyph.row_count; ++row) {
            const std::int64_t y = top + row;
            if (y < 0 || y >= run_height) continue;
            for (std::int64_t column = 0; column < glyph.column_count; ++column) {
                const std::int64_t x = left + column;
                if (x < 0 || x >= run_width) continue;
                const std::uint8_t value = glyph.rows[static_cast<std::size_t>(row * glyph.pitch + column)];
                std::uint8_t& target = out[static_cast<std::size_t>(y * run_width + x)];
                target = std::max(target, value);
            }
        }
    }

    [[nodiscard]] constexpr Size size() const noexcept { return size_; }

    // Coverage at a point counted from the run's own corner, which the caller has clipped to size().
    [[nodiscard]] constexpr std::uint8_t coverage_at(Point at) const {
        const auto c = static_cast<std::size_t>(at.left().value_);
        const auto r = static_cast<std::size_t>(at.top().value_);
        return coverage_[r * static_cast<std::size_t>(size_.width().value_) + c];
    }

private:
    Size size_{};
    std::vector<std::uint8_t> coverage_{};
};

// What layout needs from text. TextEngine is the real model. A small constexpr model lets the static_assert
// tests run layout and painting at compile time, where the C font libraries cannot go.
template <class S>
concept TextShaper = requires(const S& shaper, std::string_view text) {
    { shaper.shape(text) } -> std::same_as<std::expected<TextRun, Error>>;
    { shaper.line_height() } -> std::same_as<Height>;
};

namespace detail {

// Rounds 26.6 fixed point up to whole pixels, and clamps it to int32_t.
[[nodiscard]] constexpr std::int32_t ceil_pixels(std::int64_t fixed) noexcept {
    return saturate_to<std::int32_t>((fixed + 63) / 64);
}

struct TextEngineImpl;
struct TextEngineImplDeleter {
    void operator()(TextEngineImpl* impl) const noexcept;
};
}  // namespace detail

class TextEngine {
public:
    // Finds the default sans-serif font through fontconfig. A missing font is an Error.
    [[nodiscard]] static std::expected<TextEngine, Error> create();

    // The same engine from a font's bytes, which it copies. Nothing checks that they are a real font, beyond
    // what FreeType and the same checks as create do, because font data is untrusted wherever it comes from.
    [[nodiscard]] static std::expected<TextEngine, Error> from_font_bytes(std::span<const std::uint8_t> font);

    // Refuses over-long captions and invalid UTF-8 before anything is shaped.
    [[nodiscard]] std::expected<TextRun, Error> shape(std::string_view text) const;

    [[nodiscard]] Height line_height() const noexcept;

private:
    explicit TextEngine(std::unique_ptr<detail::TextEngineImpl, detail::TextEngineImplDeleter> impl) noexcept;

    std::unique_ptr<detail::TextEngineImpl, detail::TextEngineImplDeleter> impl_;
};

static_assert(TextShaper<TextEngine>);

}  // namespace jgui::inline v001
