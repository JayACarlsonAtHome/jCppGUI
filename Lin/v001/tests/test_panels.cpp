// File: tests/test_panels.cpp
// Purpose: The panel helpers, each a static_assert, so this file compiling is the test: halves and quarters at even, odd,
//          zero, one and the largest sizes, and split_remaining_space with too much, too little and nothing to share.
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

#include "jCppGUI/v001/panels.hpp"

namespace {

using namespace jgui;

constexpr std::int32_t int_max = std::numeric_limits<std::int32_t>::max();

[[nodiscard]] constexpr Rect rect_at(std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height) noexcept {
    return Rect{Point{Left{left}, Top{top}}, Size{Width{width}, Height{height}}};
}

// The two sides of a cut meet with no gap or overlap, stay the parent's other way, and add up to the parent.
[[nodiscard]] constexpr bool splits_across(const Rect& parent, const Rect& first, const Rect& second) noexcept {
    return first.origin() == parent.origin() && left_after(first.right()) == second.left() && second.right() == parent.right() && first.top() == parent.top() && second.top() == parent.top() &&
           first.height() == parent.height() && second.height() == parent.height();
}
[[nodiscard]] constexpr bool splits_down(const Rect& parent, const Rect& first, const Rect& second) noexcept {
    return first.origin() == parent.origin() && top_after(first.bottom()) == second.top() && second.bottom() == parent.bottom() && first.left() == parent.left() && second.left() == parent.left() &&
           first.width() == parent.width() && second.width() == parent.width();
}
[[nodiscard]] constexpr bool halves_and_quarters_tile(const Rect& parent) noexcept {
    return full_of(parent) == parent && splits_across(parent, left_half_of(parent), right_half_of(parent)) && splits_down(parent, top_half_of(parent), bottom_half_of(parent)) &&
           splits_down(left_half_of(parent), top_left_quarter_of(parent), bottom_left_quarter_of(parent)) && splits_down(right_half_of(parent), top_right_quarter_of(parent), bottom_right_quarter_of(parent)) &&
           splits_across(top_half_of(parent), top_left_quarter_of(parent), top_half_of(right_half_of(parent))) && top_right_quarter_of(parent) == top_half_of(right_half_of(parent));
}

// --- A length cut in two ---
static_assert(smaller_half_of(Width{}) == Width{} && larger_half_of(Width{}) == Width{});
static_assert(smaller_half_of(Width{1}) == Width{} && larger_half_of(Width{1}) == Width{1}, "one pixel goes to the larger side");
static_assert(smaller_half_of(Height{7}) == Height{3} && larger_half_of(Height{7}) == Height{4});
static_assert(smaller_half_of(Width{8}) == Width{4} && larger_half_of(Width{8}) == Width{4});
static_assert(larger_half_of(Width{int_max}) + smaller_half_of(Width{int_max}) == Width{int_max});
static_assert(larger_half_of(Width{int_max}) == Width{int_max / 2 + 1});

// --- Which side takes the extra pixel ---
static_assert(left_half_of(rect_at(10, 20, 7, 5)) == rect_at(10, 20, 4, 5), "left takes the extra pixel");
static_assert(right_half_of(rect_at(10, 20, 7, 5)) == rect_at(14, 20, 3, 5));
static_assert(top_half_of(rect_at(10, 20, 7, 5)) == rect_at(10, 20, 7, 3), "top takes the extra pixel");
static_assert(bottom_half_of(rect_at(10, 20, 7, 5)) == rect_at(10, 23, 7, 2));
static_assert(top_left_quarter_of(rect_at(0, 0, 7, 5)) == rect_at(0, 0, 4, 3));
static_assert(top_right_quarter_of(rect_at(0, 0, 7, 5)) == rect_at(4, 0, 3, 3));
static_assert(bottom_left_quarter_of(rect_at(0, 0, 7, 5)) == rect_at(0, 3, 4, 2));
static_assert(bottom_right_quarter_of(rect_at(0, 0, 7, 5)) == rect_at(4, 3, 3, 2));

// --- Nothing is lost, at every size that could go wrong ---
static_assert(halves_and_quarters_tile(rect_at(0, 0, 640, 480)));
static_assert(halves_and_quarters_tile(rect_at(3, 9, 641, 481)), "odd");
static_assert(halves_and_quarters_tile(rect_at(-50, -90, 3, 7)), "odd, from a negative corner");
static_assert(halves_and_quarters_tile(rect_at(5, 5, 0, 0)), "zero");
static_assert(halves_and_quarters_tile(rect_at(5, 5, 1, 1)), "one");
static_assert(halves_and_quarters_tile(rect_at(5, 5, 1, 0)), "one and zero");
static_assert(halves_and_quarters_tile(rect_at(0, 0, int_max, int_max)), "the largest");
static_assert(right_half_of(rect_at(5, 5, 1, 1)).empty(), "the other side of one pixel is empty, at the right place");
static_assert(right_half_of(rect_at(5, 5, 1, 1)).left() == Left{6});

// --- Sharing an axis ---
using WidePanel = PanelSize<detail::WidthTag>;
using TallPanel = PanelSize<detail::HeightTag>;

template <std::size_t N>
[[nodiscard]] constexpr std::vector<Width> split_across(std::int32_t length, const std::array<WidePanel, N>& panels) {
    return split_remaining_space(Width{length}, std::span<const WidePanel>{panels});
}
[[nodiscard]] constexpr bool sizes_are(const std::vector<Width>& sizes, std::initializer_list<std::int32_t> wanted) {
    std::size_t i = 0;
    if (sizes.size() != wanted.size()) return false;
    for (const std::int32_t size : wanted)
        if (sizes[i++] != Width{size}) return false;
    return true;
}
constexpr WidePanel fills{};
[[nodiscard]] constexpr WidePanel fixed_at(std::int32_t size) noexcept { return WidePanel::fixed(Width{size}); }

static_assert(!WidePanel{}.fixed_size(), "filling is the default");
static_assert(sizes_are(split_across<0>(100, {}), {}), "no panels");
static_assert(sizes_are(split_across<1>(100, {fills}), {100}));
static_assert(sizes_are(split_across<3>(100, {fixed_at(10), fills, fixed_at(20)}), {10, 70, 20}), "fixed first, wherever they stand");
static_assert(sizes_are(split_across<2>(10, {fills, fills}), {5, 5}));
static_assert(sizes_are(split_across<3>(11, {fills, fills, fills}), {4, 4, 3}), "the earlier panels take the extra pixels");
static_assert(sizes_are(split_across<3>(2, {fills, fills, fills}), {1, 1, 0}));
static_assert(sizes_are(split_across<3>(0, {fills, fixed_at(5), fills}), {0, 0, 0}), "no length at all");
static_assert(sizes_are(split_across<3>(25, {fixed_at(10), fixed_at(20), fixed_at(30)}), {10, 15, 0}), "clipped in order, never negative");
static_assert(sizes_are(split_across<3>(10, {fixed_at(10), fills, fixed_at(0)}), {10, 0, 0}), "nothing is left to fill");
static_assert(sizes_are(split_across<2>(100, {fixed_at(10), fixed_at(20)}), {10, 20}), "with no filling panel the rest stays unused");
static_assert(sizes_are(split_across<2>(int_max, {fixed_at(int_max), fixed_at(int_max)}), {int_max, 0}), "two of the largest cannot overflow");
static_assert(sizes_are(split_across<3>(int_max, {fills, fixed_at(1), fills}), {(int_max - 1) / 2, 1, (int_max - 1) / 2}));
static_assert(sizes_are(split_across<2>(int_max, {fills, fills}), {int_max / 2 + 1, int_max / 2}));

// The other axis is its own type, and takes the same rule.
static_assert(split_remaining_space(Height{9}, std::span<const TallPanel>{std::array<TallPanel, 2>{TallPanel::fixed(Height{4}), TallPanel{}}}).back() == Height{5});

// A list longer than anything real shares a short length without losing a pixel.
[[nodiscard]] constexpr bool many_panels_add_up() {
    const std::vector<WidePanel> panels(1000);
    const std::vector<Width> sizes = split_remaining_space(Width{1234}, std::span<const WidePanel>{panels});
    Width total{};
    for (const Width size : sizes) {
        if (size != Width{1} && size != Width{2}) return false;
        total = total + size;
    }
    return total == Width{1234};
}
static_assert(many_panels_add_up());

}  // namespace

int main() {
    std::cout << "the panel helpers hold at compile time\n";
    return 0;
}
