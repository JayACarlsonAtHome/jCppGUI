// File: tests/test_canvas.cpp
// Purpose: Tests 2 and 3: drawing outside the buffer writes nothing out of bounds, checked by a guard
//          region, and PixelBuffer refuses oversize and multiplication-overflow sizes.
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "check.hpp"
#include "jCppGUI/v001/canvas.hpp"

int main() {
    using namespace jgui;
    jgui_test::Checker check;
    const std::int32_t hi = std::numeric_limits<std::int32_t>::max();
    const std::int32_t lo = std::numeric_limits<std::int32_t>::min();

    // Test 2: a 16x16 buffer inside a larger block of guard pixels, with rows 32 pixels apart.
    constexpr std::uint32_t guard = 0xDEADBEEFU;
    std::vector<std::uint32_t> storage(32U * 32U, guard);
    const std::size_t start = 8U * 32U + 8U;
    const std::span<std::uint32_t> inner = std::span{storage}.subspan(start);
    auto buffer = PixelBuffer::create(inner, Size{16, 16}, 32 * 4);
    check(buffer.has_value(), "buffer inside the guard region");
    const Canvas canvas{*buffer};
    const std::array shapes{Rect{lo, lo, hi, hi},  Rect{hi, hi, hi, hi},     Rect{-100, -100, 100, 100},
                            Rect{16, 0, 50, 50},   Rect{0, 16, 50, 50},      Rect{-5, 3, 10, 2},
                            Rect{15, 15, hi, hi},  Rect{-1, -1, 18, 18},     Rect{lo, 0, hi, 16},
                            Rect{0, hi - 1, 8, 8}, Rect{hi - 1, 0, 8, 8},    Rect{4, 4, 0, 0}};
    for (const Rect& shape : shapes) {
        canvas.fill_rect(shape, Color{0xFF010203U});
        canvas.stroke_rect(shape, Color{0xFF040506U});
        canvas.clipped(Rect{2, 2, 4, 4}).fill_rect(shape, Color{0xFF070809U});
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
    check(refused(Size{16385, 1}), "width over the limit");
    check(refused(Size{1, 16385}), "height over the limit");
    check(refused(Size{hi, hi}), "both sides at the int32_t maximum");
    check(refused(Size{65536, 65536}), "the product wraps to zero in 32 bits");
    check(refused(Size{46341, 46341}), "the product wraps past int32_t");
    check(PixelBuffer::byte_size(Size{16384, 16384}).value_or(0) == std::size_t{1} << 30U, "exactly at both limits");
    std::array<std::uint32_t, 4> few{};
    check(!PixelBuffer::create(few, Size{4, 4}, 16).has_value(), "storage smaller than the size");
    check(!PixelBuffer::create(few, Size{2, 2}, 4).has_value(), "stride under the width");
    check(!PixelBuffer::create(few, Size{2, 2}, 9).has_value(), "stride not whole pixels");
    check(PixelBuffer::create(few, Size{2, 2}, 8).has_value(), "a valid buffer");
    return check.finish();
}
