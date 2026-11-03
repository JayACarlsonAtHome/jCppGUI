// File: tests/test_constexpr.cpp
// Purpose: Compile-time tests. Constant evaluation rejects undefined behaviour, including overflow,
//          out-of-bounds access, reading a destroyed object and leaks, but only on the paths it actually
//          evaluates. So these run edge cases through the real code: this file compiling is the test.
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "help_ui.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "test_access.hpp"

// Metrics the concept must refuse. Only the concept looks at them, so they are outside the anonymous
// namespace below: a compiler rightly reports unused fields of a type with internal linkage.
namespace refused_metrics {
struct NoCloseSide {
    static constexpr std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
};
struct NotConstant {
    static inline std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;
};
struct WrongFieldType {
    static constexpr std::int64_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;
};
struct NoEdge {
    static constexpr std::int32_t edge_width = 0;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;
};
struct BoxTallerThanStrip {
    static constexpr std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 10;
    static constexpr std::int32_t close_side = 16;
};
struct CornerShorterThanEdge {
    static constexpr std::int32_t edge_width = 14;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;
};
// A shaper only a concept looks at, for the same reason.
struct UnusedShaper {
    [[nodiscard]] constexpr std::expected<jgui::TextRun, jgui::Error> shape(std::string_view) const { return jgui::TextRun{}; }
    [[nodiscard]] constexpr jgui::Height line_height() const noexcept { return jgui::Height{}; }
};
}  // namespace refused_metrics

namespace {

using namespace jgui;

constexpr std::int32_t int_max = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t int_min = std::numeric_limits<std::int32_t>::min();

// --- Checked arithmetic ---
static_assert(sat_add(int_max, 1) == int_max && sat_add(int_max, int_max) == int_max);
static_assert(sat_add(int_min, -1) == int_min && sat_add(int_min, int_min) == int_min);
static_assert(sat_add(int_max, int_min) == -1 && sat_add(2, 3) == 5);
static_assert(sat_sub(int_min, 1) == int_min && sat_sub(int_max, -1) == int_max && sat_sub(int_min, int_min) == 0);
static_assert(sat_sub(0, int_min) == int_max);
static_assert(saturate_to<std::int32_t>(std::int64_t{1} << 40U) == int_max);
static_assert(saturate_to<std::int32_t>(-(std::int64_t{1} << 40U)) == int_min);
static_assert(saturate_to<std::uint32_t>(-1) == 0U);
static_assert(saturate_to<std::int32_t>(std::numeric_limits<std::uint64_t>::max()) == int_max);
static_assert(saturate_to<std::uint8_t>(256) == 255 && saturate_to<std::uint8_t>(255) == 255);
static_assert(detail::ceil_pixels(0) == 0 && detail::ceil_pixels(1) == 1 && detail::ceil_pixels(64) == 1 && detail::ceil_pixels(65) == 2);
static_assert(detail::ceil_pixels(std::int64_t{1} << 50U) == int_max);

// --- Geometry ---
static_assert(Size{Width{-1}, Height{-1}} == Size{Width{0}, Height{0}} && Size{Width{int_min}, Height{5}}.width() == Width{} && Size{Width{5}, Height{int_min}}.empty());
static_assert(Size{Width{int_max}, Height{int_max}}.width() == Width{int_max});
static_assert(Left{int_max} + Width{1} == Right{int_max} && Top{int_max} + Height{1} == Bottom{int_max});
static_assert(Right{int_min} - Width{1} == Left{int_min} && Bottom{int_min} - Height{1} == Top{int_min});
static_assert(Right{int_max} - Left{int_min} == Width{int_max} && Bottom{int_max} - Top{int_min} == Height{int_max}, "the length saturates");
static_assert(Right{int_min} - Left{int_max} == Width{} && Bottom{int_min} - Top{int_max} == Height{}, "and is never negative");
static_assert(Left{3} + Width{4} == Right{7} && Right{7} - Left{3} == Width{4} && Right{7} - Width{4} == Left{3});
static_assert(Top{3} + Height{4} == Bottom{7} && Bottom{7} - Top{3} == Height{4} && Bottom{7} - Height{4} == Top{3});
static_assert(Width{3} + Width{4} == Width{7} && Width{3} - Width{4} == Width{} && Width{int_max} + Width{int_max} == Width{int_max});
static_assert(Height{3} + Height{4} == Height{7} && Height{3} - Height{4} == Height{} && Height{int_max} + Height{1} == Height{int_max});
static_assert(Width{-1} == Width{} && Width{int_min} == Width{} && Height{-1} == Height{}, "a length is never negative");
static_assert(left_after(Right{5}) == Left{5} && top_after(Bottom{5}) == Top{5});
static_assert(Point{} == Point{Left{}, Top{}} && Size{}.empty() && Size{Width{1}, Height{}}.empty() && !Size{Width{1}, Height{1}}.empty());

// Each is its own type, with no conversion to or from an integer or each other, in either direction.
template <class A, class B>
concept Interchangeable = std::constructible_from<A, B> || std::constructible_from<B, A> || std::convertible_to<A, B> || std::convertible_to<B, A>;
template <class T>
concept NoLooseIntegers = !std::convertible_to<int, T> && !std::convertible_to<T, int> && !std::constructible_from<int, T> &&
                          !std::constructible_from<T, std::int64_t> && !std::constructible_from<T, std::uint32_t> &&
                          !std::constructible_from<T, bool> && !std::constructible_from<T, double> && std::constructible_from<T, std::int32_t>;
static_assert(NoLooseIntegers<Left> && NoLooseIntegers<Right> && NoLooseIntegers<Top> && NoLooseIntegers<Bottom> && NoLooseIntegers<Width> && NoLooseIntegers<Height>);
static_assert(!Interchangeable<Left, Right> && !Interchangeable<Left, Top> && !Interchangeable<Left, Bottom> && !Interchangeable<Left, Width> && !Interchangeable<Left, Height>);
static_assert(!Interchangeable<Right, Top> && !Interchangeable<Right, Bottom> && !Interchangeable<Right, Width> && !Interchangeable<Right, Height>);
static_assert(!Interchangeable<Top, Bottom> && !Interchangeable<Top, Width> && !Interchangeable<Top, Height>);
static_assert(!Interchangeable<Bottom, Width> && !Interchangeable<Bottom, Height> && !Interchangeable<Width, Height>);
// Only the arithmetic that means something exists.
template <class A, class B>
concept Adds = requires(A a, B b) { a + b; };
template <class A, class B>
concept Subtracts = requires(A a, B b) { a - b; };
template <class A, class B>
concept Compares = requires(A a, B b) { a == b; };
static_assert(!Adds<Left, Left> && !Adds<Left, Right> && !Adds<Left, Top> && !Adds<Left, Height> && !Adds<Right, Width> && !Adds<Width, Left> && !Adds<Width, Height> && !Adds<Top, Width>);
static_assert(!Subtracts<Left, Left> && !Subtracts<Left, Right> && !Subtracts<Left, Width> && !Subtracts<Right, Right> && !Subtracts<Right, Top> && !Subtracts<Width, Height>);
static_assert(!Adds<Left, int> && !Adds<Width, int> && !Subtracts<Right, int> && !Compares<Left, int> && !Compares<Width, int> && !Compares<Left, Right> && !Compares<Width, Height>);
static_assert(!std::constructible_from<Size, int, int> && !std::constructible_from<Size, Height, Width> && !std::constructible_from<Size, Width, Width>);
static_assert(!std::constructible_from<Point, int, int> && !std::constructible_from<Point, Top, Left> && !std::constructible_from<Point, Width, Height>);
static_assert(!std::constructible_from<Rect, Size, Point> && !std::constructible_from<Rect, Left, Top, Width, Height>);
static_assert(Rect{Point{Left{int_max}, Top{int_max}}, Size{Width{10}, Height{10}}}.right() == Right{int_max} && Rect{Point{Left{int_max}, Top{int_max}}, Size{Width{10}, Height{10}}}.bottom() == Bottom{int_max});
static_assert(Rect{Point{Left{int_max - 1}, Top{0}}, Size{Width{10}, Height{10}}}.contains(Point{Left{int_max}, Top{5}}));
static_assert(!Rect{Point{Left{int_max}, Top{0}}, Size{Width{0}, Height{10}}}.contains(Point{Left{int_max}, Top{5}}));
static_assert(Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}.contains(Point{Left{int_min}, Top{int_min}}));
static_assert(!Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}.contains(Point{Left{-1}, Top{-1}}));
static_assert(Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}.contains(Point{Left{-2}, Top{-2}}));
static_assert(!Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.contains(Point{Left{10}, Top{0}}) && !Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.contains(Point{Left{-1}, Top{0}}));
static_assert(!Rect{}.contains(Point{}));
static_assert(Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.intersect(Rect{Point{Left{5}, Top{5}}, Size{Width{10}, Height{10}}}) == Rect{Point{Left{5}, Top{5}}, Size{Width{5}, Height{5}}});
static_assert(Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.intersect(Rect{Point{Left{10}, Top{0}}, Size{Width{10}, Height{10}}}).empty());
static_assert(Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.intersect(Rect{Point{Left{-5}, Top{-5}}, Size{Width{3}, Height{3}}}).empty());
static_assert(Rect{Point{Left{int_max}, Top{int_max}}, Size{Width{int_max}, Height{int_max}}}.intersect(Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}).empty());
static_assert(Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}.intersect(Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}) == Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}});
static_assert(Rect{Point{Left{int_max - 1}, Top{0}}, Size{Width{int_max}, Height{4}}}.intersect(Rect{Point{Left{0}, Top{0}}, Size{Width{int_max}, Height{4}}}) == Rect{Point{Left{int_max - 1}, Top{0}}, Size{Width{1}, Height{4}}});
static_assert(Rect{Point{Left{int_max - 2}, Top{0}}, Size{Width{int_max}, Height{4}}}.intersect(Rect{Point{Left{0}, Top{0}}, Size{Width{int_max}, Height{4}}}) == Rect{Point{Left{int_max - 2}, Top{0}}, Size{Width{2}, Height{4}}});
static_assert(Rect{Point{Left{0}, Top{0}}, Size{Width{4}, Height{4}}}.inset(Width{3}, Height{3}).empty() && Rect{Point{Left{0}, Top{0}}, Size{Width{10}, Height{10}}}.inset(Width{2}, Height{2}) == Rect{Point{Left{2}, Top{2}}, Size{Width{6}, Height{6}}});
static_assert(Rect{Point{Left{int_max}, Top{int_min}}, Size{Width{4}, Height{4}}}.inset(Width{int_max}, Height{int_max}).empty());

// --- UTF-8 ---
static_assert(valid_utf8("") && valid_utf8("Help") && valid_utf8("caf\xC3\xA9"));
static_assert(valid_utf8("\xE2\x82\xAC") && valid_utf8("\xF0\x9F\x98\x80"));
static_assert(valid_utf8("\x7F") && valid_utf8("\xC2\x80") && valid_utf8("\xDF\xBF"));
static_assert(valid_utf8("\xE0\xA0\x80") && valid_utf8("\xED\x9F\xBF") && valid_utf8("\xEE\x80\x80"));
static_assert(valid_utf8("\xF0\x90\x80\x80") && valid_utf8("\xF4\x8F\xBF\xBF"));
static_assert(valid_utf8(std::string_view{"\0", 1}));
static_assert(!valid_utf8("\x80") && !valid_utf8("\xBF") && !valid_utf8("\xFF") && !valid_utf8("\xFE"));
static_assert(!valid_utf8("\xC0\x80") && !valid_utf8("\xC1\xBF"), "overlong two bytes");
static_assert(!valid_utf8("\xE0\x80\x80") && !valid_utf8("\xE0\x9F\xBF"), "overlong three bytes");
static_assert(!valid_utf8("\xF0\x80\x80\x80") && !valid_utf8("\xF0\x8F\xBF\xBF"), "overlong four bytes");
static_assert(!valid_utf8("\xED\xA0\x80") && !valid_utf8("\xED\xBF\xBF"), "surrogates");
static_assert(!valid_utf8("\xF4\x90\x80\x80") && !valid_utf8("\xF5\x80\x80\x80"), "above U+10FFFF");
static_assert(!valid_utf8("\xC3") && !valid_utf8("\xE2\x82") && !valid_utf8("\xF0\x9F\x98"), "truncated");
static_assert(!valid_utf8("a\xC3") && !valid_utf8("\xC3(") && !valid_utf8("\xE2(\xA1"), "bad continuation");

// --- Limits ---
consteval bool refused(Size size) { return !PixelBuffer::byte_size(size).has_value(); }
static_assert(PixelBuffer::byte_size(Size{}).value() == 0);
static_assert(PixelBuffer::byte_size(Size{Width{0}, Height{max_buffer_side}}).value() == 0);
static_assert(PixelBuffer::byte_size(Size{Width{1}, Height{1}}).value() == 4);
static_assert(max_buffer_side == 8192);
static_assert(PixelBuffer::byte_size(Size{Width{max_buffer_side}, Height{max_buffer_side}}).value() == std::size_t{1} << 28U, "256 MiB at most");
static_assert(refused(Size{Width{max_buffer_side + 1}, Height{1}}) && refused(Size{Width{1}, Height{max_buffer_side + 1}}));
static_assert(refused(Size{Width{int_max}, Height{int_max}}) && refused(Size{Width{46341}, Height{46341}}) && refused(Size{Width{65536}, Height{65536}}));
static_assert(max_caption_bytes == 4096 && max_run_width > 0 && max_run_area > 0);

// A shaped run's glyph count, at the limit.
static_assert(max_run_glyphs == 8192);
static_assert(detail::check_run_glyphs(0).has_value() && detail::check_run_glyphs(max_run_glyphs).has_value());
static_assert(detail::check_run_glyphs(max_run_glyphs + 1).error().code == ErrorCode::GlyphTooLarge);
static_assert(!detail::check_run_glyphs(std::numeric_limits<std::size_t>::max()).has_value());
static_assert(std::int64_t{max_run_glyphs} * int_max < std::numeric_limits<std::int64_t>::max() / 2, "what bounds the advance sum");

// A glyph bitmap's pitch, limited before anything multiplies it.
static_assert(detail::glyph_pitch_fits(0, 0) && detail::glyph_pitch_fits(16, 16) && detail::glyph_pitch_fits(20, 16));
static_assert(detail::glyph_pitch_fits(detail::max_glyph_pitch, max_glyph_side));
static_assert(!detail::glyph_pitch_fits(detail::max_glyph_pitch + 1, max_glyph_side), "over the limit");
static_assert(!detail::glyph_pitch_fits(15, 16), "a row shorter than its pixels");
static_assert(!detail::glyph_pitch_fits(-16, 16), "a negative pitch");
static_assert(!detail::glyph_pitch_fits(int_max, int_max) && !detail::glyph_pitch_fits(int_min, 0));
static_assert(detail::max_glyph_bytes == 512U * 2048U);

// A run's coverage must hold one byte per pixel. A violation aborts: see tests/test_text.cpp.
static_assert(TextRun::fits(Size{}, 0) && TextRun::fits(Size{Width{3}, Height{4}}, 12));
static_assert(!TextRun::fits(Size{Width{3}, Height{4}}, 11) && !TextRun::fits(Size{Width{3}, Height{4}}, 13) && !TextRun::fits(Size{}, 1));
static_assert(!TextRun::fits(Size{Width{int_max}, Height{int_max}}, 0));
static_assert(TextRun::fits(Size{Width{int_max}, Height{int_max}}, std::size_t{2147483647} * std::size_t{2147483647}));
static_assert(!TextRun::fits(Size{Width{int_max}, Height{int_max}}, std::numeric_limits<std::size_t>::max()));
static_assert(TextRun{Size{Width{2}, Height{2}}, std::vector<std::uint8_t>(4, 7)}.coverage_at(Point{Left{1}, Top{1}}) == 7);
static_assert(TextRun{Size{}, std::vector<std::uint8_t>{}}.size().empty());

consteval bool buffer_create_checks() {
    std::array<std::uint32_t, 12> storage{};
    const auto accepted = [&storage](Size size, std::int32_t stride) {
        return PixelBuffer::create(storage, size, stride).has_value();
    };
    if (!accepted(Size{Width{3}, Height{4}}, 12)) return false;
    if (!accepted(Size{Width{2}, Height{3}}, 16)) return false;
    if (accepted(Size{Width{3}, Height{5}}, 12)) return false;  // too little storage
    if (accepted(Size{Width{3}, Height{4}}, 8)) return false;   // stride under the width
    if (accepted(Size{Width{3}, Height{4}}, 13)) return false;  // not whole pixels
    if (accepted(Size{Width{3}, Height{4}}, -4)) return false;
    if (accepted(Size{Width{1}, Height{max_buffer_side}}, int_max - 3)) return false;
    return true;
}
static_assert(buffer_create_checks());

// A canvas or a buffer cannot be kept by value, or passed along as a copy.
static_assert(!std::copy_constructible<Canvas> && !std::move_constructible<Canvas> && !std::is_copy_assignable_v<Canvas> && !std::is_move_assignable_v<Canvas>);
static_assert(!std::copy_constructible<PixelBuffer> && !std::move_constructible<PixelBuffer> && !std::is_copy_assignable_v<PixelBuffer> && !std::is_move_assignable_v<PixelBuffer>);

// --- Canvas: any write outside the buffer would stop this compiling, and the guards check the rest ---
consteval bool canvas_stays_inside() {
    constexpr std::size_t total = 8 * 8;
    constexpr std::uint32_t guard = 0xDEADBEEFU;
    std::array<std::uint32_t, total> storage{};
    storage.fill(guard);
    // A 4x4 buffer in the middle of the storage, with rows 8 pixels apart.
    const std::span<std::uint32_t> inner = std::span{storage}.subspan(18);
    const Canvas canvas{PixelBuffer::create(inner, Size{Width{4}, Height{4}}, 32).value()};
    const std::array<Rect, 12> shapes{Rect{Point{Left{int_min}, Top{int_min}}, Size{Width{int_max}, Height{int_max}}}, Rect{Point{Left{int_max}, Top{int_max}}, Size{Width{int_max}, Height{int_max}}},
                                      Rect{Point{Left{-100}, Top{-100}}, Size{Width{100}, Height{100}}},               Rect{Point{Left{4}, Top{0}}, Size{Width{10}, Height{10}}},
                                      Rect{Point{Left{0}, Top{4}}, Size{Width{10}, Height{10}}},                       Rect{Point{Left{-5}, Top{1}}, Size{Width{6}, Height{1}}},
                                      Rect{Point{Left{3}, Top{3}}, Size{Width{int_max}, Height{int_max}}},             Rect{Point{Left{1}, Top{1}}, Size{Width{0}, Height{0}}},
                                      Rect{Point{Left{-1}, Top{-1}}, Size{Width{6}, Height{6}}},                       Rect{Point{Left{int_min}, Top{0}}, Size{Width{int_max}, Height{2}}},
                                      Rect{Point{Left{0}, Top{int_max - 1}}, Size{Width{4}, Height{4}}},               Rect{Point{Left{int_max - 1}, Top{0}}, Size{Width{4}, Height{4}}}};
    for (const Rect& shape : shapes) {
        canvas.fill_rect(shape, Color{1});
        canvas.stroke_rect(shape, Color{2});
        canvas.clipped(Rect{Point{Left{1}, Top{1}}, Size{Width{2}, Height{2}}}).fill_rect(shape, Color{3});
    }
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            const bool in_buffer = y >= 2 && y < 6 && x >= 2 && x < 6;
            if (!in_buffer && storage[y * 8 + x] != guard) return false;
        }
    }
    return true;
}
static_assert(canvas_stays_inside());

consteval bool canvas_clip_and_text() {
    std::array<std::uint32_t, 16> storage{};
    const Canvas canvas{PixelBuffer::create(storage, Size{Width{4}, Height{4}}, 16).value()};
    const TextRun run{Size{Width{4}, Height{4}}, std::vector<std::uint8_t>(16, 255)};
    canvas.clipped(Rect{Point{Left{1}, Top{1}}, Size{Width{2}, Height{2}}}).draw_text(Point{Left{0}, Top{0}}, run, Color{0xFF112233U});
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            const bool inside = x >= 1 && x < 3 && y >= 1 && y < 3;
            if (storage[y * 4 + x] != (inside ? 0xFF112233U : 0U)) return false;
        }
    }
    canvas.draw_text(Point{Left{int_max}, Top{int_max}}, run, Color{1});
    canvas.draw_text(Point{Left{int_min}, Top{int_min}}, run, Color{1});
    canvas.draw_text(Point{Left{-3}, Top{-3}}, run, Color{0xFF000000U});
    return storage[0] == 0xFF000000U && storage[5] == 0xFF112233U;
}
static_assert(canvas_clip_and_text());

// A canvas for a frame draws while that frame is current, its clipped children too. Afterwards it aborts: see
// tests/test_canvas.cpp, because in constant evaluation that would stop this file compiling.
consteval bool canvas_draws_during_its_frame() {
    std::array<std::uint32_t, 4> storage{};
    detail::FrameLedger ledger;
    const auto generation = ledger.begin();
    const auto buffer = PixelBuffer::create(storage, Size{Width{2}, Height{2}}, 8);
    const Canvas canvas{buffer.value(), ledger, generation};
    canvas.clipped(Rect{Point{}, Size{Width{1}, Height{1}}}).fill_rect(Rect{Point{}, Size{Width{9}, Height{9}}}, Color{7});
    return storage == std::array<std::uint32_t, 4>{7, 0, 0, 0};
}
static_assert(canvas_draws_during_its_frame());

consteval bool glyph_blit_is_clipped() {
    const std::array<std::uint8_t, 4> glyph{10, 20, 30, 40};
    const detail::GlyphBitmap bitmap{glyph, 2, 2, 2};
    std::array<std::uint8_t, 9> out{};
    constexpr std::int64_t huge = std::int64_t{1} << 40U;
    TextRun::blit(bitmap, huge, huge, Size{Width{3}, Height{3}}, out);
    TextRun::blit(bitmap, -huge, -huge, Size{Width{3}, Height{3}}, out);
    if (out != std::array<std::uint8_t, 9>{}) return false;
    TextRun::blit(bitmap, 2, 2, Size{Width{3}, Height{3}}, out);  // only the top-left of the glyph lands
    if (out[8] != 10 || out[7] != 0 || out[5] != 0) return false;
    TextRun::blit(bitmap, -1, -1, Size{Width{3}, Height{3}}, out);  // only the bottom-right lands
    return out[0] == 40 && out[1] == 0 && out[3] == 0;
}
static_assert(glyph_blit_is_clipped());

// --- Pool: the real logic, on the slots that constant evaluation allows ---
template <class T>
using TestPool = Pool<T, std::vector>;

consteval bool pool_basics() {
    TestPool<int> pool;
    const Handle<int> first = pool.emplace(10).value();
    const Handle<int> second = pool.emplace(20).value();
    if (*pool.find(first) != 10 || *pool.find(second) != 20) return false;
    pool.destroy(first);
    if (pool.find(first) != nullptr) return false;  // lookups fail at once
    if (*pool.find(second) != 20) return false;
    pool.destroy(first);  // a second destroy does nothing
    pool.collect();
    if (pool.find(first) != nullptr) return false;
    const Handle<int> reused = pool.emplace(30).value();
    if (pool.slot_count() != 2) return false;       // the slot was reused
    if (pool.find(first) != nullptr) return false;  // and the old handle stays dead
    if (reused == first || *pool.find(reused) != 30) return false;
    pool.destroy(first);  // a stale destroy leaves the new item alone
    pool.collect();
    return *pool.find(reused) == 30;
}
static_assert(pool_basics());

consteval bool pool_stale_and_foreign_handles() {
    TestPool<int> big;
    const Handle<int> far = [&] {
        Handle<int> last = big.emplace(0).value();
        for (int i = 1; i < 4; ++i) last = big.emplace(i).value();
        return last;
    }();
    TestPool<int> small;
    return small.find(far) == nullptr;  // out of range for this pool
}
static_assert(pool_stale_and_foreign_handles());

consteval bool pool_retires_at_max_generation() {
    TestPool<int> pool{2};
    Handle<int> handle = pool.emplace(0).value();  // generation 0
    for (int cycle = 0; cycle < 2; ++cycle) {
        pool.destroy(handle);
        pool.collect();
        handle = pool.emplace(cycle).value();
        if (pool.slot_count() != 1 || pool.retired_count() != 0) return false;
    }
    pool.destroy(handle);  // now at the maximum, generation 2
    pool.collect();
    if (pool.retired_count() != 1) return false;
    const Handle<int> next = pool.emplace(9).value();
    return pool.slot_count() == 2 && pool.find(handle) == nullptr && *pool.find(next) == 9;
}
static_assert(pool_retires_at_max_generation());

consteval bool pool_top_generation_does_not_wrap() {
    TestPool<int> pool{0};
    const Handle<int> handle = pool.emplace(1).value();
    pool.destroy(handle);
    pool.collect();
    return pool.retired_count() == 1 && pool.find(handle) == nullptr;
}
static_assert(pool_top_generation_does_not_wrap());

// A destroyed item's storage stays valid until collect. Reading it after that would not compile.
consteval bool pool_deferred_destruction() {
    TestPool<std::string> pool;
    const Handle<std::string> handle = pool.emplace("a long enough string to be on the heap").value();
    std::string* item = pool.find(handle);
    pool.destroy(handle);
    const bool readable = item->size() > 10;
    pool.collect();
    return readable && pool.find(handle) == nullptr;
}
static_assert(pool_deferred_destruction());

// --- Event queue ---
struct OrderSink {
    std::array<int, 4> seen{};
    std::size_t count{};
    constexpr void on(const CloseEvent&) { seen[count++] = 1; }
    constexpr void on(const ResizeEvent&) { seen[count++] = 2; }
    constexpr void on(const PointerMove&) { seen[count++] = 3; }
    constexpr void on(const PointerButton&) { seen[count++] = 4; }
    constexpr void on(const PointerLeave&) { seen[count++] = 5; }
    constexpr void on(const KeyEvent&) { seen[count++] = 6; }
    constexpr void on(const PointerScroll&) { seen[count++] = 7; }
};

consteval bool queue_keeps_arrival_order() {
    BackendEventQueue queue;
    if (!queue.push(PointerMove{}) || !queue.push(KeyEvent{Key::Space}) || !queue.push(PointerMove{}) || !queue.push(CloseEvent{}))
        return false;
    OrderSink sink;
    queue.drain(sink);
    if (!queue.empty() || sink.seen != std::array<int, 4>{3, 6, 3, 1}) return false;
    if (!queue.push(ResizeEvent{})) return false;
    OrderSink again;
    queue.drain(again);
    return again.seen[0] == 2 && again.count == 1;
}
static_assert(queue_keeps_arrival_order());

// --- Wire values: each meaning is its own type, with no way in or out but the explicit one ---
static_assert(!std::same_as<Serial, PointerButtonCode> && !std::same_as<Serial, Keysym> && !std::same_as<PointerButtonCode, Keysym>);
static_assert(!std::convertible_to<std::uint32_t, Serial> && !std::convertible_to<Serial, std::uint32_t>);
static_assert(!std::convertible_to<std::uint32_t, PointerButtonCode> && !std::convertible_to<PointerButtonCode, std::uint32_t>);
static_assert(!std::convertible_to<std::uint32_t, Keysym> && !std::convertible_to<Keysym, std::uint32_t>);
static_assert(!std::convertible_to<std::uint32_t, ModifierMask> && !std::convertible_to<ModifierMask, std::uint32_t>);
static_assert(!std::constructible_from<std::uint32_t, Serial> && !std::constructible_from<Serial, PointerButtonCode>);
static_assert(jgui_test::serial(5) == jgui_test::serial(5) && jgui_test::serial(5) != jgui_test::serial(6));
// A serial proves that a press happened, so a callback cannot make one, not even a default one.
static_assert(!std::default_initializable<Serial> && !std::constructible_from<Serial, std::uint32_t> && std::copy_constructible<Serial>);
// A button or a key can be named at compile time, but there is no way to turn a run-time number into one.
template <std::uint32_t N>
concept NamesAKey = requires { Keysym{N}; };
static_assert(NamesAKey<0x41> && Keysym{0x41} != Key::Space);
template <class T>
concept RunTimeWire = requires(std::uint32_t number) { T::make(number); };
static_assert(!RunTimeWire<Serial> && !RunTimeWire<Keysym> && !std::default_initializable<Keysym> && !std::default_initializable<PointerButtonCode>);
static_assert(MouseButton::Left != PointerButtonCode{0x111} && Key::Return != Key::Space);
static_assert((Modifier::Shift | Modifier::Ctrl).holds(Modifier::Ctrl) && !(Modifier::Shift | Modifier::Ctrl).holds(Modifier::Alt));
static_assert((Modifier::Shift | Modifier::Ctrl) == (Modifier::Ctrl | Modifier::Shift) && ModifierMask{}.holds(ModifierMask{}));
static_assert(!std::constructible_from<Rect, int, int, int, int> && std::constructible_from<Rect, Point, Size>);

// --- The memory budget: all of a window's frame buffers, and the memory a frame is drawn in ---
constexpr std::size_t mib = std::size_t{1} << 20U;
constexpr std::size_t most_buffers = 4;
consteval detail::FrameRoom room(std::size_t frame, std::size_t held, std::size_t held_count, bool reuses = false) {
    return detail::frame_room(frame, detail::HeldBuffers{held, held_count}, reuses, most_buffers, window_pixel_budget);
}
static_assert(window_pixel_budget == 1024 * mib);
// With nothing held, the largest frame fits, as must anything the size limit allows.
static_assert(room(256 * mib, 0, 0) == detail::FrameRoom::Ready);
// One buffer plus the memory it is drawn in, at the budget and just over. Only then is it Never.
static_assert(room(512 * mib, 0, 0) == detail::FrameRoom::Ready);
static_assert(room(512 * mib + 1, 0, 0) == detail::FrameRoom::Never);
static_assert(room(std::numeric_limits<std::size_t>::max(), 0, 0) == detail::FrameRoom::Never);
static_assert(room(512 * mib + 1, 256 * mib, 1) == detail::FrameRoom::Never, "no release can help");
// Maximum-size frames: two held still leave room, and three do not, until one is released.
static_assert(room(256 * mib, 256 * mib, 1) == detail::FrameRoom::Ready);
static_assert(room(256 * mib, 512 * mib, 2) == detail::FrameRoom::Ready, "exactly at the budget");
static_assert(room(256 * mib, 512 * mib + 1, 2) == detail::FrameRoom::Wait, "one byte over");
static_assert(room(256 * mib, 768 * mib, 3) == detail::FrameRoom::Wait);
// Reusing a buffer still counts it and the memory it is drawn in, so what is held can still make it wait.
static_assert(room(256 * mib, 512 * mib, 2, true) == detail::FrameRoom::Ready);
static_assert(room(256 * mib, 768 * mib, 3, true) == detail::FrameRoom::Wait);
// The count limit waits for a new buffer, and does not stop one that is reused.
static_assert(room(mib, 4 * mib, 4) == detail::FrameRoom::Wait);
static_assert(room(mib, 4 * mib, 4, true) == detail::FrameRoom::Ready);
static_assert(room(mib, 3 * mib, 3) == detail::FrameRoom::Ready);
// A count that cannot be added up safely waits, and does not wrap into room.
static_assert(room(mib, std::numeric_limits<std::size_t>::max(), 1) == detail::FrameRoom::Wait);
// Nothing held never waits, whatever the frame's size, so a wait always has a release to end it.
static_assert(room(mib, 0, 0) == detail::FrameRoom::Ready && room(512 * mib, 0, 0, true) == detail::FrameRoom::Ready);

// --- A frame being drawn. The rule is one FrameLedger, which the Wayland backend shares. ---
consteval bool the_current_frame_works() {
    HeadlessBackend backend{Size{Width{2}, Height{2}}};
    auto frame = backend.acquire_frame(Size{Width{2}, Height{2}});
    if (!frame.has_value() || !backend.frame_pixels(*frame).has_value()) return false;
    if (!backend.present(std::move(*frame), Rect{}).has_value()) return false;
    return backend.presented() == 1;
}
static_assert(the_current_frame_works());

consteval bool a_newer_acquire_invalidates_a_frame() {
    HeadlessBackend backend{Size{Width{2}, Height{2}}};
    auto older = backend.acquire_frame(Size{Width{2}, Height{2}});
    auto newer = backend.acquire_frame(Size{Width{3}, Height{3}});
    if (!older.has_value() || !newer.has_value()) return false;
    if (backend.frame_pixels(*older).error().code != ErrorCode::StaleFrame) return false;
    if (backend.present(std::move(*older), Rect{}).error().code != ErrorCode::StaleFrame) return false;
    if (backend.presented() != 0) return false;
    if (!backend.frame_pixels(*newer).has_value()) return false;
    return backend.present(std::move(*newer), Rect{}).has_value() && backend.presented() == 1;
}
static_assert(a_newer_acquire_invalidates_a_frame());

// The old dangling-buffer path: acquire one size, then fail to acquire another, then use the first.
consteval bool a_failed_acquire_still_invalidates() {
    HeadlessBackend backend{Size{Width{2}, Height{2}}};
    auto older = backend.acquire_frame(Size{Width{2}, Height{2}});
    if (!older.has_value() || backend.acquire_frame(Size{Width{int_max}, Height{int_max}}).has_value()) return false;
    if (backend.frame_pixels(*older).error().code != ErrorCode::StaleFrame) return false;
    return backend.present(std::move(*older), Rect{}).error().code == ErrorCode::StaleFrame && backend.presented() == 0;
}
static_assert(a_failed_acquire_still_invalidates());

consteval bool a_frame_moved_from_is_refused() {
    HeadlessBackend backend{Size{Width{2}, Height{2}}};
    auto frame = backend.acquire_frame(Size{Width{2}, Height{2}});
    if (!frame.has_value()) return false;
    Frame<HeadlessBackend> taken = std::move(*frame);
    if (backend.frame_pixels(*frame).error().code != ErrorCode::StaleFrame) return false;
    if (!backend.frame_pixels(taken).has_value()) return false;
    Frame<HeadlessBackend> again{std::move(taken)};
    if (backend.frame_pixels(taken).has_value()) return false;
    taken = std::move(again);
    if (backend.frame_pixels(again).has_value() || !backend.frame_pixels(taken).has_value()) return false;
    return backend.present(std::move(taken), Rect{}).has_value() && backend.presented() == 1;
}
static_assert(a_frame_moved_from_is_refused());

consteval bool a_presented_frame_ends() {
    HeadlessBackend backend{Size{Width{2}, Height{2}}};
    auto frame = backend.acquire_frame(Size{Width{2}, Height{2}});
    if (!frame.has_value()) return false;
    auto second = backend.acquire_frame(Size{Width{2}, Height{2}});
    if (!second.has_value() || !backend.present(std::move(*second), Rect{}).has_value()) return false;
    return backend.frame_pixels(*second).error().code == ErrorCode::StaleFrame &&
           backend.present(std::move(*frame), Rect{}).error().code == ErrorCode::StaleFrame;
}
static_assert(a_presented_frame_ends());

static_assert(!std::copy_constructible<Frame<HeadlessBackend>> && !std::is_copy_assignable_v<Frame<HeadlessBackend>>);
static_assert(std::move_constructible<Frame<HeadlessBackend>>);
static_assert(!std::constructible_from<Frame<HeadlessBackend>, Frame<HeadlessBackend>&>);

// What a callback gets is the Window, and it cannot change the size, take the requests or reach the backend.
template <class T>
concept CanSetSize = requires(T& window) { window.set_size(Size{}); };
template <class T>
concept CanTakeRequests = requires(T& window) { window.take_requests(); };
template <class T>
concept CanRequest = requires(T& window) { window.request_close(); };
template <class T>
concept HasBackend = requires(T& application) { application.backend(); };
template <class T>
concept CanDispatch = requires(T& window) { window.dispatch(PointerLeave{}); };
template <class T>
concept CanNotify = requires(T& window) { window.notify(PointerLeave{}); };
template <class T>
concept CanClearDamage = requires(T& window) { window.clear_damage(); };
template <class T>
concept CanLayOut = requires(T& window, const refused_metrics::UnusedShaper& text) { window.layout(Rect{}, text); };
template <class T>
concept CanPaint = requires(T& window, Canvas& canvas) { window.paint(canvas); };
template <class T>
concept CanAskDamage = requires(T& window) { window.damaged(); };
template <class T>
concept CanAskCapture = requires(T& window) { window.capture_active(); };
static_assert(!CanSetSize<Window<>> && !CanTakeRequests<Window<>> && CanRequest<Window<>>);
static_assert(!CanDispatch<Window<>> && !CanNotify<Window<>> && !CanClearDamage<Window<>> && !CanLayOut<Window<>> && !CanPaint<Window<>>);
static_assert(!CanAskDamage<Window<>> && !CanAskCapture<Window<>>);
static_assert(!std::copy_constructible<Window<>> && !std::is_copy_assignable_v<Window<>> && !std::is_move_assignable_v<Window<>> && std::move_constructible<Window<>>);
static_assert(!std::default_initializable<TreeKey> && !std::copy_constructible<TreeKey>);
template <class T>
concept TakesEvents = requires(T& application) { application.on(CloseEvent{}); };
static_assert(!TakesEvents<ApplicationCore<HeadlessBackend, Window<>>>);
static_assert(!HasBackend<Window<>> && !HasBackend<Application<HeadlessBackend, Window<>>> && !HasBackend<ApplicationCore<HeadlessBackend, Window<>>>);

// --- Frame ---
using Frame = WindowFrame<DefaultFrameMetrics>;
constexpr Size frame_window{Width{200}, Height{100}};
static_assert(Frame::hit(Point{Left{-1}, Top{0}}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{Left{200}, Top{0}}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{Left{int_max}, Top{int_min}}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{Left{0}, Top{0}}, frame_window).edge == ResizeEdge::TopLeft);
static_assert(Frame::hit(Point{Left{199}, Top{0}}, frame_window).edge == ResizeEdge::TopRight);
static_assert(Frame::hit(Point{Left{0}, Top{99}}, frame_window).edge == ResizeEdge::BottomLeft);
static_assert(Frame::hit(Point{Left{199}, Top{99}}, frame_window).edge == ResizeEdge::BottomRight);
static_assert(Frame::hit(Point{Left{100}, Top{0}}, frame_window).edge == ResizeEdge::TopSide);
static_assert(Frame::hit(Point{Left{100}, Top{99}}, frame_window).edge == ResizeEdge::BottomSide);
static_assert(Frame::hit(Point{Left{0}, Top{50}}, frame_window).edge == ResizeEdge::LeftSide);
static_assert(Frame::hit(Point{Left{199}, Top{50}}, frame_window).edge == ResizeEdge::RightSide);
static_assert(Frame::hit(Point{Left{100}, Top{10}}, frame_window).region == FrameRegion::Title);
static_assert(Frame::hit(Point{Left{100}, Top{50}}, frame_window).region == FrameRegion::Client);
static_assert(Frame::hit(Point{Frame::close_box(frame_window).left(), Top{10}}, frame_window).region == FrameRegion::Close);
static_assert(Frame::client_rect(Size{}).empty() && Frame::client_rect(Size{Width{1}, Height{1}}).empty());
static_assert(Frame::client_rect(Size{Width{int_max}, Height{int_max}}).width() > Width{});
static_assert(Frame::hit(Point{Left{0}, Top{0}}, Size{}).region == FrameRegion::Outside);

// Metrics are compile-time values, each set of them a different type, and every size follows them.
struct ThickMetrics {
    static constexpr std::int32_t edge_width = 8;
    static constexpr std::int32_t corner_length = 20;
    static constexpr std::int32_t title_height = 32;
    static constexpr std::int32_t close_side = 20;
};
using ThickFrame = WindowFrame<ThickMetrics>;
static_assert(FrameMetrics<DefaultFrameMetrics> && FrameMetrics<ThickMetrics>);
static_assert(!std::same_as<Frame, ThickFrame>);
static_assert(Widget<Frame> && Widget<ThickFrame>);
static_assert(Frame::edge_width == 4 && Frame::corner_length == 12 && Frame::title_height == 24 && Frame::close_side == 16);
static_assert(Frame::client_rect(frame_window) == Rect{Point{Left{4}, Top{24}}, Size{Width{192}, Height{72}}});
static_assert(ThickFrame::client_rect(frame_window) == Rect{Point{Left{8}, Top{32}}, Size{Width{184}, Height{60}}});
static_assert(Frame::close_box(frame_window) == Rect{Point{Left{176}, Top{4}}, Size{Width{16}, Height{16}}});
static_assert(ThickFrame::close_box(frame_window) == Rect{Point{Left{166}, Top{6}}, Size{Width{20}, Height{20}}});
static_assert(Frame::hit(Point{Left{6}, Top{50}}, frame_window).region == FrameRegion::Client);
static_assert(ThickFrame::hit(Point{Left{6}, Top{50}}, frame_window).edge == ResizeEdge::LeftSide);
static_assert(Frame::hit(Point{Left{100}, Top{28}}, frame_window).region == FrameRegion::Client);
static_assert(ThickFrame::hit(Point{Left{100}, Top{28}}, frame_window).region == FrameRegion::Title);

using namespace refused_metrics;
static_assert(!FrameMetrics<NoCloseSide> && !FrameMetrics<NotConstant> && !FrameMetrics<WrongFieldType>);
static_assert(!FrameMetrics<NoEdge> && !FrameMetrics<BoxTallerThanStrip> && !FrameMetrics<CornerShorterThanEdge>);
static_assert(!detail::tags_unique<Frame, ThickFrame>(), "a second frame in one window is a duplicate tag");

// --- A shaper for compile time: every byte is a solid block, with the real engine's refusals ---
struct BlockShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text.size() > max_caption_bytes) return make_error(ErrorCode::TextTooLong, "too long");
        if (!valid_utf8(text)) return make_error(ErrorCode::InvalidUtf8, "invalid");
        const Size size{Width{saturate_to<std::int32_t>(text.size() * 2)}, Height{4}};
        return TextRun{size, std::vector<std::uint8_t>(text.size() * 2 * 4, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};
static_assert(TextShaper<BlockShaper>);

// --- A small tree, on compile-time pools ---
struct ClickTag;
struct OtherTag;
struct ItemTag;
struct CountTag;
struct SilentTag;

using Bump = Message<"bump", To<CountTag>>;
using Add = Message<"add", To<CountTag, SilentTag>, int>;
using Quiet = Message<"quiet", To<SilentTag, CountTag>>;

struct Counter {
    using tag = CountTag;
    int value{};
    constexpr int on(Bump) { return ++value; }
    constexpr int on(Add, const int& amount) { return value += amount; }
    constexpr void on(Quiet) { ++value; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return Rect{}; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper&) { return area; }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};

struct Silent {
    using tag = SilentTag;
    int calls{};
    constexpr void on(Add, const int&) { ++calls; }
    constexpr void on(Quiet) { ++calls; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return Rect{}; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper&) { return area; }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};

static_assert(Widget<Counter> && Widget<Silent>);
static_assert(std::same_as<std::tuple_element_t<0, decltype(window(Counter{}, Silent{}).send<Quiet>())>, NoReply>);

consteval bool messages_reach_receivers_in_order() {
    auto ui = window(Counter{}, Silent{});
    auto [bumped] = ui.send<Bump>();
    if (bumped != 1) return false;
    auto [total, nothing] = ui.send<Add>(41);
    static_assert(std::same_as<decltype(nothing), NoReply>);
    if (total != 42 || ui.get<SilentTag>().calls != 1) return false;
    ui.send<Quiet>();
    return ui.get<CountTag>().value == 43 && ui.get<SilentTag>().calls == 2;
}
static_assert(messages_reach_receivers_in_order());

// Receivers are whole widgets, which the tree lays out and paints like any other.
consteval bool receivers_are_widgets() {
    auto ui = window(Counter{}, Silent{});
    const Rect area{Point{Left{0}, Top{0}}, Size{Width{2}, Height{2}}};
    const auto left = jgui_test::layout(ui, area, BlockShaper{});
    const bool passed_through = left.has_value() && *left == area;
    std::array<std::uint32_t, 4> pixels{};
    Canvas canvas{PixelBuffer::create(pixels, area.size(), 8).value()};
    jgui_test::paint(ui, canvas);
    return passed_through && !jgui_test::capture_active(ui) && !jgui_test::damaged(ui) && ui.get<CountTag>().bounds().empty() && ui.get<SilentTag>().bounds().empty() &&
           pixels == std::array<std::uint32_t, 4>{};
}
static_assert(receivers_are_widgets());

constexpr std::string_view caption = "OK";

constexpr auto bump_on_click = [](auto& u) -> void { u.template send<Bump>(); };

consteval bool button_click_rules() {
    auto ui = window(menu_bar(button<ClickTag>(caption, bump_on_click)), Counter{});
    static_cast<void>(jgui_test::layout(ui, Rect{Point{Left{0}, Top{0}}, Size{Width{100}, Height{40}}}, BlockShaper{}));
    auto& target = ui.get<ClickTag>();
    const Point on = jgui_test::inside(target.bounds(), 1, 1);
    const Point off{Left{99}, Top{39}};
    const auto count = [&] { return ui.get<CountTag>().value; };

    jgui_test::dispatch(ui, PointerPress{on, MouseButton::Left, jgui_test::serial(1)});
    if (!target.pressed() || !jgui_test::capture_active(ui)) return false;
    jgui_test::dispatch(ui, PointerRelease{on, MouseButton::Left});
    if (count() != 1 || target.pressed() || jgui_test::capture_active(ui)) return false;

    jgui_test::dispatch(ui, PointerPress{on, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(ui, PointerRelease{off, MouseButton::Left});
    if (count() != 1) return false;

    jgui_test::dispatch(ui, PointerPress{off, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(ui, PointerRelease{on, MouseButton::Left});
    if (count() != 1) return false;

    jgui_test::dispatch(ui, PointerPress{on, PointerButtonCode{0x111}, jgui_test::serial(1)});  // another mouse button
    jgui_test::dispatch(ui, PointerRelease{on, PointerButtonCode{0x111}});
    return count() == 1;
}
static_assert(button_click_rules());

// A dynamic button whose callback destroys itself and then reads its own captured state. If the
// storage were freed at once, this read would stop the file compiling.
consteval bool self_destruction_is_deferred() {
    int observed = 0;
    auto kill = [&observed, text = std::string("state long enough to be held on the heap")](auto& u) -> void {
        u.template get<ItemTag>().hide();
        observed = static_cast<int>(text.size());
    };
    using KillButton = Button<ItemTag, decltype(kill)>;
    auto ui = window(Dynamic<ItemTag, KillButton, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<KillButton> handle = node.create(CheckedText{"x"}, kill).value();
    static_cast<void>(jgui_test::layout(ui, Rect{Point{Left{0}, Top{0}}, Size{Width{50}, Height{20}}}, BlockShaper{}));
    const Point on{Left{1}, Top{1}};
    jgui_test::dispatch(ui, PointerPress{on, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(ui, PointerRelease{on, MouseButton::Left});
    if (observed != 40 || node.find(handle) != nullptr || node.shown()) return false;
    // A late event finds nothing, and a new item in the old slot is not reached by the old handle.
    jgui_test::dispatch(ui, PointerPress{on, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(ui, PointerRelease{on, MouseButton::Left});
    const Handle<KillButton> next = node.create(CheckedText{"y"}, kill).value();
    return node.find(handle) == nullptr && node.find(next) != nullptr;
}
static_assert(self_destruction_is_deferred());

consteval bool capture_and_focus_go_inert() {
    int fired = 0;
    auto fire = [&fired](auto&) -> void { ++fired; };
    using FireButton = Button<ItemTag, decltype(fire)>;
    auto ui = window(Dynamic<ItemTag, FireButton, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<FireButton> handle = node.create(CheckedText{"x"}, fire).value();
    static_cast<void>(jgui_test::layout(ui, Rect{Point{Left{0}, Top{0}}, Size{Width{50}, Height{20}}}, BlockShaper{}));
    const Point on{Left{1}, Top{1}};
    jgui_test::dispatch(ui, PointerPress{on, MouseButton::Left, jgui_test::serial(1)});
    if (node.captured() == nullptr || node.focused() == nullptr || !jgui_test::capture_active(ui)) return false;
    node.destroy(handle);
    if (node.captured() != nullptr || node.focused() != nullptr || jgui_test::capture_active(ui)) return false;
    jgui_test::dispatch(ui, PointerRelease{on, MouseButton::Left});
    jgui_test::dispatch(ui, KeyEvent{Key::Return, ModifierMask{}, true});
    return fired == 0;
}
static_assert(capture_and_focus_go_inert());

// An item that counts all it receives, with no guards of its own, so these checks see Dynamic's routing.
struct Recorder {
    using tag = ItemTag;
    Rect area{};
    int moves{};
    int releases{};
    int keys{};
    [[nodiscard]] constexpr Rect bounds() const noexcept { return area; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect offered, const Shaper&) {
        area = Rect{offered.origin(), Size{offered.width(), Height{10}}};
        return Rect{Point{offered.left(), top_after(area.bottom())}, Size{offered.width(), offered.height() - Height{10}}};
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerPress& event) { return area.contains(event.position); }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerMove& event) {
        ++moves;
        return area.contains(event.position);
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerRelease&) {
        ++releases;
        return true;
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const KeyEvent&) {
        ++keys;
        return true;
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};
static_assert(Widget<Recorder>);

consteval bool dynamic_routes_to_the_holder_only() {
    auto ui = window(Dynamic<ItemTag, Recorder, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<Recorder> upper = node.create().value();
    const Handle<Recorder> lower = node.create().value();
    static_cast<void>(jgui_test::layout(ui, Rect{Point{Left{0}, Top{0}}, Size{Width{50}, Height{40}}}, BlockShaper{}));
    if (node.find(upper)->bounds() != Rect{Point{Left{0}, Top{0}}, Size{Width{50}, Height{10}}} || node.find(lower)->bounds() != Rect{Point{Left{0}, Top{10}}, Size{Width{50}, Height{10}}}) return false;
    std::array<std::uint32_t, 1> pixel{};
    Canvas canvas{PixelBuffer::create(pixel, Size{Width{1}, Height{1}}, 4).value()};
    jgui_test::paint(ui, canvas);
    jgui_test::clear_damage(ui);
    if (jgui_test::damaged(ui)) return false;
    const Point on_upper{Left{5}, Top{5}};
    const Point on_lower{Left{5}, Top{15}};

    // Pressing the lower item gives it the pointer and the focus.
    jgui_test::dispatch(ui, PointerPress{on_lower, MouseButton::Left, jgui_test::serial(1)});
    if (node.captured() != node.find(lower) || node.focused() != node.find(lower)) return false;

    // While it holds the pointer, motion over the other item goes only to the holder.
    jgui_test::dispatch(ui, PointerMove{on_upper, true});
    if (node.find(lower)->moves != 1 || node.find(upper)->moves != 0) return false;

    // So does the release, which ends the capture.
    jgui_test::dispatch(ui, PointerRelease{on_upper, MouseButton::Left});
    if (node.find(lower)->releases != 1 || node.find(upper)->releases != 0) return false;
    if (node.captured() != nullptr) return false;

    // A second release has no holder and reaches nobody.
    jgui_test::dispatch(ui, PointerRelease{on_upper, MouseButton::Left});
    if (node.find(lower)->releases != 1 || node.find(upper)->releases != 0) return false;

    // Keys go only to the focused item, and to nobody once the focus is cleared.
    jgui_test::dispatch(ui, KeyEvent{Key::Return, ModifierMask{}, true});
    if (node.find(lower)->keys != 1 || node.find(upper)->keys != 0) return false;
    jgui_test::notify(ui, ClearFocus{});
    jgui_test::dispatch(ui, KeyEvent{Key::Return, ModifierMask{}, true});
    if (node.find(lower)->keys != 1 || node.find(upper)->keys != 0) return false;

    // With no capture, motion reaches the topmost item first, and one that is hit stops the hit test.
    jgui_test::dispatch(ui, PointerMove{on_lower, false});
    return node.find(lower)->moves == 2 && node.find(upper)->moves == 0;
}
static_assert(dynamic_routes_to_the_holder_only());

constexpr Size small_size_for_text{Width{60}, Height{24}};
// --- What the pointer looks like ---
using CursorFrame = WindowFrame<DefaultFrameMetrics>;
constexpr Size cursor_window{Width{200}, Height{100}};
[[nodiscard]] consteval CursorShape cursor_at(std::int32_t x, std::int32_t y) {
    return cursor_for(CursorFrame::hit(Point{Left{x}, Top{y}}, cursor_window));
}
static_assert(cursor_at(100, 0) == CursorShape::ResizeTop && cursor_at(100, 3) == CursorShape::ResizeTop);
static_assert(cursor_at(100, 99) == CursorShape::ResizeBottom && cursor_at(100, 96) == CursorShape::ResizeBottom);
static_assert(cursor_at(0, 50) == CursorShape::ResizeLeft && cursor_at(3, 50) == CursorShape::ResizeLeft);
static_assert(cursor_at(199, 50) == CursorShape::ResizeRight && cursor_at(196, 50) == CursorShape::ResizeRight);
static_assert(cursor_at(0, 0) == CursorShape::ResizeTopLeft && cursor_at(11, 0) == CursorShape::ResizeTopLeft && cursor_at(0, 11) == CursorShape::ResizeTopLeft);
static_assert(cursor_at(199, 0) == CursorShape::ResizeTopRight && cursor_at(188, 0) == CursorShape::ResizeTopRight);
static_assert(cursor_at(0, 99) == CursorShape::ResizeBottomLeft && cursor_at(0, 88) == CursorShape::ResizeBottomLeft);
static_assert(cursor_at(199, 99) == CursorShape::ResizeBottomRight && cursor_at(188, 99) == CursorShape::ResizeBottomRight);
static_assert(cursor_at(100, 50) == CursorShape::Default, "the middle");
static_assert(cursor_at(100, 12) == CursorShape::Default, "the title strip");
static_assert(cursor_at(4, 50) == CursorShape::Default && cursor_at(195, 50) == CursorShape::Default, "just inside the side edges");
static_assert(cursor_at(100, 4) == CursorShape::Default && cursor_at(100, 95) == CursorShape::Default, "just inside the top and bottom");
static_assert(cursor_at(-1, 50) == CursorShape::Default && cursor_at(200, 50) == CursorShape::Default && cursor_at(100, 100) == CursorShape::Default, "outside");
static_assert(cursor_for(FrameHit{FrameRegion::Close}) == CursorShape::Default && cursor_for(FrameHit{FrameRegion::Client}) == CursorShape::Default);

// --- Text that cannot be drawn does not end the program ---
// Checked text: what a program can make at run time, and where the limits fall.
static_assert(CheckedText::check("good").has_value() && CheckedText::check("").has_value());
static_assert(CheckedText::check("\xFF").error().code == ErrorCode::InvalidUtf8);
static_assert(CheckedText::check("\xC0\x80").error().code == ErrorCode::InvalidUtf8);
static_assert(CheckedText{"known"}.view() == "known" && CheckedText{std::string_view{"view"}}.view() == "view");
static_assert(detail::refuses_the_text(ErrorCode::InvalidUtf8) && detail::refuses_the_text(ErrorCode::TextTooLong) && detail::refuses_the_text(ErrorCode::GlyphTooLarge));
static_assert(!detail::refuses_the_text(ErrorCode::Font) && !detail::refuses_the_text(ErrorCode::System) && !detail::refuses_the_text(ErrorCode::SizeLimit));
consteval bool checked_text_limit_is_exact() {
    const std::string at_limit(max_caption_bytes, 'x');
    const std::string over_limit(max_caption_bytes + 1, 'x');
    return CheckedText::check(at_limit).has_value() && CheckedText::check(over_limit).error().code == ErrorCode::TextTooLong;
}
static_assert(checked_text_limit_is_exact());

// A shaper with a font that cannot do one text, and an engine that breaks on another.
struct PickyShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text == "expands") return make_error(ErrorCode::GlyphTooLarge, "the shaped line has too many glyphs");
        if (text == "breaks") return make_error(ErrorCode::Font, "the font engine failed");
        return BlockShaper{}.shape(text);
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};

constexpr Rect text_area{Point{Left{0}, Top{0}}, Size{Width{40}, Height{40}}};

consteval bool label_refuses_bad_text_when_it_is_set() {
    auto ui = window(label<ClickTag>("good"));
    if (!jgui_test::layout(ui, text_area, BlockShaper{}).has_value() || jgui_test::damaged(ui)) return false;
    auto& line = ui.get<ClickTag>();
    const auto invalid = line.set_text("\xC0\x80");
    if (invalid.has_value() || invalid.error().code != ErrorCode::InvalidUtf8) return false;
    const auto over = line.set_text(std::string(max_caption_bytes + 1, 'x'));
    if (over.has_value() || over.error().code != ErrorCode::TextTooLong) return false;
    if (line.text() != "good" || jgui_test::damaged(ui)) return false;
    if (!line.set_text("fine").has_value() || !jgui_test::damaged(ui)) return false;
    return jgui_test::layout(ui, text_area, BlockShaper{}).has_value() && line.text() == "fine";
}
static_assert(label_refuses_bad_text_when_it_is_set());

consteval bool shaping_refusal_stays_with_the_widget() {
    auto ui = window(label<ClickTag>("expands"), label<OtherTag>("after"));
    const auto left = jgui_test::layout(ui, text_area, PickyShaper{});
    auto& refused = ui.get<ClickTag>();
    if (!left.has_value() || !refused.text_error() || refused.text_error()->code != ErrorCode::GlyphTooLarge) return false;
    // It keeps its row, and the next widget goes below it as it would have.
    if (refused.bounds().height() != Height{16} || ui.get<OtherTag>().bounds().top() != Top{16} || ui.get<OtherTag>().text_error()) return false;
    // New text clears the refusal.
    if (!refused.set_text("good").has_value() || refused.text_error()) return false;
    return jgui_test::layout(ui, text_area, PickyShaper{}).has_value() && !refused.text_error();
}
static_assert(shaping_refusal_stays_with_the_widget());

consteval bool button_shaping_refusal_stays_with_it() {
    auto ui = window(button<ClickTag>("expands", [](auto&) -> void {}));
    if (!jgui_test::layout(ui, text_area, PickyShaper{}).has_value()) return false;
    const auto& refused = ui.get<ClickTag>();
    return refused.text_error() && refused.text_error()->code == ErrorCode::GlyphTooLarge &&
           refused.bounds().width() == theme::button_pad_x + theme::button_pad_x;
}
static_assert(button_shaping_refusal_stays_with_it());

consteval bool failure_of_the_engine_still_ends_the_layout() {
    auto ui = window(label<ClickTag>("breaks"));
    const auto left = jgui_test::layout(ui, text_area, PickyShaper{});
    return !left.has_value() && left.error().code == ErrorCode::Font && !ui.get<ClickTag>().text_error();
}
static_assert(failure_of_the_engine_still_ends_the_layout());

// The whole way through the application: a frame is drawn without that text, and the other text is in it.
consteval bool refused_text_draws_nothing_and_the_rest_draws() {
    constexpr Size size{Width{60}, Height{24}};
    ApplicationCore core{HeadlessBackend{size}, window(label<ClickTag>("expands"), label<OtherTag>("after"))};
    const PickyShaper shaper;
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;
    const auto ink_in = [&](const Rect& area) {
        int count = 0;
        for (std::int32_t y = jgui_test::number(area.top()); y < jgui_test::number(area.bottom()); ++y)
            for (std::int32_t x = jgui_test::number(area.left()); x < jgui_test::number(area.right()); ++x)
                if (jgui_test::backend(core).pixel(Point{Left{x}, Top{y}}) != theme::background.argb) ++count;
        return count;
    };
    if (ink_in(core.ui().get<ClickTag>().bounds()) != 0 || ink_in(core.ui().get<OtherTag>().bounds()) == 0) return false;
    if (!core.ui().get<ClickTag>().set_text("good").has_value()) return false;
    if (!core.process_pending(shaper).has_value() || core.frames() != 2) return false;
    return ink_in(core.ui().get<ClickTag>().bounds()) != 0;
}
static_assert(refused_text_draws_nothing_and_the_rest_draws());

consteval bool failure_of_the_engine_still_ends_the_run() {
    ApplicationCore core{HeadlessBackend{small_size_for_text}, window(label<ClickTag>("breaks"))};
    const auto result = core.process_pending(PickyShaper{});
    return !result.has_value() && result.error().code == ErrorCode::Font && core.frames() == 0;
}
static_assert(failure_of_the_engine_still_ends_the_run());

// The shape follows the pointer through a real application, and only a change reaches the backend.
consteval bool cursor_changes_only_when_the_region_does() {
    ApplicationCore core{HeadlessBackend{cursor_window}, window(window_frame(button<ClickTag>("x", [](auto&) -> void {})))};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    auto& backend = jgui_test::backend(core);
    if (backend.cursor_requests() != 0) return false;
    const auto at = [&](std::int32_t x, std::int32_t y) {
        backend.inject(PointerMove{Point{Left{x}, Top{y}}});
        return core.process_pending(shaper).has_value();
    };
    const auto expect = [&](std::int32_t x, std::int32_t y, CursorShape shape, std::size_t requests) {
        return at(x, y) && backend.cursor() == shape && backend.cursor_requests() == requests;
    };
    if (!expect(100, 50, CursorShape::Default, 1)) return false;
    if (!expect(101, 51, CursorShape::Default, 1) || !expect(100, 12, CursorShape::Default, 1)) return false;  // middle to title is no change
    if (!expect(100, 0, CursorShape::ResizeTop, 2) || !expect(90, 1, CursorShape::ResizeTop, 2)) return false;
    if (!expect(100, 99, CursorShape::ResizeBottom, 3) || !expect(0, 50, CursorShape::ResizeLeft, 4)) return false;
    if (!expect(199, 50, CursorShape::ResizeRight, 5) || !expect(0, 0, CursorShape::ResizeTopLeft, 6)) return false;
    if (!expect(199, 0, CursorShape::ResizeTopRight, 7) || !expect(0, 99, CursorShape::ResizeBottomLeft, 8)) return false;
    if (!expect(199, 99, CursorShape::ResizeBottomRight, 9) || !expect(100, 50, CursorShape::Default, 10)) return false;

    // An enter says nothing is known, so the same shape goes again. A leave alone sends nothing.
    backend.inject(PointerLeave{});
    if (!core.process_pending(shaper).has_value() || backend.cursor_requests() != 10) return false;
    backend.inject(PointerMove{Point{Left{100}, Top{50}}, false, true});
    if (!core.process_pending(shaper).has_value() || backend.cursor_requests() != 11) return false;
    return expect(0, 50, CursorShape::ResizeLeft, 12);
}
static_assert(cursor_changes_only_when_the_region_does());

consteval bool cursor_stays_during_a_capture_and_catches_up_after() {
    ApplicationCore core{HeadlessBackend{cursor_window}, window(window_frame(button<ClickTag>("x", [](auto&) -> void {})))};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    auto& backend = jgui_test::backend(core);
    const Point on_button = jgui_test::inside(core.ui().get<ClickTag>().bounds(), 1, 1);
    const Point on_edge{Left{0}, Top{50}};
    backend.inject(PointerMove{on_button, false, true});
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    backend.inject(PointerMove{on_edge});
    backend.inject(PointerMove{Point{Left{0}, Top{0}}});
    if (!core.process_pending(shaper).has_value() || backend.cursor() != CursorShape::Default || backend.cursor_requests() != 1) return false;
    backend.inject(PointerButton{on_edge, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
    return core.process_pending(shaper).has_value() && backend.cursor() == CursorShape::ResizeLeft && backend.cursor_requests() == 2;
}
static_assert(cursor_stays_during_a_capture_and_catches_up_after());

// A window with no frame says nothing about it, and the pointer must still look like something, once.
consteval bool cursor_with_no_frame_is_the_arrow() {
    ApplicationCore core{HeadlessBackend{cursor_window}, window(label<ClickTag>("x"))};
    const BlockShaper shaper;
    auto& backend = jgui_test::backend(core);
    backend.inject(PointerMove{Point{Left{0}, Top{0}}, false, true});
    backend.inject(PointerMove{Point{Left{1}, Top{1}}});
    return core.process_pending(shaper).has_value() && backend.cursor() == CursorShape::Default && backend.cursor_requests() == 1;
}
static_assert(cursor_with_no_frame_is_the_arrow());

// A request from a callback goes the same way, and is carried out after the delivery.
consteval bool callback_can_ask_for_a_cursor() {
    ApplicationCore core{HeadlessBackend{cursor_window}, window(button<ClickTag>("x", [](auto& u) -> void { u.request_cursor(CursorShape::ResizeTop); }))};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    const Point on_button = jgui_test::inside(core.ui().get<ClickTag>().bounds(), 1, 1);
    auto& backend = jgui_test::backend(core);
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
    return core.process_pending(shaper).has_value() && backend.cursor() == CursorShape::ResizeTop;
}
static_assert(callback_can_ask_for_a_cursor());

// --- The real demo tree and application logic, from injected events to pixels, during compilation ---
constexpr Size small_window{Width{120}, Height{60}};

consteval bool demo_help_toggles_in_pixels() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;

    const Rect target = core.ui().get<demo::HelpButton>().bounds();
    const Point on = jgui_test::inside(target, 1, 1);
    const Rect bar = core.ui().get<MenuBarTag>().bounds();
    const Rect below{Point{bar.left(), top_after(bar.bottom())}, Size{bar.width(), Frame::client_rect(small_window).bottom() - top_after(bar.bottom())}};
    const auto text_pixels = [&] {
        int count = 0;
        for (std::int32_t y = jgui_test::number(below.top()); y < jgui_test::number(below.bottom()); ++y)
            for (std::int32_t x = jgui_test::number(below.left()); x < jgui_test::number(below.right()); ++x)
                if (jgui_test::backend(core).pixel(Point{Left{x}, Top{y}}) != theme::background.argb) ++count;
        return count;
    };
    const auto click = [&] {
        jgui_test::backend(core).inject(PointerButton{on, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
        jgui_test::backend(core).inject(PointerButton{on, MouseButton::Left, ButtonState::Released, jgui_test::serial(2)});
        return core.process_pending(shaper).has_value();
    };
    if (text_pixels() != 0) return false;
    if (!click() || text_pixels() == 0 || !core.ui().get<demo::HelpLine>().shown()) return false;
    if (!click() || text_pixels() != 0 || core.ui().get<demo::HelpLine>().shown()) return false;
    return true;
}
static_assert(demo_help_toggles_in_pixels());

consteval bool frame_routing_and_refused_sizes() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    const auto frames = core.frames();

    // No damage, no redraw.
    if (!core.process_pending(shaper).has_value() || core.frames() != frames) return false;

    // A title press starts a move with its serial, and an edge press a resize.
    jgui_test::backend(core).inject(PointerButton{Point{Left{60}, Top{10}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(11)});
    jgui_test::backend(core).inject(PointerButton{Point{Left{0}, Top{30}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(12)});
    if (!core.process_pending(shaper).has_value()) return false;
    if (jgui_test::backend(core).move_serial() != jgui_test::serial(11) || jgui_test::backend(core).resize_serial() != jgui_test::serial(12)) return false;
    if (jgui_test::backend(core).resize_edge() != ResizeEdge::LeftSide) return false;

    // An absurd size is refused and leaves the window as it was.
    jgui_test::backend(core).inject(ResizeEvent{Size{Width{int_max}, Height{int_max}}});
    const auto refused_result = core.process_pending(shaper);
    if (refused_result.has_value() || refused_result.error().code != ErrorCode::SizeLimit) return false;
    if (core.size() != small_window) return false;

    // Press on the close box, release off it: nothing. Press and release on it: closed.
    const Point close = jgui_test::inside(Frame::close_box(small_window), 1, 1);
    jgui_test::backend(core).inject(PointerButton{close, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(13)});
    jgui_test::backend(core).inject(PointerButton{Point{Left{60}, Top{40}}, MouseButton::Left, ButtonState::Released, jgui_test::serial(14)});
    if (!core.process_pending(shaper).has_value() || core.closed()) return false;
    jgui_test::backend(core).inject(PointerButton{close, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(15)});
    jgui_test::backend(core).inject(PointerButton{close, MouseButton::Left, ButtonState::Released, jgui_test::serial(16)});
    return core.process_pending(shaper).has_value() && core.closed();
}
static_assert(frame_routing_and_refused_sizes());

// The frame is an ordinary node: the tree finds it and what it holds, and a childless frame works.
consteval bool frame_is_a_node_of_the_tree() {
    auto ui = window(window_frame<ThickMetrics>(menu_bar(button<ClickTag>("x", bump_on_click)), Counter{}));
    const Rect whole{Point{Left{0}, Top{0}}, Size{Width{120}, Height{80}}};
    detail::TestAccess::set_size(ui, whole.size());
    const auto left = jgui_test::layout(ui, whole, BlockShaper{});
    if (!left.has_value() || !left->empty()) return false;
    if (ui.get<WindowFrameTag>().bounds() != whole) return false;
    if (ui.get<MenuBarTag>().bounds().origin() != ThickFrame::client_rect(whole.size()).origin()) return false;
    if (ui.get<ClickTag>().bounds().intersect(ui.get<MenuBarTag>().bounds()).empty()) return false;

    auto empty = window(window_frame());
    detail::TestAccess::set_size(empty, whole.size());
    if (!jgui_test::layout(empty, whole, BlockShaper{}).has_value()) return false;
    jgui_test::dispatch(empty, PointerPress{Point{Left{60}, Top{10}}, MouseButton::Left, jgui_test::serial(9)});
    return detail::TestAccess::take_requests(empty).move == jgui_test::serial(9) && !detail::TestAccess::take_requests(empty).move.has_value();
}
static_assert(frame_is_a_node_of_the_tree());

// A frame of another size routes and paints at that size, all the way through the application.
consteval bool thick_frame_through_the_application() {
    ApplicationCore core{HeadlessBackend{small_window}, window(window_frame<ThickMetrics>(label<ClickTag>("x")))};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    jgui_test::backend(core).inject(PointerButton{Point{Left{6}, Top{30}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(21)});
    jgui_test::backend(core).inject(PointerButton{Point{Left{60}, Top{28}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(22)});
    if (!core.process_pending(shaper).has_value()) return false;
    if (jgui_test::backend(core).resize_serial() != jgui_test::serial(21) || jgui_test::backend(core).resize_edge() != ResizeEdge::LeftSide) return false;
    if (jgui_test::backend(core).move_serial() != jgui_test::serial(22)) return false;
    return jgui_test::backend(core).pixel(Point{Left{6}, Top{30}}) == theme::frame_strip.argb && jgui_test::backend(core).pixel(Point{Left{60}, Top{28}}) == theme::frame_strip.argb;
}
static_assert(thick_frame_through_the_application());

// The frame's rules, pinned as they were when it sat outside the tree. Each evaluation paints the
// window several times, so they are split to stay inside the compiler's default operation limit.
struct FrameRig {
    ApplicationCore<HeadlessBackend, decltype(demo::make_ui<std::vector>())> core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    BlockShaper shaper{};

    [[nodiscard]] consteval bool start() { return core.process_pending(shaper).has_value(); }
    template <class E>
    [[nodiscard]] consteval bool deliver(const E& event) {
        jgui_test::backend(core).inject(event);
        return core.process_pending(shaper).has_value();
    }
    [[nodiscard]] consteval bool left(Point at, ButtonState state, std::uint32_t serial) {
        return deliver(PointerButton{at, MouseButton::Left, state, jgui_test::serial(serial)});
    }
    [[nodiscard]] constexpr auto& frame() { return core.ui().get<WindowFrameTag>(); }
    [[nodiscard]] constexpr auto& help() { return core.ui().get<demo::HelpButton>(); }
    [[nodiscard]] constexpr Point on_help() {
        return jgui_test::inside(help().bounds(), 1, 1);
    }
};
constexpr Rect small_box = Frame::close_box(small_window);
constexpr Point on_close = jgui_test::inside(small_box, 1, 1);
constexpr Point on_title{Left{60}, Top{10}};
constexpr Point on_client{Left{60}, Top{50}};
constexpr PointerButtonCode right_button{0x111};

consteval bool hovering_the_close_box_redraws_once() {
    FrameRig rig;
    if (!rig.start()) return false;
    const auto frames = rig.core.frames();
    if (!rig.deliver(PointerMove{on_close}) || rig.core.frames() != frames + 1 || !rig.frame().close_hovered()) return false;
    if (jgui_test::backend(rig.core).pixel(jgui_test::inside(small_box, 1, 2)) != theme::close_hover.argb) return false;
    return rig.deliver(PointerMove{on_close}) && rig.core.frames() == frames + 1;
}
static_assert(hovering_the_close_box_redraws_once());

consteval bool close_stays_armed_over_a_widget_and_back() {
    FrameRig rig;
    if (!rig.start() || !rig.deliver(PointerMove{on_close})) return false;
    if (!rig.left(on_close, ButtonState::Pressed, 31) || !rig.frame().close_armed()) return false;
    if (jgui_test::backend(rig.core).pixel(jgui_test::inside(small_box, 1, 2)) != theme::close_pressed.argb) return false;
    if (!rig.deliver(PointerMove{rig.on_help()}) || !rig.frame().close_armed() || rig.frame().close_hovered()) return false;
    if (!rig.help().hovered()) return false;
    if (!rig.deliver(PointerMove{on_close}) || !rig.frame().close_armed() || rig.core.closed()) return false;
    return rig.left(on_close, ButtonState::Released, 32) && rig.core.closed();
}
static_assert(close_stays_armed_over_a_widget_and_back());

consteval bool leaving_the_window_disarms_close() {
    FrameRig rig;
    if (!rig.start() || !rig.left(on_close, ButtonState::Pressed, 33)) return false;
    if (!rig.deliver(PointerLeave{}) || rig.frame().close_armed() || rig.frame().close_hovered()) return false;
    return rig.left(on_close, ButtonState::Released, 34) && !rig.core.closed();
}
static_assert(leaving_the_window_disarms_close());

consteval bool only_the_left_button_acts_on_the_frame() {
    FrameRig rig;
    if (!rig.start()) return false;
    if (!rig.deliver(PointerButton{on_title, right_button, ButtonState::Pressed, jgui_test::serial(35)})) return false;
    if (jgui_test::backend(rig.core).move_serial().has_value()) return false;
    return rig.deliver(PointerButton{on_close, right_button, ButtonState::Pressed, jgui_test::serial(36)}) && !rig.frame().close_armed();
}
static_assert(only_the_left_button_acts_on_the_frame());

consteval bool a_captured_drag_over_the_frame_starts_nothing() {
    FrameRig rig;
    if (!rig.start()) return false;
    const Point help = rig.on_help();
    if (!rig.left(help, ButtonState::Pressed, 37) || !jgui_test::capture_active(rig.core.ui())) return false;
    if (!rig.deliver(PointerMove{on_title}) || !rig.help().pressed()) return false;
    if (!rig.left(help, ButtonState::Released, 38)) return false;
    return !jgui_test::backend(rig.core).move_serial().has_value() && !jgui_test::backend(rig.core).resize_serial().has_value();
}
static_assert(a_captured_drag_over_the_frame_starts_nothing());

consteval bool only_a_client_press_takes_the_focus_away() {
    FrameRig rig;
    if (!rig.start()) return false;
    const Point help = rig.on_help();
    if (!rig.left(help, ButtonState::Pressed, 39) || !rig.left(help, ButtonState::Released, 40) || !rig.help().focused()) return false;
    if (!rig.left(on_title, ButtonState::Pressed, 41) || !rig.help().focused()) return false;
    if (jgui_test::backend(rig.core).move_serial() != jgui_test::serial(41)) return false;
    return rig.left(on_client, ButtonState::Pressed, 42) && !rig.help().focused();
}
static_assert(only_a_client_press_takes_the_focus_away());

consteval bool moving_off_the_client_area_is_a_leave() {
    FrameRig rig;
    if (!rig.start() || !rig.deliver(PointerMove{rig.on_help()}) || !rig.help().hovered()) return false;
    return rig.deliver(PointerMove{on_title}) && !rig.help().hovered();
}
static_assert(moving_off_the_client_area_is_a_leave());

// A press queued behind a resize is judged at the new size, before any layout has seen it.
consteval bool a_press_behind_a_resize_uses_the_new_size() {
    FrameRig rig;
    if (!rig.start()) return false;
    jgui_test::backend(rig.core).inject(ResizeEvent{Size{Width{300}, Height{200}}});
    if (!rig.left(Point{Left{299}, Top{100}}, ButtonState::Pressed, 43)) return false;
    return jgui_test::backend(rig.core).resize_serial() == jgui_test::serial(43) && jgui_test::backend(rig.core).resize_edge() == ResizeEdge::RightSide;
}
static_assert(a_press_behind_a_resize_uses_the_new_size());

// While the display holds too much, nothing is drawn, nothing fails, and the damage stays until it can be.
consteval bool waiting_for_room_keeps_the_damage() {
    ApplicationCore core{HeadlessBackend{Size{Width{60}, Height{40}}}, window(window_frame())};
    const BlockShaper shaper;
    jgui_test::backend(core).set_display_holding(true);
    if (!core.process_pending(shaper).has_value() || core.frames() != 0) return false;
    if (!core.process_pending(shaper).has_value() || core.frames() != 0) return false;
    jgui_test::backend(core).set_display_holding(false);
    return core.process_pending(shaper).has_value() && core.frames() == 1;
}
static_assert(waiting_for_room_keeps_the_damage());

consteval bool run_ends_on_close_and_empty_wait_is_an_error() {
    ApplicationCore closes{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    jgui_test::backend(closes).inject(CloseEvent{});
    if (!closes.run(BlockShaper{}).has_value()) return false;
    ApplicationCore waits{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const auto result = waits.run(BlockShaper{});
    return !result.has_value() && result.error().code == ErrorCode::WouldBlock;
}
static_assert(run_ends_on_close_and_empty_wait_is_an_error());

}  // namespace

int main() {
    std::cout << "everything was checked during compilation\n";
}
