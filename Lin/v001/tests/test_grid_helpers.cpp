// File: tests/test_grid_helpers.cpp
// Purpose: The small helpers the data grid is built from, each on its own: scrolling, spans, selection steps, sorting
//          through indices and cell text. Each is a static_assert, so this file compiling is the test.
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "jCppGUI/v001/jgui.hpp"

namespace {

using namespace jgui;

constexpr std::int32_t int_max = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t int_min = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t size_most = std::numeric_limits<std::size_t>::max();

// --- Lengths multiplied and divided ---
static_assert(repeated_extent(Height{30}, 1'000'000) == Height{30'000'000});
static_assert(repeated_extent(Height{2048}, max_grid_rows) == Height{2'048'000'000});
static_assert(repeated_extent(Height{}, size_most) == Height{} && repeated_extent(Height{5}, 0) == Height{});
static_assert(repeated_extent(Height{int_max}, 2) == Height{int_max} && repeated_extent(Width{1}, size_most) == Width{int_max});
static_assert(repeated_extent(Height{int_max}, 1) == Height{int_max});
static_assert(whole_units_in(Height{100}, Height{}) == 0 && whole_units_in(Height{100}, Height{30}) == 3 && whole_units_in(Height{}, Height{30}) == 0);
static_assert(whole_units_in(Width{int_max}, Width{1}) == static_cast<std::size_t>(int_max));
static_assert(rescaled_extent(Height{5}, Height{}, Height{100}) == Height{});
static_assert(rescaled_extent(Height{int_max}, Height{int_max}, Height{int_max}) == Height{int_max}, "the middle of the sum goes past 32 bits");
static_assert(rescaled_extent(Height{int_max}, Height{1}, Height{int_max}) == Height{int_max}, "and the answer saturates");
static_assert(rescaled_extent(Width{50}, Width{100}, Width{7}) == Width{3});

// --- How far an offset can go ---
static_assert(clamped_scroll_offset(Height{50}, Height{}, Height{100}) == Height{}, "no content");
static_assert(clamped_scroll_offset(Height{50}, Height{20}, Height{100}) == Height{}, "a view bigger than the content");
static_assert(clamped_scroll_offset(Height{50}, Height{100}, Height{100}) == Height{}, "a view exactly the content");
static_assert(clamped_scroll_offset(Height{50}, Height{120}, Height{100}) == Height{20});
static_assert(clamped_scroll_offset(Height{10}, Height{120}, Height{100}) == Height{10});
static_assert(clamped_scroll_offset(Height{int_max}, repeated_extent(Height{20}, max_grid_rows), Height{400}) == Height{19'999'600}, "a million rows");
static_assert(clamped_scroll_offset(Height{int_max}, Height{int_max}, Height{}) == Height{int_max}, "the greatest of everything");
static_assert(clamped_scroll_offset(Width{int_max}, Width{1}, Width{1}) == Width{}, "a one pixel window");

// --- An amount of scrolling ---
static_assert(ScrollDown::of_steps(int_max).steps() == max_scroll_steps && ScrollDown::of_steps(int_min).steps() == -max_scroll_steps);
static_assert(ScrollDown::of_pixels(int_max).pixels() == max_scroll_pixels && ScrollDown::of_pixels(int_min).pixels() == -max_scroll_pixels);
static_assert(ScrollDown::of_steps(0).none() && ScrollDown::of_pixels(0).none() && !ScrollDown::of_steps(1).none() && ScrollDown{}.none());
static_assert(scroll_distance(ScrollDown::of_steps(-2), Height{60}) == Height{120});
static_assert(scroll_distance(ScrollDown::of_pixels(-7), Height{60}) == Height{7});
static_assert(scroll_distance(ScrollDown::of_steps(16), Height{int_max}) == Height{int_max});
static_assert(offset_after_scroll(Height{100}, ScrollDown::of_steps(1), Height{60}) == Height{160});
static_assert(offset_after_scroll(Height{100}, ScrollDown::of_steps(-1), Height{60}) == Height{40});
static_assert(offset_after_scroll(Height{10}, ScrollDown::of_pixels(-11), Height{60}) == Height{}, "never before the start");
static_assert(offset_after_scroll(Height{int_max}, ScrollDown::of_pixels(5), Height{60}) == Height{int_max}, "and it saturates");
static_assert(offset_after_scroll(Width{9}, ScrollAcross{}, Width{60}) == Width{9});

// --- Bringing a piece into view ---
static_assert(offset_to_reveal(Height{100}, Height{120}, Height{20}, Height{200}) == Height{100}, "already showing");
static_assert(offset_to_reveal(Height{100}, Height{80}, Height{20}, Height{200}) == Height{80}, "above");
static_assert(offset_to_reveal(Height{100}, Height{300}, Height{20}, Height{200}) == Height{120}, "below");
static_assert(offset_to_reveal(Height{100}, Height{280}, Height{20}, Height{200}) == Height{100}, "just fits");
static_assert(offset_to_reveal(Height{100}, Height{300}, Height{500}, Height{200}) == Height{300}, "longer than the view: its start");
static_assert(offset_to_reveal(Height{100}, Height{300}, Height{20}, Height{}) == Height{300}, "no view at all");
static_assert(offset_to_reveal(Height{}, Height{int_max}, Height{int_max}, Height{1}) == Height{int_max});

// --- A thumb ---
static_assert(scroll_thumb_length(Height{100}, Height{100}, Height{100}, Height{16}) == Height{}, "nothing to scroll");
static_assert(scroll_thumb_length(Height{1000}, Height{100}, Height{100}, Height{16}) == Height{16}, "its share would be 10");
static_assert(scroll_thumb_length(Height{200}, Height{100}, Height{100}, Height{16}) == Height{50});
static_assert(scroll_thumb_length(Height{200}, Height{100}, Height{10}, Height{16}) == Height{10}, "never more than the track");
static_assert(scroll_thumb_length(Height{200}, Height{100}, Height{}, Height{16}) == Height{}, "no track");
static_assert(scroll_thumb_start(Height{}, Height{200}, Height{100}, Height{100}, Height{50}) == Height{});
static_assert(scroll_thumb_start(Height{100}, Height{200}, Height{100}, Height{100}, Height{50}) == Height{50}, "last offset, end of the track");
static_assert(scroll_thumb_start(Height{50}, Height{100}, Height{100}, Height{100}, Height{}) == Height{}, "nothing to scroll: no division by zero");
static_assert(offset_for_thumb_start(Height{50}, Height{200}, Height{100}, Height{100}, Height{50}) == Height{100});
static_assert(offset_for_thumb_start(Height{int_max}, Height{200}, Height{100}, Height{100}, Height{50}) == Height{100}, "past the end is the last offset");
static_assert(offset_for_thumb_start(Height{10}, Height{200}, Height{100}, Height{50}, Height{50}) == Height{}, "a thumb filling its track has nowhere to go");
static_assert(offset_for_thumb_start(Height{384}, repeated_extent(Height{20}, max_grid_rows), Height{400}, Height{400}, Height{16}) == Height{19'999'600}, "a million rows");

// --- Which bars ---
constexpr Width bar_across{12};
constexpr Height bar_down{12};
[[nodiscard]] consteval ScrollBarsNeeded bars(std::int32_t content_w, std::int32_t content_h, std::int32_t room_w, std::int32_t room_h) {
    return scroll_bars_needed(Size{Width{content_w}, Height{content_h}}, Size{Width{room_w}, Height{room_h}}, bar_across, bar_down);
}
static_assert(bars(50, 50, 100, 100) == ScrollBarsNeeded{false, false}, "it all fits");
static_assert(bars(100, 100, 100, 100) == ScrollBarsNeeded{false, false}, "exactly");
static_assert(bars(50, 101, 100, 100) == ScrollBarsNeeded{true, false});
static_assert(bars(101, 50, 100, 100) == ScrollBarsNeeded{false, true});
static_assert(bars(95, 101, 100, 100) == ScrollBarsNeeded{true, true}, "the down bar's room makes the other needed");
static_assert(bars(101, 95, 100, 100) == ScrollBarsNeeded{true, true}, "and the other way about");
static_assert(bars(0, 0, 0, 0) == ScrollBarsNeeded{false, false});
static_assert(bars(1, 1, 1, 1) == ScrollBarsNeeded{false, false});
static_assert(bars(2, 2, 1, 1) == ScrollBarsNeeded{true, true});

// --- A bar as a whole ---
constexpr Rect track_down{Point{Left{90}, Top{0}}, Size{Width{10}, Height{100}}};

[[nodiscard]] consteval ScrollBar<DownAxis> bar_of(Height content, Height view) {
    ScrollBar<DownAxis> bar;
    bar.place(track_down, content, view);
    return bar;
}

[[nodiscard]] consteval bool bar_scrolls_and_clamps() {
    ScrollBar<DownAxis> bar = bar_of(Height{1000}, Height{100});
    if (!bar.scroll_to(Height{500}) || bar.offset() != Height{500}) return false;
    if (bar.scroll_to(Height{500})) return false;
    if (!bar.scroll_to(Height{int_max}) || bar.offset() != Height{900}) return false;
    if (bar.scroll_to(Height{int_max}) || bar.scroll_by(ScrollDown::of_steps(1), Height{10})) return false;
    if (!bar.scroll_by(ScrollDown::of_steps(-1), Height{10}) || bar.offset() != Height{890}) return false;
    bar.place(track_down, Height{500}, Height{100});
    if (bar.offset() != Height{400}) return false;
    bar.place(track_down, Height{500}, Height{1000});
    return bar.offset() == Height{} && bar.thumb().empty();
}
static_assert(bar_scrolls_and_clamps());

[[nodiscard]] consteval bool bar_takes_presses() {
    ScrollBar<DownAxis> bar = bar_of(Height{1000}, Height{100});
    const Point on_thumb{Left{95}, Top{5}};
    const Point in_trough{Left{95}, Top{50}};
    if (bar.press(Point{Left{50}, Top{5}}) != ScrollBarPress::Missed) return false;
    if (bar.press(in_trough) != ScrollBarPress::Paged || bar.offset() != Height{100}) return false;
    const Rect thumb = bar.thumb();
    if (thumb.top() != Top{9} || thumb.height() != Height{16}) return false;  // 100 of 900, on 84 of room
    if (bar.press(Point{Left{95}, Top{2}}) != ScrollBarPress::Paged || bar.offset() != Height{}) return false;
    if (bar.press(Point{Left{95}, Top{50}}) != ScrollBarPress::Paged) return false;
    if (bar.press(Point{Left{95}, Top{2}}) != ScrollBarPress::Paged) return false;
    if (bar.drag_to(in_trough)) return false;  // nothing held
    if (bar.press(on_thumb) != ScrollBarPress::Grabbed || !bar.grabbed()) return false;
    if (!bar.drag_to(Point{Left{0}, Top{int_max}}) || bar.offset() != Height{900}) return false;
    if (bar.drag_to(Point{Left{0}, Top{int_max}})) return false;
    if (!bar.drag_to(Point{Left{0}, Top{int_min}}) || bar.offset() != Height{}) return false;
    bar.release();
    if (bar.grabbed() || bar.drag_to(in_trough)) return false;
    bar.scroll_to(Height{900});
    if (bar.press(Point{Left{95}, Top{5}}) != ScrollBarPress::Paged || bar.offset() != Height{800}) return false;
    // A view of nothing has no page to go by.
    ScrollBar<DownAxis> blind = bar_of(Height{1000}, Height{});
    return blind.press(in_trough) == ScrollBarPress::PagedNowhere && blind.offset() == Height{};
}
static_assert(bar_takes_presses());

[[nodiscard]] consteval bool bar_with_no_track_takes_nothing_but_still_scrolls() {
    ScrollBar<AcrossAxis> bar;
    bar.place(Rect{}, Width{100}, Width{10});
    if (bar.press(Point{}) != ScrollBarPress::Missed || !bar.thumb().empty()) return false;
    return bar.scroll_to(Width{5}) && bar.offset() == Width{5};
}
static_assert(bar_with_no_track_takes_nothing_but_still_scrolls());

// --- Lengths end to end ---
constexpr std::array<Width, 3> three_widths{Width{100}, Width{50}, Width{}};
constexpr std::span<const Width> three{three_widths};
static_assert(total_width(three) == Width{150} && total_width({}) == Width{});
constexpr std::array<Width, 2> huge_widths{Width{int_max}, Width{int_max}};
static_assert(total_width(std::span<const Width>{huge_widths}) == Width{int_max});
static_assert(span_index_at(three, Left{10}, Left{10}) == 0 && span_index_at(three, Left{10}, Left{109}) == 0);
static_assert(span_index_at(three, Left{10}, Left{110}) == 1 && span_index_at(three, Left{10}, Left{159}) == 1);
static_assert(!span_index_at(three, Left{10}, Left{9}) && !span_index_at(three, Left{10}, Left{160}) && !span_index_at({}, Left{}, Left{}));
static_assert(!span_index_at(std::span<const Width>{huge_widths}, Left{int_min}, Left{int_max}).has_value() || span_index_at(std::span<const Width>{huge_widths}, Left{int_min}, Left{int_max}) == 1);

// The first boundary at 110 and the second at 160: the empty third sits on it too, and the later wins.
static_assert(boundary_near(three, Left{10}, Left{110}, Width{4}) == 0);
static_assert(boundary_near(three, Left{10}, Left{107}, Width{4}) == 0 && boundary_near(three, Left{10}, Left{113}, Width{4}) == 0);
static_assert(!boundary_near(three, Left{10}, Left{106}, Width{4}) && !boundary_near(three, Left{10}, Left{114}, Width{4}));
static_assert(boundary_near(three, Left{10}, Left{160}, Width{4}) == 2);
static_assert(!boundary_near(three, Left{10}, Left{int_min}, Width{4}) && !boundary_near(three, Left{10}, Left{int_max}, Width{4}));
static_assert(!boundary_near({}, Left{}, Left{}, Width{4}) && !boundary_near(three, Left{10}, Left{110}, Width{}));
constexpr std::array<Width, 2> close_widths{Width{3}, Width{3}};
static_assert(boundary_near(std::span<const Width>{close_widths}, Left{}, Left{3}, Width{4}) == 0 && boundary_near(std::span<const Width>{close_widths}, Left{}, Left{5}, Width{4}) == 1, "the nearer of two");

static_assert(dragged_width(Left{100}, Width{50}, Left{120}, Width{24}, Width{4096}) == Width{70});
static_assert(dragged_width(Left{100}, Width{50}, Left{60}, Width{24}, Width{4096}) == Width{24}, "not under the least");
static_assert(dragged_width(Left{100}, Width{50}, Left{int_min}, Width{24}, Width{4096}) == Width{24});
static_assert(dragged_width(Left{100}, Width{50}, Left{int_max}, Width{24}, Width{4096}) == Width{4096}, "not over the most");
static_assert(dragged_width(Left{100}, Width{50}, Left{100}, Width{24}, Width{4096}) == Width{50}, "no move, no change");

// --- Items of one height ---
static_assert(visible_item_range(Height{}, Height{100}, Height{20}, 0) == ItemRange{0, 0}, "no items");
static_assert(visible_item_range(Height{}, Height{100}, Height{20}, 1) == ItemRange{0, 1}, "one");
static_assert(visible_item_range(Height{}, Height{100}, Height{20}, 1000) == ItemRange{0, 5});
static_assert(visible_item_range(Height{10}, Height{100}, Height{20}, 1000) == ItemRange{0, 6}, "part of one each end");
static_assert(visible_item_range(Height{20}, Height{100}, Height{20}, 1000) == ItemRange{1, 5});
static_assert(visible_item_range(Height{}, Height{100}, Height{}, 1000) == ItemRange{1000, 0}, "items of no height");
static_assert(visible_item_range(Height{}, Height{}, Height{20}, 1000) == ItemRange{1000, 0}, "no view");
static_assert(visible_item_range(Height{19'999'600}, Height{400}, Height{20}, max_grid_rows) == ItemRange{999'980, 20}, "the end of a million");
static_assert(visible_item_range(Height{int_max}, Height{400}, Height{20}, max_grid_rows) == ItemRange{max_grid_rows, 0}, "past the end");
static_assert(visible_item_range(Height{int_max}, Height{int_max}, Height{1}, size_most).count <= 1, "the greatest of everything");
static_assert(item_index_at(Height{}, Height{20}, 3) == 0 && item_index_at(Height{59}, Height{20}, 3) == 2);
static_assert(!item_index_at(Height{60}, Height{20}, 3) && !item_index_at(Height{}, Height{20}, 0) && !item_index_at(Height{5}, Height{}, 3));
static_assert(item_index_at(Height{19'999'999}, Height{20}, max_grid_rows) == 999'999);

// --- Selection steps ---
static_assert(selection_step_for_key(Key::Up) == SelectionStep::Previous && selection_step_for_key(Key::Down) == SelectionStep::Next);
static_assert(selection_step_for_key(Key::PageUp) == SelectionStep::PageBack && selection_step_for_key(Key::PageDown) == SelectionStep::PageForward);
static_assert(selection_step_for_key(Key::Home) == SelectionStep::First && selection_step_for_key(Key::End) == SelectionStep::Last);
static_assert(!selection_step_for_key(Key::Return) && !selection_step_for_key(Key::Left));

static_assert(!stepped_selection(std::nullopt, SelectionStep::Next, 0, 10) && !stepped_selection(0, SelectionStep::Next, 0, 10), "no rows");
static_assert(stepped_selection(std::nullopt, SelectionStep::Next, 5, 10) == 0 && stepped_selection(std::nullopt, SelectionStep::Previous, 5, 10) == 0);
static_assert(stepped_selection(std::nullopt, SelectionStep::Last, 5, 10) == 4);
static_assert(stepped_selection(0, SelectionStep::Next, 1, 10) == 0 && stepped_selection(0, SelectionStep::Previous, 1, 10) == 0, "one row");
static_assert(stepped_selection(2, SelectionStep::Next, 5, 10) == 3 && stepped_selection(2, SelectionStep::Previous, 5, 10) == 1);
static_assert(stepped_selection(4, SelectionStep::Next, 5, 10) == 4 && stepped_selection(0, SelectionStep::Previous, 5, 10) == 0, "the ends");
static_assert(stepped_selection(20, SelectionStep::PageForward, 100, 10) == 30 && stepped_selection(20, SelectionStep::PageBack, 100, 10) == 10);
static_assert(stepped_selection(95, SelectionStep::PageForward, 100, 10) == 99 && stepped_selection(5, SelectionStep::PageBack, 100, 10) == 0);
static_assert(stepped_selection(5, SelectionStep::PageForward, 100, 0) == 6, "a page of none counts as one");
static_assert(stepped_selection(5, SelectionStep::PageForward, 100, size_most) == 99 && stepped_selection(5, SelectionStep::PageBack, 100, size_most) == 0);
static_assert(stepped_selection(5, SelectionStep::First, 100, 10) == 0 && stepped_selection(5, SelectionStep::Last, 100, 10) == 99);
static_assert(stepped_selection(size_most, SelectionStep::Previous, 100, 10) == 98, "a position past the end counts as the last");
static_assert(stepped_selection(999'999, SelectionStep::Next, max_grid_rows, 20) == 999'999);

// --- Sorting through indices ---
[[nodiscard]] consteval std::vector<std::uint32_t> sorted_by_value(std::vector<int> values) {
    std::vector<std::uint32_t> order = identity_order(static_cast<std::uint32_t>(values.size()));
    stable_sort_indices(order, [&](std::uint32_t a, std::uint32_t b) { return values[a] < values[b]; });
    return order;
}
static_assert(identity_order(0).empty() && identity_order(3) == std::vector<std::uint32_t>{0, 1, 2});
static_assert(sorted_by_value({}).empty() && sorted_by_value({7}) == std::vector<std::uint32_t>{0});
static_assert(sorted_by_value({3, 1, 2}) == std::vector<std::uint32_t>{1, 2, 0});
static_assert(sorted_by_value({5, 4, 3, 2, 1}) == std::vector<std::uint32_t>{4, 3, 2, 1, 0}, "an odd count, reversed");
static_assert(sorted_by_value({2, 1, 2, 1, 2, 1}) == std::vector<std::uint32_t>{1, 3, 5, 0, 2, 4}, "stable");
static_assert(sorted_by_value({1, 1, 1, 1, 1, 1, 1}) == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6}, "all equal stay as they were");
[[nodiscard]] consteval bool long_sort_is_sorted_and_stable() {
    std::vector<int> values;
    for (int i = 0; i < 257; ++i) values.push_back((i * 7919) % 13);
    const std::vector<std::uint32_t> order = sorted_by_value(values);
    if (order.size() != values.size()) return false;
    for (std::size_t i = 1; i < order.size(); ++i) {
        if (values[order[i - 1]] > values[order[i]]) return false;
        if (values[order[i - 1]] == values[order[i]] && order[i - 1] >= order[i]) return false;
    }
    return true;
}
static_assert(long_sort_is_sorted_and_stable());
[[nodiscard]] consteval bool position_is_found() {
    const std::vector<std::uint32_t> order{2, 0, 1};
    return position_of_index(order, 2) == 0 && position_of_index(order, 1) == 2 && !position_of_index(order, 3) && !position_of_index({}, 0);
}
static_assert(position_is_found());

// --- Text ---
static_assert(decimal_text(0, false) == "0" && decimal_text(10, false) == "10" && decimal_text(5, true) == "-5");
static_assert(decimal_text(std::numeric_limits<std::uint64_t>::max(), false) == "18446744073709551615");
static_assert(CellFormat<int>::text(int_min) == "-2147483648" && CellFormat<int>::text(int_max) == "2147483647" && CellFormat<int>::text(0) == "0");
static_assert(CellFormat<std::int64_t>::text(std::numeric_limits<std::int64_t>::min()) == "-9223372036854775808");
static_assert(CellFormat<std::uint64_t>::text(std::numeric_limits<std::uint64_t>::max()) == "18446744073709551615");
static_assert(CellFormat<std::uint8_t>::text(255) == "255" && CellFormat<std::int8_t>::text(-128) == "-128", "a small number is a number, and not a character");
static_assert(CellFormat<bool>::text(true) == "true" && CellFormat<bool>::text(false) == "false");
static_assert(CellFormat<std::string>::text(std::string{"x y"}) == "x y" && CellFormat<std::string>::text(std::string{}).empty());
static_assert(CellFormat<Left>::text(Left{-3}) == "-3" && CellFormat<Right>::text(Right{int_max}) == "2147483647");
static_assert(CellFormat<Top>::text(Top{int_min}) == "-2147483648" && CellFormat<Bottom>::text(Bottom{}) == "0");
static_assert(CellFormat<Width>::text(Width{7}) == "7" && CellFormat<Height>::text(Height{-7}) == "0", "a length is never negative");
static_assert(HasCellFormat<int> && HasCellFormat<bool> && HasCellFormat<std::string> && HasCellFormat<Width> && HasCellFormat<Left>);
static_assert(!HasCellFormat<double> && !HasCellFormat<char> && !HasCellFormat<Size> && !HasCellFormat<char8_t>, "no format of its own");

static_assert(utf8_prefix("abc", 10) == "abc" && utf8_prefix("abc", 3) == "abc" && utf8_prefix("abc", 2) == "ab" && utf8_prefix("abc", 0).empty());
static_assert(utf8_prefix("a\xC3\xA9z", 2) == "a", "a two byte character is not split");
static_assert(utf8_prefix("a\xC3\xA9z", 3) == "a\xC3\xA9");
static_assert(utf8_prefix("\xE2\x82\xACz", 2).empty() && utf8_prefix("\xF0\x9F\x98\x80z", 3).empty(), "nor a longer one");
static_assert(utf8_prefix("\x80\x80\x80\x80\x80z", 4).size() == 1, "only three steps back through what is not valid anyway");
static_assert(utf8_prefix("", 0).empty());

}  // namespace

int main() {
    std::cout << "the helpers' static_asserts held\n";
    return 0;
}
