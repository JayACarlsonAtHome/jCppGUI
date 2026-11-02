// File: tests/test_canvas.cpp
// Purpose: Tests 2 and 3: drawing outside the buffer writes nothing out of bounds, checked by a guard
//          region, and PixelBuffer refuses oversize and multiplication-overflow sizes. Also what a widget that
//          keeps a canvas past its frame gets: an abort, and no write to memory that has moved.
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"
#include "jCppGUI/v001/canvas.hpp"

// A prvalue from clipped can initialize a member, so a widget can keep a live Canvas after all. A copy or a move
// would not compile: see tests/compile_fail/canvas_stored.cpp. This one must not work once its frame is over.
struct KeptCanvas {
    jgui::Canvas canvas;
    explicit KeptCanvas(const jgui::Canvas& given) noexcept : canvas{given.clipped(jgui::Rect{jgui::Point{}, jgui::Size{jgui::Width{4}, jgui::Height{4}}})} {}
};

// Outside the anonymous namespace because only the Widget concept reads bounds, and a compiler rightly reports a
// member nobody calls on a type with internal linkage.
struct Keeper {
    using tag = Keeper;
    std::unique_ptr<KeptCanvas> kept{};
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return jgui::Rect{}; }
    void paint(const jgui::TreeKey&, jgui::Canvas& canvas) {
        if (!kept) kept = std::make_unique<KeptCanvas>(canvas);
        kept->canvas.fill_rect(jgui::Rect{jgui::Point{}, jgui::Size{jgui::Width{1}, jgui::Height{1}}}, jgui::Color{0xFF0000FFU});
    }
    template <class S>
    std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey&, jgui::Rect area, const S&) { return area; }
    template <class U, class E>
    bool handle(const jgui::TreeKey&, U&, const E&) { return false; }
    template <class U, class E>
    void notify(const jgui::TreeKey&, U&, const E&) {}
};

namespace {

// The reproduction of the use after free, on the path it took: a resize reallocates the pixels, and the kept
// canvas is used after the frame it was given for. How it dies is the test: an abort, and no ASan report.
template <class Use>
[[nodiscard]] bool kept_canvas_aborts(Use use) {
    std::cout.flush();
    const pid_t child = ::fork();
    if (child == 0) {
        auto app = jgui::make_application(jgui::HeadlessBackend{jgui::Size{jgui::Width{8}, jgui::Height{8}}}, jgui::window(Keeper{}));
        if (!app || !app->process_pending()) ::_exit(2);
        // It drew once while its frame was current. That frame is over, and a kept canvas must stop here.
        use(*app);
        ::_exit(0);
    }
    int status = 0;
    if (child < 0 || ::waitpid(child, &status, 0) != child) return false;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

void test_kept_canvas(jgui_test::Checker& check) {
    using jgui::Height;
    using jgui::Width;
    check(kept_canvas_aborts([](auto& app) {
              app.ui().template get<Keeper>().kept->canvas.fill_rect(jgui::Rect{jgui::Point{}, jgui::Size{Width{1}, Height{1}}}, jgui::Color{1});
          }),
          "a kept canvas aborts as soon as its frame is over");
    check(kept_canvas_aborts([](auto& app) {
              app.ui().template get<Keeper>().kept->canvas.stroke_rect(jgui::Rect{jgui::Point{}, jgui::Size{Width{1}, Height{1}}}, jgui::Color{1});
          }),
          "so does an outline");
    check(kept_canvas_aborts([](auto& app) {
              app.ui().template get<Keeper>().kept->canvas.draw_text(jgui::Point{}, jgui::TextRun{}, jgui::Color{1});
          }),
          "and text");
    check(kept_canvas_aborts([](auto& app) {
              // The second frame's paint uses what the first one kept, after the pixels moved.
              jgui_test::backend(app).inject(jgui::ResizeEvent{jgui::Size{Width{2000}, Height{2000}}});
              static_cast<void>(app.process_pending());
          }),
          "and the original reproduction: the next frame reallocated the pixels");
}

}  // namespace

int main() {
    using namespace jgui;
    jgui_test::Checker check;
    test_kept_canvas(check);
    const std::int32_t hi = std::numeric_limits<std::int32_t>::max();
    const std::int32_t lo = std::numeric_limits<std::int32_t>::min();

    // Test 2: a 16x16 buffer inside a larger block of guard pixels, with rows 32 pixels apart.
    constexpr std::uint32_t guard = 0xDEADBEEFU;
    std::vector<std::uint32_t> storage(32U * 32U, guard);
    const std::size_t start = 8U * 32U + 8U;
    const std::span<std::uint32_t> inner = std::span{storage}.subspan(start);
    auto buffer = PixelBuffer::create(inner, Size{Width{16}, Height{16}}, 32 * 4);
    check(buffer.has_value(), "buffer inside the guard region");
    const Canvas canvas{*buffer};
    const std::array shapes{Rect{Point{Left{lo}, Top{lo}}, Size{Width{hi}, Height{hi}}},  Rect{Point{Left{hi}, Top{hi}}, Size{Width{hi}, Height{hi}}},     Rect{Point{Left{-100}, Top{-100}}, Size{Width{100}, Height{100}}},
                            Rect{Point{Left{16}, Top{0}}, Size{Width{50}, Height{50}}},   Rect{Point{Left{0}, Top{16}}, Size{Width{50}, Height{50}}},      Rect{Point{Left{-5}, Top{3}}, Size{Width{10}, Height{2}}},
                            Rect{Point{Left{15}, Top{15}}, Size{Width{hi}, Height{hi}}},  Rect{Point{Left{-1}, Top{-1}}, Size{Width{18}, Height{18}}},     Rect{Point{Left{lo}, Top{0}}, Size{Width{hi}, Height{16}}},
                            Rect{Point{Left{0}, Top{hi - 1}}, Size{Width{8}, Height{8}}}, Rect{Point{Left{hi - 1}, Top{0}}, Size{Width{8}, Height{8}}},    Rect{Point{Left{4}, Top{4}}, Size{Width{0}, Height{0}}}};
    for (const Rect& shape : shapes) {
        canvas.fill_rect(shape, Color{0xFF010203U});
        canvas.stroke_rect(shape, Color{0xFF040506U});
        canvas.clipped(Rect{Point{Left{2}, Top{2}}, Size{Width{4}, Height{4}}}).fill_rect(shape, Color{0xFF070809U});
    }
    int guards_broken = 0;
    for (std::size_t y = 0; y < 32; ++y) {
        for (std::size_t x = 0; x < 32; ++x) {
            const bool in_buffer = y >= 8 && y < 24 && x >= 8 && x < 24;
            if (!in_buffer && storage[y * 32 + x] != guard) ++guards_broken;
        }
    }
    check(guards_broken == 0, "no write outside the buffer");
    check(storage[start] != guard, "drawing did reach the buffer");

    // Test 3
    const auto refused = [](Size size) { return !PixelBuffer::byte_size(size).has_value(); };
    check(refused(Size{Width{8193}, Height{1}}), "width over the limit");
    check(refused(Size{Width{1}, Height{8193}}), "height over the limit");
    check(refused(Size{Width{hi}, Height{hi}}), "both sides at the int32_t maximum");
    check(refused(Size{Width{65536}, Height{65536}}), "the product wraps to zero in 32 bits");
    check(refused(Size{Width{46341}, Height{46341}}), "the product wraps past int32_t");
    check(PixelBuffer::byte_size(Size{Width{8192}, Height{8192}}).value_or(0) == std::size_t{1} << 28U, "exactly at the limit");
    std::array<std::uint32_t, 4> few{};
    check(!PixelBuffer::create(few, Size{Width{4}, Height{4}}, 16).has_value(), "storage smaller than the size");
    check(!PixelBuffer::create(few, Size{Width{2}, Height{2}}, 4).has_value(), "stride under the width");
    check(!PixelBuffer::create(few, Size{Width{2}, Height{2}}, 9).has_value(), "stride not whole pixels");
    check(PixelBuffer::create(few, Size{Width{2}, Height{2}}, 8).has_value(), "a valid buffer");
    return check.finish();
}
