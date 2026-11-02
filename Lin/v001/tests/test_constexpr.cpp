// File: tests/test_constexpr.cpp
// Purpose: Compile-time tests. Constant evaluation rejects undefined behaviour, including overflow,
//          out-of-bounds access, reading a destroyed object and leaks, but only on the paths it actually
//          evaluates. So these run edge cases through the real code: this file compiling is the test.
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "help_ui.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"

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
static_assert(sat_mul(int_max, 2) == int_max && sat_mul(int_min, 2) == int_min && sat_mul(int_min, -1) == int_max);
static_assert(sat_mul(int_max, int_max) == int_max && sat_mul(int_min, int_min) == int_max && sat_mul(-3, 4) == -12);
static_assert(saturate_to<std::int32_t>(std::int64_t{1} << 40U) == int_max);
static_assert(saturate_to<std::int32_t>(-(std::int64_t{1} << 40U)) == int_min);
static_assert(saturate_to<std::uint32_t>(-1) == 0U);
static_assert(saturate_to<std::int32_t>(std::numeric_limits<std::uint64_t>::max()) == int_max);
static_assert(saturate_to<std::uint8_t>(256) == 255 && saturate_to<std::uint8_t>(255) == 255);
static_assert(detail::ceil_pixels(0) == 0 && detail::ceil_pixels(1) == 1 && detail::ceil_pixels(64) == 1 && detail::ceil_pixels(65) == 2);
static_assert(detail::ceil_pixels(std::int64_t{1} << 50U) == int_max);

// --- Geometry ---
static_assert(Size{-1, -1} == Size{0, 0} && Size{int_min, 5}.width() == 0 && Size{5, int_min}.empty());
static_assert(Size{int_max, int_max}.width() == int_max);
static_assert((Point{int_max, int_min} + Point{1, -1}) == Point{int_max, int_min});
static_assert((Point{int_min, int_max} - Point{1, -1}) == Point{int_min, int_max});
static_assert(Rect{int_max, int_max, 10, 10}.right() == int_max && Rect{int_max, int_max, 10, 10}.bottom() == int_max);
static_assert(Rect{int_max - 1, 0, 10, 10}.contains(Point{int_max, 5}));
static_assert(!Rect{int_max, 0, 0, 10}.contains(Point{int_max, 5}));
static_assert(Rect{int_min, int_min, int_max, int_max}.contains(Point{int_min, int_min}));
static_assert(!Rect{int_min, int_min, int_max, int_max}.contains(Point{-1, -1}));
static_assert(Rect{int_min, int_min, int_max, int_max}.contains(Point{-2, -2}));
static_assert(!Rect{0, 0, 10, 10}.contains(Point{10, 0}) && !Rect{0, 0, 10, 10}.contains(Point{-1, 0}));
static_assert(!Rect{}.contains(Point{}));
static_assert(Rect{0, 0, 10, 10}.intersect(Rect{5, 5, 10, 10}) == Rect{5, 5, 5, 5});
static_assert(Rect{0, 0, 10, 10}.intersect(Rect{10, 0, 10, 10}).empty());
static_assert(Rect{0, 0, 10, 10}.intersect(Rect{-5, -5, 3, 3}).empty());
static_assert(Rect{int_max, int_max, int_max, int_max}.intersect(Rect{int_min, int_min, int_max, int_max}).empty());
static_assert(Rect{int_min, int_min, int_max, int_max}.intersect(Rect{int_min, int_min, int_max, int_max}) == Rect{int_min, int_min, int_max, int_max});
static_assert(Rect{int_max - 1, 0, int_max, 4}.intersect(Rect{0, 0, int_max, 4}) == Rect{int_max - 1, 0, 1, 4});
static_assert(Rect{int_max - 2, 0, int_max, 4}.intersect(Rect{0, 0, int_max, 4}) == Rect{int_max - 2, 0, 2, 4});
static_assert(Rect{0, 0, 4, 4}.inset(3).empty() && Rect{0, 0, 10, 10}.inset(2) == Rect{2, 2, 6, 6});
static_assert(Rect{int_max, int_min, 4, 4}.inset(int_max).empty());

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
constexpr bool refused(Size size) { return !PixelBuffer::byte_size(size).has_value(); }
static_assert(PixelBuffer::byte_size(Size{}).value() == 0);
static_assert(PixelBuffer::byte_size(Size{0, max_buffer_side}).value() == 0);
static_assert(PixelBuffer::byte_size(Size{1, 1}).value() == 4);
static_assert(std::int64_t{max_buffer_side} * max_buffer_side * 4 == max_buffer_bytes, "both limits meet exactly");
static_assert(PixelBuffer::byte_size(Size{max_buffer_side, max_buffer_side}).value() == std::size_t{1} << 30U);
static_assert(refused(Size{max_buffer_side + 1, 1}) && refused(Size{1, max_buffer_side + 1}));
static_assert(refused(Size{int_max, int_max}) && refused(Size{46341, 46341}) && refused(Size{65536, 65536}));
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
static_assert(detail::coverage_fits(Size{}, 0) && detail::coverage_fits(Size{3, 4}, 12));
static_assert(!detail::coverage_fits(Size{3, 4}, 11) && !detail::coverage_fits(Size{3, 4}, 13) && !detail::coverage_fits(Size{}, 1));
static_assert(!detail::coverage_fits(Size{int_max, int_max}, 0));
static_assert(detail::coverage_fits(Size{int_max, int_max}, std::size_t{2147483647} * std::size_t{2147483647}));
static_assert(!detail::coverage_fits(Size{int_max, int_max}, std::numeric_limits<std::size_t>::max()));
static_assert(TextRun{Size{2, 2}, std::vector<std::uint8_t>(4, 7)}.coverage_at(1, 1) == 7);
static_assert(TextRun{Size{}, std::vector<std::uint8_t>{}}.size().empty());

constexpr bool buffer_create_checks() {
    std::array<std::uint32_t, 12> storage{};
    const auto accepted = [&storage](Size size, std::int32_t stride) {
        return PixelBuffer::create(storage, size, stride).has_value();
    };
    if (!accepted(Size{3, 4}, 12)) return false;
    if (!accepted(Size{2, 3}, 16)) return false;
    if (accepted(Size{3, 5}, 12)) return false;  // too little storage
    if (accepted(Size{3, 4}, 8)) return false;   // stride under the width
    if (accepted(Size{3, 4}, 13)) return false;  // not whole pixels
    if (accepted(Size{3, 4}, -4)) return false;
    if (accepted(Size{1, 16384}, int_max - 3)) return false;
    return true;
}
static_assert(buffer_create_checks());

// --- Canvas: any write outside the buffer would stop this compiling, and the guards check the rest ---
constexpr bool canvas_stays_inside() {
    constexpr std::size_t total = 8 * 8;
    constexpr std::uint32_t guard = 0xDEADBEEFU;
    std::array<std::uint32_t, total> storage{};
    storage.fill(guard);
    // A 4x4 buffer in the middle of the storage, with rows 8 pixels apart.
    const std::span<std::uint32_t> inner = std::span{storage}.subspan(18);
    const Canvas canvas{PixelBuffer::create(inner, Size{4, 4}, 32).value()};
    const std::array<Rect, 12> shapes{Rect{int_min, int_min, int_max, int_max}, Rect{int_max, int_max, int_max, int_max},
                                      Rect{-100, -100, 100, 100},               Rect{4, 0, 10, 10},
                                      Rect{0, 4, 10, 10},                       Rect{-5, 1, 6, 1},
                                      Rect{3, 3, int_max, int_max},             Rect{1, 1, 0, 0},
                                      Rect{-1, -1, 6, 6},                       Rect{int_min, 0, int_max, 2},
                                      Rect{0, int_max - 1, 4, 4},               Rect{int_max - 1, 0, 4, 4}};
    for (const Rect& shape : shapes) {
        canvas.fill_rect(shape, Color{1});
        canvas.stroke_rect(shape, Color{2});
        canvas.clipped(Rect{1, 1, 2, 2}).fill_rect(shape, Color{3});
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

constexpr bool canvas_clip_and_text() {
    std::array<std::uint32_t, 16> storage{};
    const Canvas canvas{PixelBuffer::create(storage, Size{4, 4}, 16).value()};
    const TextRun run{Size{4, 4}, std::vector<std::uint8_t>(16, 255)};
    canvas.clipped(Rect{1, 1, 2, 2}).draw_text(Point{0, 0}, run, Color{0xFF112233U});
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            const bool inside = x >= 1 && x < 3 && y >= 1 && y < 3;
            if (storage[y * 4 + x] != (inside ? 0xFF112233U : 0U)) return false;
        }
    }
    canvas.draw_text(Point{int_max, int_max}, run, Color{1});
    canvas.draw_text(Point{int_min, int_min}, run, Color{1});
    canvas.draw_text(Point{-3, -3}, run, Color{0xFF000000U});
    return storage[0] == 0xFF000000U && storage[5] == 0xFF112233U;
}
static_assert(canvas_clip_and_text());

constexpr bool glyph_blit_is_clipped() {
    const std::array<std::uint8_t, 4> glyph{10, 20, 30, 40};
    const detail::GlyphBitmap bitmap{glyph, 2, 2, 2};
    std::array<std::uint8_t, 9> out{};
    constexpr std::int64_t huge = std::int64_t{1} << 40U;
    detail::blit_glyph(bitmap, huge, huge, Size{3, 3}, out);
    detail::blit_glyph(bitmap, -huge, -huge, Size{3, 3}, out);
    if (out != std::array<std::uint8_t, 9>{}) return false;
    detail::blit_glyph(bitmap, 2, 2, Size{3, 3}, out);  // only the top-left of the glyph lands
    if (out[8] != 10 || out[7] != 0 || out[5] != 0) return false;
    detail::blit_glyph(bitmap, -1, -1, Size{3, 3}, out);  // only the bottom-right lands
    return out[0] == 40 && out[1] == 0 && out[3] == 0;
}
static_assert(glyph_blit_is_clipped());

// --- Pool: the real logic, on the slots that constant evaluation allows ---
template <class T>
using TestPool = Pool<T, std::vector>;

constexpr bool pool_basics() {
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

constexpr bool pool_stale_and_foreign_handles() {
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

constexpr bool pool_retires_at_max_generation() {
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

constexpr bool pool_top_generation_does_not_wrap() {
    TestPool<int> pool{0};
    const Handle<int> handle = pool.emplace(1).value();
    pool.destroy(handle);
    pool.collect();
    return pool.retired_count() == 1 && pool.find(handle) == nullptr;
}
static_assert(pool_top_generation_does_not_wrap());

// A destroyed item's storage stays valid until collect. Reading it after that would not compile.
constexpr bool pool_deferred_destruction() {
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
};

constexpr bool queue_keeps_arrival_order() {
    BackendEventQueue queue;
    if (!queue.push(PointerMove{}) || !queue.push(KeyEvent{}) || !queue.push(PointerMove{}) || !queue.push(CloseEvent{}))
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

// --- Frame ---
using Frame = WindowFrame<DefaultFrameMetrics>;
constexpr Size frame_window{200, 100};
static_assert(Frame::hit(Point{-1, 0}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{200, 0}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{int_max, int_min}, frame_window).region == FrameRegion::Outside);
static_assert(Frame::hit(Point{0, 0}, frame_window).edge == ResizeEdge::TopLeft);
static_assert(Frame::hit(Point{199, 0}, frame_window).edge == ResizeEdge::TopRight);
static_assert(Frame::hit(Point{0, 99}, frame_window).edge == ResizeEdge::BottomLeft);
static_assert(Frame::hit(Point{199, 99}, frame_window).edge == ResizeEdge::BottomRight);
static_assert(Frame::hit(Point{100, 0}, frame_window).edge == ResizeEdge::Top);
static_assert(Frame::hit(Point{100, 99}, frame_window).edge == ResizeEdge::Bottom);
static_assert(Frame::hit(Point{0, 50}, frame_window).edge == ResizeEdge::Left);
static_assert(Frame::hit(Point{199, 50}, frame_window).edge == ResizeEdge::Right);
static_assert(Frame::hit(Point{100, 10}, frame_window).region == FrameRegion::Title);
static_assert(Frame::hit(Point{100, 50}, frame_window).region == FrameRegion::Client);
static_assert(Frame::hit(Point{Frame::close_box(frame_window).x(), 10}, frame_window).region == FrameRegion::Close);
static_assert(Frame::client_rect(Size{}).empty() && Frame::client_rect(Size{1, 1}).empty());
static_assert(Frame::client_rect(Size{int_max, int_max}).width() > 0);
static_assert(Frame::hit(Point{0, 0}, Size{}).region == FrameRegion::Outside);

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
static_assert(Frame::client_rect(frame_window) == Rect{4, 24, 192, 72});
static_assert(ThickFrame::client_rect(frame_window) == Rect{8, 32, 184, 60});
static_assert(Frame::close_box(frame_window) == Rect{176, 4, 16, 16});
static_assert(ThickFrame::close_box(frame_window) == Rect{166, 6, 20, 20});
static_assert(Frame::hit(Point{6, 50}, frame_window).region == FrameRegion::Client);
static_assert(ThickFrame::hit(Point{6, 50}, frame_window).edge == ResizeEdge::Left);
static_assert(Frame::hit(Point{100, 28}, frame_window).region == FrameRegion::Client);
static_assert(ThickFrame::hit(Point{100, 28}, frame_window).region == FrameRegion::Title);

using namespace refused_metrics;
static_assert(!FrameMetrics<NoCloseSide> && !FrameMetrics<NotConstant> && !FrameMetrics<WrongFieldType>);
static_assert(!FrameMetrics<NoEdge> && !FrameMetrics<BoxTallerThanStrip> && !FrameMetrics<CornerShorterThanEdge>);
static_assert(!detail::tags_unique<Frame, ThickFrame>(), "a second frame in one window is a duplicate tag");

// --- A shaper for compile time: every byte is a solid block, with the real engine's refusals ---
struct BlockShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text.size() > max_caption_bytes) return make_error(ErrorCode::TextTooLong, "too long");
        if (!valid_utf8(text)) return make_error(ErrorCode::InvalidUtf8, "invalid");
        const Size size{saturate_to<std::int32_t>(text.size() * 2), 4};
        return TextRun{size, std::vector<std::uint8_t>(text.size() * 2 * 4, 255)};
    }
    [[nodiscard]] constexpr std::int32_t line_height() const noexcept { return 4; }
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
    constexpr void paint(Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper&) { return area; }
    template <class U, class E>
    constexpr bool handle(U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(U&, const E&) {}
};

struct Silent {
    using tag = SilentTag;
    int calls{};
    constexpr void on(Add, const int&) { ++calls; }
    constexpr void on(Quiet) { ++calls; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return Rect{}; }
    constexpr void paint(Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper&) { return area; }
    template <class U, class E>
    constexpr bool handle(U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(U&, const E&) {}
};

static_assert(Widget<Counter> && Widget<Silent>);
static_assert(std::same_as<std::tuple_element_t<0, decltype(window(Counter{}, Silent{}).send<Quiet>())>, NoReply>);

constexpr bool messages_reach_receivers_in_order() {
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
constexpr bool receivers_are_widgets() {
    auto ui = window(Counter{}, Silent{});
    const Rect area{0, 0, 2, 2};
    const auto left = ui.layout(area, BlockShaper{});
    const bool passed_through = left.has_value() && *left == area;
    std::array<std::uint32_t, 4> pixels{};
    Canvas canvas{PixelBuffer::create(pixels, area.size(), 8).value()};
    ui.paint(canvas);
    return passed_through && !ui.capture_active() && !ui.damaged() && ui.get<CountTag>().bounds().empty() && ui.get<SilentTag>().bounds().empty() &&
           pixels == std::array<std::uint32_t, 4>{};
}
static_assert(receivers_are_widgets());

constexpr std::string_view caption = "OK";

constexpr auto bump_on_click = [](auto& u) -> void { u.template send<Bump>(); };

constexpr bool button_click_rules() {
    auto ui = window(menu_bar(button<ClickTag>(caption, bump_on_click)), Counter{});
    static_cast<void>(ui.layout(Rect{0, 0, 100, 40}, BlockShaper{}));
    auto& target = ui.get<ClickTag>();
    const Point on{target.bounds().x() + 1, target.bounds().y() + 1};
    const Point off{99, 39};
    const auto count = [&] { return ui.get<CountTag>().value; };

    ui.dispatch(PointerPress{on, MouseButton::Left});
    if (!target.pressed() || !ui.capture_active()) return false;
    ui.dispatch(PointerRelease{on, MouseButton::Left});
    if (count() != 1 || target.pressed() || ui.capture_active()) return false;

    ui.dispatch(PointerPress{on, MouseButton::Left});
    ui.dispatch(PointerRelease{off, MouseButton::Left});
    if (count() != 1) return false;

    ui.dispatch(PointerPress{off, MouseButton::Left});
    ui.dispatch(PointerRelease{on, MouseButton::Left});
    if (count() != 1) return false;

    ui.dispatch(PointerPress{on, std::uint32_t{0x111}});  // another mouse button
    ui.dispatch(PointerRelease{on, std::uint32_t{0x111}});
    return count() == 1;
}
static_assert(button_click_rules());

// A dynamic button whose callback destroys itself and then reads its own captured state. If the
// storage were freed at once, this read would stop the file compiling.
constexpr bool self_destruction_is_deferred() {
    int observed = 0;
    auto kill = [&observed, text = std::string("state long enough to be held on the heap")](auto& u) -> void {
        u.template get<ItemTag>().hide();
        observed = static_cast<int>(text.size());
    };
    using KillButton = Button<ItemTag, decltype(kill)>;
    auto ui = window(Dynamic<ItemTag, KillButton, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<KillButton> handle = node.create("x", kill).value();
    static_cast<void>(ui.layout(Rect{0, 0, 50, 20}, BlockShaper{}));
    const Point on{1, 1};
    ui.dispatch(PointerPress{on, MouseButton::Left});
    ui.dispatch(PointerRelease{on, MouseButton::Left});
    if (observed != 40 || node.find(handle) != nullptr || node.shown()) return false;
    // A late event finds nothing, and a new item in the old slot is not reached by the old handle.
    ui.dispatch(PointerPress{on, MouseButton::Left});
    ui.dispatch(PointerRelease{on, MouseButton::Left});
    const Handle<KillButton> next = node.create("y", kill).value();
    return node.find(handle) == nullptr && node.find(next) != nullptr;
}
static_assert(self_destruction_is_deferred());

constexpr bool capture_and_focus_go_inert() {
    int fired = 0;
    auto fire = [&fired](auto&) -> void { ++fired; };
    using FireButton = Button<ItemTag, decltype(fire)>;
    auto ui = window(Dynamic<ItemTag, FireButton, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<FireButton> handle = node.create("x", fire).value();
    static_cast<void>(ui.layout(Rect{0, 0, 50, 20}, BlockShaper{}));
    const Point on{1, 1};
    ui.dispatch(PointerPress{on, MouseButton::Left});
    if (node.captured() == nullptr || node.focused() == nullptr || !ui.capture_active()) return false;
    node.destroy(handle);
    if (node.captured() != nullptr || node.focused() != nullptr || ui.capture_active()) return false;
    ui.dispatch(PointerRelease{on, MouseButton::Left});
    ui.dispatch(KeyEvent{Keysym::Return, 0, true});
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
    constexpr void paint(Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(Rect offered, const Shaper&) {
        area = Rect{offered.x(), offered.y(), offered.width(), 10};
        return Rect{offered.x(), offered.y() + 10, offered.width(), offered.height() - 10};
    }
    template <class U>
    constexpr bool handle(U&, const PointerPress& event) { return area.contains(event.position); }
    template <class U>
    constexpr bool handle(U&, const PointerMove& event) {
        ++moves;
        return area.contains(event.position);
    }
    template <class U>
    constexpr bool handle(U&, const PointerRelease&) {
        ++releases;
        return true;
    }
    template <class U>
    constexpr bool handle(U&, const KeyEvent&) {
        ++keys;
        return true;
    }
    template <class U, class E>
    constexpr bool handle(U&, const E&) { return false; }
    template <class U, class E>
    constexpr void notify(U&, const E&) {}
};
static_assert(Widget<Recorder>);

constexpr bool dynamic_routes_to_the_holder_only() {
    auto ui = window(Dynamic<ItemTag, Recorder, std::vector>{});
    auto& node = ui.get<ItemTag>();
    const Handle<Recorder> upper = node.create().value();
    const Handle<Recorder> lower = node.create().value();
    static_cast<void>(ui.layout(Rect{0, 0, 50, 40}, BlockShaper{}));
    if (node.find(upper)->bounds() != Rect{0, 0, 50, 10} || node.find(lower)->bounds() != Rect{0, 10, 50, 10}) return false;
    std::array<std::uint32_t, 1> pixel{};
    Canvas canvas{PixelBuffer::create(pixel, Size{1, 1}, 4).value()};
    ui.paint(canvas);
    ui.clear_damage();
    if (ui.damaged()) return false;
    const Point on_upper{5, 5};
    const Point on_lower{5, 15};

    // Pressing the lower item gives it the pointer and the focus.
    ui.dispatch(PointerPress{on_lower, MouseButton::Left});
    if (node.captured() != node.find(lower) || node.focused() != node.find(lower)) return false;

    // While it holds the pointer, motion over the other item goes only to the holder.
    ui.dispatch(PointerMove{on_upper, true});
    if (node.find(lower)->moves != 1 || node.find(upper)->moves != 0) return false;

    // So does the release, which ends the capture.
    ui.dispatch(PointerRelease{on_upper, MouseButton::Left});
    if (node.find(lower)->releases != 1 || node.find(upper)->releases != 0) return false;
    if (node.captured() != nullptr) return false;

    // A second release has no holder and reaches nobody.
    ui.dispatch(PointerRelease{on_upper, MouseButton::Left});
    if (node.find(lower)->releases != 1 || node.find(upper)->releases != 0) return false;

    // Keys go only to the focused item, and to nobody once the focus is cleared.
    ui.dispatch(KeyEvent{Keysym::Return, 0, true});
    if (node.find(lower)->keys != 1 || node.find(upper)->keys != 0) return false;
    ui.notify(ClearFocus{});
    ui.dispatch(KeyEvent{Keysym::Return, 0, true});
    if (node.find(lower)->keys != 1 || node.find(upper)->keys != 0) return false;

    // With no capture, motion reaches the topmost item first, and one that is hit stops the hit test.
    ui.dispatch(PointerMove{on_lower, false});
    return node.find(lower)->moves == 2 && node.find(upper)->moves == 0;
}
static_assert(dynamic_routes_to_the_holder_only());

constexpr bool shaper_errors_reach_layout() {
    auto ui = window(label<ClickTag>("\xFF"));
    const auto result = ui.layout(Rect{0, 0, 10, 10}, BlockShaper{});
    return !result.has_value() && result.error().code == ErrorCode::InvalidUtf8;
}
static_assert(shaper_errors_reach_layout());

constexpr bool label_new_text_is_checked_by_layout() {
    auto ui = window(label<ClickTag>("good"));
    if (!ui.layout(Rect{0, 0, 40, 40}, BlockShaper{}).has_value() || ui.damaged()) return false;
    ui.get<ClickTag>().set_text("\xC0\x80");
    if (!ui.damaged()) return false;
    if (ui.layout(Rect{0, 0, 40, 40}, BlockShaper{}).has_value()) return false;
    ui.get<ClickTag>().set_text("fine");
    return ui.layout(Rect{0, 0, 40, 40}, BlockShaper{}).has_value() && ui.get<ClickTag>().text() == "fine";
}
static_assert(label_new_text_is_checked_by_layout());

// --- The real demo tree and application logic, from injected events to pixels, during compilation ---
constexpr Size small_window{120, 60};

constexpr bool demo_help_toggles_in_pixels() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value() || core.frames() != 1) return false;

    const Rect target = core.ui().get<demo::HelpButton>().bounds();
    const Point on{target.x() + 1, target.y() + 1};
    const Rect bar = core.ui().get<MenuBarTag>().bounds();
    const Rect below{bar.x(), bar.bottom(), bar.width(), Frame::client_rect(small_window).bottom() - bar.bottom()};
    const auto text_pixels = [&] {
        int count = 0;
        for (std::int32_t y = below.y(); y < below.bottom(); ++y)
            for (std::int32_t x = below.x(); x < below.right(); ++x)
                if (core.backend().pixel(Point{x, y}) != theme::background.argb) ++count;
        return count;
    };
    const auto click = [&] {
        core.backend().inject(PointerButton{on, MouseButton::Left, ButtonState::Pressed, 1});
        core.backend().inject(PointerButton{on, MouseButton::Left, ButtonState::Released, 2});
        return core.process_pending(shaper).has_value();
    };
    if (text_pixels() != 0) return false;
    if (!click() || text_pixels() == 0 || !core.ui().get<demo::HelpLine>().shown()) return false;
    if (!click() || text_pixels() != 0 || core.ui().get<demo::HelpLine>().shown()) return false;
    return true;
}
static_assert(demo_help_toggles_in_pixels());

constexpr bool frame_routing_and_refused_sizes() {
    ApplicationCore core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    const auto frames = core.frames();

    // No damage, no redraw.
    if (!core.process_pending(shaper).has_value() || core.frames() != frames) return false;

    // A title press starts a move with its serial, and an edge press a resize.
    core.backend().inject(PointerButton{Point{60, 10}, MouseButton::Left, ButtonState::Pressed, 11});
    core.backend().inject(PointerButton{Point{0, 30}, MouseButton::Left, ButtonState::Pressed, 12});
    if (!core.process_pending(shaper).has_value()) return false;
    if (core.backend().move_serial() != 11U || core.backend().resize_serial() != 12U) return false;
    if (core.backend().resize_edge() != ResizeEdge::Left) return false;

    // An absurd size is refused and leaves the window as it was.
    core.backend().inject(ResizeEvent{Size{int_max, int_max}});
    const auto refused_result = core.process_pending(shaper);
    if (refused_result.has_value() || refused_result.error().code != ErrorCode::SizeLimit) return false;
    if (core.size() != small_window) return false;

    // Press on the close box, release off it: nothing. Press and release on it: closed.
    const Point close = Point{Frame::close_box(small_window).x() + 1, Frame::close_box(small_window).y() + 1};
    core.backend().inject(PointerButton{close, MouseButton::Left, ButtonState::Pressed, 13});
    core.backend().inject(PointerButton{Point{60, 40}, MouseButton::Left, ButtonState::Released, 14});
    if (!core.process_pending(shaper).has_value() || core.closed()) return false;
    core.backend().inject(PointerButton{close, MouseButton::Left, ButtonState::Pressed, 15});
    core.backend().inject(PointerButton{close, MouseButton::Left, ButtonState::Released, 16});
    return core.process_pending(shaper).has_value() && core.closed();
}
static_assert(frame_routing_and_refused_sizes());

// The frame is an ordinary node: the tree finds it and what it holds, and a childless frame works.
constexpr bool frame_is_a_node_of_the_tree() {
    auto ui = window(window_frame<ThickMetrics>(menu_bar(button<ClickTag>("x", bump_on_click)), Counter{}));
    const Rect whole{0, 0, 120, 80};
    ui.set_size(whole.size());
    const auto left = ui.layout(whole, BlockShaper{});
    if (!left.has_value() || !left->empty()) return false;
    if (ui.get<WindowFrameTag>().bounds() != whole) return false;
    if (ui.get<MenuBarTag>().bounds().origin() != ThickFrame::client_rect(whole.size()).origin()) return false;
    if (ui.get<ClickTag>().bounds().intersect(ui.get<MenuBarTag>().bounds()).empty()) return false;

    auto empty = window(window_frame());
    empty.set_size(whole.size());
    if (!empty.layout(whole, BlockShaper{}).has_value()) return false;
    empty.dispatch(PointerPress{Point{60, 10}, MouseButton::Left, 9});
    return empty.take_requests().move == 9U && !empty.take_requests().move.has_value();
}
static_assert(frame_is_a_node_of_the_tree());

// A frame of another size routes and paints at that size, all the way through the application.
constexpr bool thick_frame_through_the_application() {
    ApplicationCore core{HeadlessBackend{small_window}, window(window_frame<ThickMetrics>(label<ClickTag>("x")))};
    const BlockShaper shaper;
    if (!core.process_pending(shaper).has_value()) return false;
    core.backend().inject(PointerButton{Point{6, 30}, MouseButton::Left, ButtonState::Pressed, 21});
    core.backend().inject(PointerButton{Point{60, 28}, MouseButton::Left, ButtonState::Pressed, 22});
    if (!core.process_pending(shaper).has_value()) return false;
    if (core.backend().resize_serial() != 21U || core.backend().resize_edge() != ResizeEdge::Left) return false;
    if (core.backend().move_serial() != 22U) return false;
    return core.backend().pixel(Point{6, 30}) == theme::frame_strip.argb && core.backend().pixel(Point{60, 28}) == theme::frame_strip.argb;
}
static_assert(thick_frame_through_the_application());

// The frame's rules, pinned as they were when it sat outside the tree. Each evaluation paints the
// window several times, so they are split to stay inside the compiler's default operation limit.
struct FrameRig {
    ApplicationCore<HeadlessBackend, decltype(demo::make_ui<std::vector>())> core{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    BlockShaper shaper{};

    [[nodiscard]] constexpr bool start() { return core.process_pending(shaper).has_value(); }
    template <class E>
    [[nodiscard]] constexpr bool send(const E& event) {
        core.backend().inject(event);
        return core.process_pending(shaper).has_value();
    }
    [[nodiscard]] constexpr bool left(Point at, ButtonState state, std::uint32_t serial) {
        return send(PointerButton{at, MouseButton::Left, state, serial});
    }
    [[nodiscard]] constexpr auto& frame() { return core.ui().get<WindowFrameTag>(); }
    [[nodiscard]] constexpr auto& help() { return core.ui().get<demo::HelpButton>(); }
    [[nodiscard]] constexpr Point on_help() {
        return Point{help().bounds().x() + 1, help().bounds().y() + 1};
    }
};
constexpr Rect small_box = Frame::close_box(small_window);
constexpr Point on_close{small_box.x() + 1, small_box.y() + 1};
constexpr Point on_title{60, 10};
constexpr Point on_client{60, 50};
constexpr std::uint32_t right_button = 0x111;

constexpr bool hovering_the_close_box_redraws_once() {
    FrameRig rig;
    if (!rig.start()) return false;
    const auto frames = rig.core.frames();
    if (!rig.send(PointerMove{on_close}) || rig.core.frames() != frames + 1 || !rig.frame().close_hovered()) return false;
    if (rig.core.backend().pixel(Point{small_box.x() + 1, small_box.y() + 2}) != theme::close_hover.argb) return false;
    return rig.send(PointerMove{on_close}) && rig.core.frames() == frames + 1;
}
static_assert(hovering_the_close_box_redraws_once());

constexpr bool close_stays_armed_over_a_widget_and_back() {
    FrameRig rig;
    if (!rig.start() || !rig.send(PointerMove{on_close})) return false;
    if (!rig.left(on_close, ButtonState::Pressed, 31) || !rig.frame().close_armed()) return false;
    if (rig.core.backend().pixel(Point{small_box.x() + 1, small_box.y() + 2}) != theme::close_pressed.argb) return false;
    if (!rig.send(PointerMove{rig.on_help()}) || !rig.frame().close_armed() || rig.frame().close_hovered()) return false;
    if (!rig.help().hovered()) return false;
    if (!rig.send(PointerMove{on_close}) || !rig.frame().close_armed() || rig.core.closed()) return false;
    return rig.left(on_close, ButtonState::Released, 32) && rig.core.closed();
}
static_assert(close_stays_armed_over_a_widget_and_back());

constexpr bool leaving_the_window_disarms_close() {
    FrameRig rig;
    if (!rig.start() || !rig.left(on_close, ButtonState::Pressed, 33)) return false;
    if (!rig.send(PointerLeave{}) || rig.frame().close_armed() || rig.frame().close_hovered()) return false;
    return rig.left(on_close, ButtonState::Released, 34) && !rig.core.closed();
}
static_assert(leaving_the_window_disarms_close());

constexpr bool only_the_left_button_acts_on_the_frame() {
    FrameRig rig;
    if (!rig.start()) return false;
    if (!rig.send(PointerButton{on_title, right_button, ButtonState::Pressed, 35})) return false;
    if (rig.core.backend().move_serial().has_value()) return false;
    return rig.send(PointerButton{on_close, right_button, ButtonState::Pressed, 36}) && !rig.frame().close_armed();
}
static_assert(only_the_left_button_acts_on_the_frame());

constexpr bool a_captured_drag_over_the_frame_starts_nothing() {
    FrameRig rig;
    if (!rig.start()) return false;
    const Point help = rig.on_help();
    if (!rig.left(help, ButtonState::Pressed, 37) || !rig.core.ui().capture_active()) return false;
    if (!rig.send(PointerMove{on_title}) || !rig.help().pressed()) return false;
    if (!rig.left(help, ButtonState::Released, 38)) return false;
    return !rig.core.backend().move_serial().has_value() && !rig.core.backend().resize_serial().has_value();
}
static_assert(a_captured_drag_over_the_frame_starts_nothing());

constexpr bool only_a_client_press_takes_the_focus_away() {
    FrameRig rig;
    if (!rig.start()) return false;
    const Point help = rig.on_help();
    if (!rig.left(help, ButtonState::Pressed, 39) || !rig.left(help, ButtonState::Released, 40) || !rig.help().focused()) return false;
    if (!rig.left(on_title, ButtonState::Pressed, 41) || !rig.help().focused()) return false;
    if (rig.core.backend().move_serial() != 41U) return false;
    return rig.left(on_client, ButtonState::Pressed, 42) && !rig.help().focused();
}
static_assert(only_a_client_press_takes_the_focus_away());

constexpr bool moving_off_the_client_area_is_a_leave() {
    FrameRig rig;
    if (!rig.start() || !rig.send(PointerMove{rig.on_help()}) || !rig.help().hovered()) return false;
    return rig.send(PointerMove{on_title}) && !rig.help().hovered();
}
static_assert(moving_off_the_client_area_is_a_leave());

// A press queued behind a resize is judged at the new size, before any layout has seen it.
constexpr bool a_press_behind_a_resize_uses_the_new_size() {
    FrameRig rig;
    if (!rig.start()) return false;
    rig.core.backend().inject(ResizeEvent{Size{300, 200}});
    if (!rig.left(Point{299, 100}, ButtonState::Pressed, 43)) return false;
    return rig.core.backend().resize_serial() == 43U && rig.core.backend().resize_edge() == ResizeEdge::Right;
}
static_assert(a_press_behind_a_resize_uses_the_new_size());

constexpr bool run_ends_on_close_and_empty_wait_is_an_error() {
    ApplicationCore closes{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    closes.backend().inject(CloseEvent{});
    if (!closes.run(BlockShaper{}).has_value()) return false;
    ApplicationCore waits{HeadlessBackend{small_window}, demo::make_ui<std::vector>()};
    const auto result = waits.run(BlockShaper{});
    return !result.has_value() && result.error().code == ErrorCode::WouldBlock;
}
static_assert(run_ends_on_close_and_empty_wait_is_an_error());

}  // namespace

int main() { return 0; }
