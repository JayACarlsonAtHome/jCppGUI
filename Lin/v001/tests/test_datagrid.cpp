// File: tests/test_datagrid.cpp
// Purpose: DataGrid. In static_assert, the whole grid from injected events to the state and the pixels, with a shaper that
//          needs no font. At run time, with the real font: the limit on rows and what the picture looks like.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"

struct GridTag;
struct EchoTag;

// What the grid told it.
using Picked = jgui::Message<"picked", jgui::To<EchoTag>, jgui::GridRow>;

// Outside the anonymous namespace because only the Widget concept reads bounds, and a compiler rightly reports a member
// nobody calls on a type with internal linkage.
struct Echo {
    using tag = EchoTag;
    int told{};
    std::optional<jgui::GridRow> last{};
    constexpr void on(Picked, const jgui::GridRow& row) {
        ++told;
        last = row;
    }
    [[nodiscard]] constexpr jgui::Rect bounds() const noexcept { return jgui::Rect{}; }
    constexpr void paint(const jgui::TreeKey&, jgui::Canvas&) {}
    template <class Shaper>
    constexpr std::expected<jgui::Rect, jgui::Error> layout(const jgui::TreeKey&, jgui::Rect area, const Shaper&) {
        return area;
    }
    template <class U, class E>
    constexpr bool handle(const jgui::TreeKey&, U&, const E&) {
        return false;
    }
    template <class U, class E>
    constexpr void notify(const jgui::TreeKey&, U&, const E&) {}
};

namespace {

using namespace jgui;
using jgui_test::Checker;
using jgui_test::number;

constexpr std::int32_t int_max = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t int_min = std::numeric_limits<std::int32_t>::min();

struct SmallRow {
    Left left{};
    std::string name{};
    bool on{};
};

// Two pixels a byte, four rows high, solid, so a row is 8 high with its padding.
struct SolidShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text == "refused") return make_error(ErrorCode::GlyphTooLarge, "this shaper refuses that");
        if (text == "broken") return make_error(ErrorCode::Font, "the font went away");
        const Size size{Width{saturate_to<std::int32_t>(text.size() * 2)}, Height{4}};
        return TextRun{size, std::vector<std::uint8_t>(text.size() * 2 * 4, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};

constexpr std::int32_t small_row_height = 8;
// A letter is 2 across, and so a button is 6, and the two of them with their gap are 13.
constexpr Width small_button{6};
// Padding, one letter, both buttons and the boundary's reach inside the column.
constexpr Width small_drag_floor{4 + 2 + 13 + 3};

using SmallGrid = DataGrid<GridTag, SmallRow, Picked, Column<"L", &SmallRow::left, 50>, Column<"Name", &SmallRow::name, 50>, Column<"On", &SmallRow::on, 50>>;
static_assert(Widget<SmallGrid>);

constexpr Size small_window{Width{210}, Height{60}};

[[nodiscard]] constexpr std::vector<SmallRow> small_rows(std::int32_t count) {
    std::vector<SmallRow> rows;
    for (std::int32_t i = 0; i < count; ++i) rows.push_back(SmallRow{Left{i}, std::string{"n"}, i % 2 == 0});
    return rows;
}

// A tree with one grid and its echo, a shaper, and short ways to send it things. Each test is a function that runs in constant evaluation.
template <class Shaper = SolidShaper>
struct Rig {
    constexpr explicit Rig(std::int32_t rows, Size size = small_window, Shaper with = Shaper{}) : shaper{with}, core{HeadlessBackend{size}, window(Echo{}, SmallGrid{})} {
        jgui::require(grid().set_rows(small_rows(rows)).has_value(), "rows refused");
        settle();
    }
    [[nodiscard]] constexpr SmallGrid& grid() { return core.ui().template get<GridTag>(); }
    [[nodiscard]] constexpr Echo& echo() { return core.ui().template get<EchoTag>(); }
    [[nodiscard]] constexpr HeadlessBackend& backend() { return jgui_test::backend(core); }
    constexpr void settle() { jgui::require(core.process_pending(shaper).has_value(), "a frame failed"); }
    template <class E>
    constexpr void deliver(const E& event) {
        backend().inject(event);
        settle();
    }
    constexpr void move(std::int32_t x, std::int32_t y) { deliver(PointerMove{at(x, y)}); }
    constexpr void press(std::int32_t x, std::int32_t y) { deliver(PointerButton{at(x, y), MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)}); }
    constexpr void release(std::int32_t x, std::int32_t y) { deliver(PointerButton{at(x, y), MouseButton::Left, ButtonState::Released, jgui_test::serial(1)}); }
    constexpr void click(std::int32_t x, std::int32_t y) {
        press(x, y);
        release(x, y);
    }
    constexpr void key(Keysym which) { deliver(KeyEvent{which, ModifierMask{}, true}); }
    constexpr void ctrl(Keysym which) { deliver(KeyEvent{which, Modifier::Ctrl, true}); }
    [[nodiscard]] constexpr bool sorted_by(std::initializer_list<GridSortKey> wanted) { return std::ranges::equal(grid().sort_keys(), wanted); }
    constexpr void wheel(std::int32_t steps) { deliver(PointerScroll{at(10, 20), ScrollDown::of_steps(steps), ScrollAcross{}}); }
    [[nodiscard]] static constexpr Point at(std::int32_t x, std::int32_t y) { return Point{Left{x}, Top{y}}; }
    [[nodiscard]] constexpr std::uint32_t pixel(std::int32_t x, std::int32_t y) { return backend().pixel(at(x, y)); }
    [[nodiscard]] constexpr std::uint64_t frames() const { return core.frames(); }
    // Time goes by, and the loop gets its turn, with no event.
    constexpr void wait_ms(std::int64_t ms) {
        backend().advance_time(std::chrono::milliseconds{ms});
        settle();
    }
    constexpr void press_on(const Rect& button) { click(number(button.left()) + 1, number(button.top()) + 1); }
    // How many pixels in the rectangle are text's colour.
    [[nodiscard]] constexpr int text_pixels(const Rect& area) {
        int count = 0;
        for (std::int32_t y = number(area.top()); y < number(area.bottom()); ++y)
            for (std::int32_t x = number(area.left()); x < number(area.right()); ++x)
                if (pixel(x, y) == theme::text.argb) ++count;
        return count;
    }

    Shaper shaper;
    ApplicationCore<HeadlessBackend, Window<Echo, SmallGrid>> core;
};

// The border: everything in the grid starts this far in.
constexpr std::int32_t side = 4;
constexpr std::int32_t top_edge = 4;

// The grid's body starts one row below the border. Row r, if not scrolled, is at this y.
[[nodiscard]] constexpr std::int32_t row_y(std::int32_t row) { return top_edge + small_row_height * (row + 1) + 1; }

// --- Selection ---
[[nodiscard]] constexpr bool click_selects_and_tells() {
    Rig<> rig{20};
    if (rig.grid().selected()) return false;
    const std::uint64_t before = rig.frames();
    rig.click(10, row_y(2));
    if (rig.grid().selected() != GridRow{2} || rig.echo().told != 1 || rig.echo().last != GridRow{2} || !rig.grid().focused()) return false;
    if (rig.frames() == before) return false;
    // The same row again changes nothing, tells nobody and draws nothing.
    const std::uint64_t settled = rig.frames();
    rig.click(10, row_y(2));
    if (rig.echo().told != 1 || rig.frames() != settled) return false;
    rig.click(10, row_y(3));
    return rig.echo().told == 2 && rig.echo().last == GridRow{3};
}
static_assert(click_selects_and_tells());

[[nodiscard]] constexpr bool keys_move_the_selection_and_keep_it_showing() {
    Rig<> rig{20};
    rig.key(Key::Down);
    if (rig.grid().selected()) return false;  // no focus yet
    rig.click(10, row_y(0));
    rig.key(Key::Down);
    if (rig.grid().selected() != GridRow{1} || rig.grid().scroll_down() != Height{}) return false;
    rig.key(Key::Up);
    rig.key(Key::Up);
    if (rig.grid().selected() != GridRow{0} || rig.echo().told != 3) return false;  // the second Up changed nothing
    rig.key(Key::End);
    if (rig.grid().selected() != GridRow{19}) return false;
    // 20 rows of 8 in a body of 44: the last row's bottom is at the view's.
    if (rig.grid().scroll_down() != Height{116}) return false;
    rig.key(Key::PageUp);  // five whole rows in view
    if (rig.grid().selected() != GridRow{14} || rig.grid().scroll_down() != Height{112}) return false;
    rig.key(Key::Home);
    if (rig.grid().selected() != GridRow{0} || rig.grid().scroll_down() != Height{}) return false;
    rig.key(Key::PageDown);
    if (rig.grid().selected() != GridRow{5}) return false;
    const std::uint64_t settled = rig.frames();
    rig.deliver(KeyEvent{Key::Down, ModifierMask{}, false});
    return rig.grid().selected() == GridRow{5} && rig.frames() == settled;  // a release does nothing
}
static_assert(keys_move_the_selection_and_keep_it_showing());

[[nodiscard]] constexpr bool keys_with_nothing_to_select() {
    Rig<> rig{0};
    rig.click(10, row_y(0));
    if (rig.grid().selected() || !rig.grid().focused()) return false;
    const std::uint64_t settled = rig.frames();
    rig.key(Key::Down);
    rig.key(Key::End);
    return !rig.grid().selected() && rig.frames() == settled && rig.echo().told == 0;
}
static_assert(keys_with_nothing_to_select());

[[nodiscard]] constexpr bool focus_goes_when_the_frame_says() {
    Rig<> rig{20};
    rig.click(10, row_y(0));
    jgui_test::notify(rig.core.ui(), ClearFocus{});
    rig.settle();
    rig.key(Key::Down);
    return rig.grid().selected() == GridRow{0} && !rig.grid().focused();
}
static_assert(focus_goes_when_the_frame_says());

// --- Scrolling ---
[[nodiscard]] constexpr bool wheel_scrolls_and_clamps_with_no_extra_frame() {
    Rig<> rig{20};
    const std::uint64_t before = rig.frames();
    rig.wheel(-1);
    if (rig.frames() != before) return false;  // already at the start
    rig.wheel(1);
    if (rig.grid().scroll_down() != Height{24} || rig.frames() != before + 1) return false;  // three rows
    rig.wheel(int_max);
    if (rig.grid().scroll_down() != Height{116} || rig.frames() != before + 2) return false;
    rig.wheel(int_max);
    if (rig.frames() != before + 2) return false;  // the same offset: nothing to draw
    rig.deliver(PointerScroll{Rig<>::at(10, 20), ScrollDown::of_pixels(-5), ScrollAcross{}});
    if (rig.grid().scroll_down() != Height{111}) return false;
    rig.deliver(PointerScroll{Rig<>::at(10, 20), ScrollDown::of_pixels(int_min), ScrollAcross{}});
    if (rig.grid().scroll_down() != Height{}) return false;
    rig.deliver(PointerScroll{Rig<>::at(10, 20), ScrollDown{}, ScrollAcross{}});
    return rig.frames() == before + 4;
}
static_assert(wheel_scrolls_and_clamps_with_no_extra_frame());

[[nodiscard]] constexpr bool scrolling_outside_goes_nowhere() {
    Rig<> rig{20};
    rig.deliver(PointerScroll{Rig<>::at(500, 500), ScrollDown::of_steps(1), ScrollAcross{}});
    return rig.grid().scroll_down() == Height{};
}
static_assert(scrolling_outside_goes_nowhere());

[[nodiscard]] constexpr bool a_bar_shows_only_when_needed() {
    Rig<> few{2};
    if (!few.grid().thumb_down().empty() || !few.grid().thumb_across().empty()) return false;
    if (few.grid().body_area().width() != Width{202}) return false;
    Rig<> many{20};
    if (many.grid().thumb_down().empty() || !many.grid().thumb_across().empty()) return false;
    if (many.grid().body_area().width() != Width{190}) return false;
    Rig<> narrow{2, Size{Width{60}, Height{60}}};
    if (!narrow.grid().thumb_down().empty() || narrow.grid().thumb_across().empty()) return false;
    Rig<> both{20, Size{Width{60}, Height{60}}};
    return !both.grid().thumb_down().empty() && !both.grid().thumb_across().empty();
}
static_assert(a_bar_shows_only_when_needed());

[[nodiscard]] constexpr bool thumb_drag_and_trough_paging() {
    Rig<> rig{20};
    const Rect thumb = rig.grid().thumb_down();
    const std::int32_t x = number(thumb.left()) + 2;
    const std::int32_t y = number(thumb.top()) + 2;
    rig.press(x, y);
    if (!jgui_test::capture_active(rig.core.ui())) return false;
    rig.move(0, 59);  // the pointer may wander anywhere while it holds on
    const Height dragged = rig.grid().scroll_down();
    if (dragged == Height{}) return false;
    rig.wheel(1);
    if (rig.grid().scroll_down() != dragged) return false;  // a held thumb says where the offset is
    rig.move(x, int_max);
    if (rig.grid().scroll_down() != Height{116}) return false;
    rig.move(x, int_min);
    if (rig.grid().scroll_down() != Height{}) return false;
    rig.release(x, y);
    if (jgui_test::capture_active(rig.core.ui())) return false;
    // In the trough below the thumb, then above it: a view each way.
    rig.click(x, 55);
    if (rig.grid().scroll_down() != Height{44}) return false;
    const Rect moved = rig.grid().thumb_down();
    if (moved.top() <= thumb.top()) return false;
    rig.click(x, number(thumb.top()));
    if (rig.grid().scroll_down() != Height{}) return false;
    const std::uint64_t settled = rig.frames();
    rig.click(x, number(thumb.top()));  // on the thumb now: nothing moved
    return rig.frames() == settled && !jgui_test::capture_active(rig.core.ui());
}
static_assert(thumb_drag_and_trough_paging());

[[nodiscard]] constexpr bool offsets_clamp_when_rows_or_size_change() {
    Rig<> rig{20};
    rig.wheel(int_max);
    if (rig.grid().scroll_down() != Height{116}) return false;
    if (!rig.grid().set_rows(small_rows(10))) return false;
    rig.settle();
    if (rig.grid().scroll_down() != Height{36}) return false;  // 80 - 44
    rig.deliver(ResizeEvent{Size{Width{210}, Height{200}}});
    if (rig.grid().scroll_down() != Height{} || !rig.grid().thumb_down().empty()) return false;
    if (!rig.grid().set_rows(small_rows(0))) return false;
    rig.settle();
    return rig.grid().scroll_down() == Height{};
}
static_assert(offsets_clamp_when_rows_or_size_change());

[[nodiscard]] constexpr bool the_smallest_windows_hold() {
    Rig<> one{20, Size{Width{1}, Height{1}}};
    one.wheel(1);
    one.click(0, 0);
    one.key(Key::End);
    one.move(0, 0);
    Rig<> flat{20, Size{Width{120}, Height{1}}};
    flat.click(100, 0);
    flat.key(Key::PageDown);
    Rig<> empty{0, Size{Width{1}, Height{1}}};
    empty.wheel(1);
    empty.click(0, 0);
    return empty.grid().scroll_down() == Height{} && !empty.grid().selected() && one.grid().selected() == GridRow{19};
}
static_assert(the_smallest_windows_hold());

// --- Columns ---
// A click makes a column current and sorts nothing, and only the keys sort.
[[nodiscard]] constexpr bool the_keys_sort_the_current_column() {
    Rig<> rig{0};
    // Ties on purpose: two Bs, with their rows' own order to keep.
    if (!rig.grid().set_rows(std::vector<SmallRow>{SmallRow{Left{0}, "B", true}, SmallRow{Left{1}, "A", true}, SmallRow{Left{2}, "B", false}, SmallRow{Left{3}, "C", false}})) return false;
    rig.settle();
    rig.click(10, row_y(0));  // row 0, a "B"
    rig.ctrl(Key::Up);
    if (!rig.grid().sort_keys().empty() || rig.grid().selected() != GridRow{0}) return false;  // no current column: nothing, and the selection does not move either
    rig.click(70, 4);  // the second column's header
    if (rig.grid().current_column() != 1 || !rig.grid().focused() || !rig.grid().sort_keys().empty()) return false;
    const auto order = [&](std::array<std::uint32_t, 4> wanted) {
        const auto shown = rig.grid().display_order();
        for (std::size_t i = 0; i < wanted.size(); ++i)
            if (shown[i] != wanted[i]) return false;
        return true;
    };
    if (!order({0, 1, 2, 3})) return false;
    rig.ctrl(Key::Up);
    if (!rig.sorted_by({GridSortKey{1, false}}) || !order({1, 0, 2, 3})) return false;
    if (rig.grid().selected() != GridRow{0} || rig.echo().told != 1) return false;  // the same row, and nothing to tell
    if (rig.grid().rows()[0].name != "B") return false;  // the rows themselves have not moved
    rig.key(Key::Down);
    if (rig.grid().selected() != GridRow{2} || !rig.sorted_by({GridSortKey{1, false}})) return false;  // the next one as shown, and a plain key is no sort
    rig.ctrl(Key::Down);
    if (!rig.sorted_by({GridSortKey{1, true}}) || !order({3, 0, 2, 1})) return false;  // ties in their own order both ways
    if (rig.grid().selected() != GridRow{2}) return false;
    // A column's header makes it current, and clears nothing.
    rig.click(10, 4);
    if (rig.grid().current_column() != 0 || !rig.sorted_by({GridSortKey{1, true}})) return false;
    rig.click(10, 4);
    return rig.grid().current_column() == 0 && order({3, 0, 2, 1});
}
static_assert(the_keys_sort_the_current_column());

// Rows with a tie at every level: each key breaks the last's ties, each in its own direction, and then their own order does.
[[nodiscard]] constexpr std::vector<SmallRow> rows_with_ties() {
    return std::vector<SmallRow>{SmallRow{Left{1}, "B", true}, SmallRow{Left{2}, "A", true}, SmallRow{Left{1}, "A", true}, SmallRow{Left{2}, "B", false}, SmallRow{Left{1}, "A", false}, SmallRow{Left{1}, "A", true}};
}

[[nodiscard]] constexpr bool keys_break_each_others_ties() {
    Rig<> rig{0};
    if (!rig.grid().set_rows(rows_with_ties())) return false;
    rig.settle();
    const auto order = [&](std::array<std::uint32_t, 6> wanted) {
        const auto shown = rig.grid().display_order();
        for (std::size_t i = 0; i < wanted.size(); ++i)
            if (shown[i] != wanted[i]) return false;
        return true;
    };
    rig.click(10, 4);   // Left
    rig.ctrl(Key::Up);
    if (!order({0, 2, 4, 5, 1, 3})) return false;
    rig.click(70, 4);   // Name, going down
    rig.ctrl(Key::Down);
    if (!rig.sorted_by({GridSortKey{0, false}, GridSortKey{1, true}}) || !order({0, 2, 4, 5, 3, 1})) return false;
    rig.click(120, 4);  // On, going up: false first
    rig.ctrl(Key::Up);
    if (!rig.sorted_by({GridSortKey{0, false}, GridSortKey{1, true}, GridSortKey{2, false}}) || !order({0, 4, 2, 5, 3, 1})) return false;
    // The first key turned: it keeps its number, the rest of the sort stands, and the rows' own order still settles the last tie.
    rig.click(10, 4);
    rig.ctrl(Key::Down);
    return rig.sorted_by({GridSortKey{0, true}, GridSortKey{1, true}, GridSortKey{2, false}}) && order({3, 1, 0, 4, 2, 5});
}
static_assert(keys_break_each_others_ties());

[[nodiscard]] constexpr bool asking_again_for_what_is_so_draws_nothing() {
    Rig<> rig{4};
    rig.click(10, 4);
    rig.ctrl(Key::Up);
    const std::uint64_t frames = rig.frames();
    rig.ctrl(Key::Up);
    if (rig.frames() != frames || !rig.sorted_by({GridSortKey{0, false}})) return false;
    rig.ctrl(Key::Down);
    const std::uint64_t turned = rig.frames();
    if (turned != frames + 1) return false;
    rig.ctrl(Key::Down);
    return rig.frames() == turned && rig.sorted_by({GridSortKey{0, true}});
}
static_assert(asking_again_for_what_is_so_draws_nothing());

[[nodiscard]] constexpr bool ctrl_left_and_right_clear_the_level() {
    Rig<> rig{0};
    if (!rig.grid().set_rows(rows_with_ties())) return false;
    rig.settle();
    rig.click(10, row_y(2));
    const auto add = [&](std::int32_t x, Keysym way) {
        rig.click(x, 4);
        rig.ctrl(way);
    };
    // Nothing to clear: no frame, and no scrolling, in a window narrow enough to scroll.
    rig.deliver(ResizeEvent{Size{Width{100}, Height{60}}});
    rig.click(10, 4);
    const std::uint64_t frames = rig.frames();
    rig.ctrl(Key::Left);
    rig.ctrl(Key::Right);
    if (rig.frames() != frames || rig.grid().scroll_across() != Width{}) return false;
    rig.deliver(ResizeEvent{small_window});
    add(10, Key::Up);
    add(70, Key::Down);
    add(120, Key::Up);
    if (!rig.sorted_by({GridSortKey{0, false}, GridSortKey{1, true}, GridSortKey{2, false}})) return false;
    // The middle one goes, and the last moves up to its number.
    rig.click(70, 4);
    rig.ctrl(Key::Left);
    if (!rig.sorted_by({GridSortKey{0, false}, GridSortKey{2, false}})) return false;
    const std::uint64_t cleared = rig.frames();
    rig.ctrl(Key::Right);  // no longer in the sort
    if (rig.frames() != cleared) return false;
    rig.click(10, 4);
    rig.ctrl(Key::Right);
    rig.click(120, 4);
    rig.ctrl(Key::Left);
    if (!rig.grid().sort_keys().empty()) return false;
    // Emptied, it is the rows' own order, with the selection on its row.
    const auto shown = rig.grid().display_order();
    for (std::size_t i = 0; i < shown.size(); ++i)
        if (shown[i] != i) return false;
    return rig.grid().selected() == GridRow{2};
}
static_assert(ctrl_left_and_right_clear_the_level());

// Ten columns, and a tenth key does nothing.
struct TenRow {
    int a{}, b{}, c{}, d{}, e{}, f{}, g{}, h{}, i{}, j{};
};
struct TenTag;
using TenGrid = DataGrid<TenTag, TenRow, NoGridMessage, Column<"a", &TenRow::a, 60>, Column<"b", &TenRow::b, 60>, Column<"c", &TenRow::c, 60>, Column<"d", &TenRow::d, 60>, Column<"e", &TenRow::e, 60>, Column<"f", &TenRow::f, 60>, Column<"g", &TenRow::g, 60>, Column<"h", &TenRow::h, 60>, Column<"i", &TenRow::i, 60>,
                     Column<"j", &TenRow::j, 60>>;

[[nodiscard]] constexpr bool a_tenth_key_does_nothing() {
    ApplicationCore core{HeadlessBackend{Size{Width{620}, Height{40}}}, window(TenGrid{})};
    if (!core.process_pending(SolidShaper{})) return false;
    TenGrid& grid = core.ui().get<TenTag>();
    HeadlessBackend& backend = jgui_test::backend(core);
    const auto choose_and_sort = [&](std::int32_t column) {
        const Point on_header{Left{side + 60 * column + 10}, Top{top_edge}};
        backend.inject(PointerButton{on_header, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
        backend.inject(PointerButton{on_header, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
        backend.inject(KeyEvent{Key::Up, Modifier::Ctrl, true});
        jgui::require(core.process_pending(SolidShaper{}).has_value(), "a frame failed");
    };
    for (std::int32_t column = 0; column < 9; ++column) choose_and_sort(column);
    if (grid.sort_keys().size() != 9) return false;
    choose_and_sort(9);
    if (grid.sort_keys().size() != 9 || grid.sort_keys().back().column != 8 || grid.current_column() != 9) return false;
    // A key already in can still turn.
    backend.inject(PointerButton{Point{Left{side + 10}, Top{top_edge}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    backend.inject(PointerButton{Point{Left{side + 10}, Top{top_edge}}, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
    backend.inject(KeyEvent{Key::Down, Modifier::Ctrl, true});
    return core.process_pending(SolidShaper{}).has_value() && grid.sort_keys().front() == GridSortKey{0, true} && grid.sort_keys().size() == 9;
}
static_assert(a_tenth_key_does_nothing());

[[nodiscard]] constexpr bool the_program_can_set_the_sort() {
    Rig<> rig{0};
    if (!rig.grid().set_rows(rows_with_ties())) return false;
    rig.settle();
    rig.click(10, row_y(3));
    const std::array keys{GridSortKey{1, true}, GridSortKey{0, false}};
    if (!rig.grid().set_sort_keys(keys)) return false;
    if (!rig.sorted_by({GridSortKey{1, true}, GridSortKey{0, false}}) || rig.grid().selected() != GridRow{3} || rig.grid().display_order()[0] != 0) return false;
    const auto refused = [](const std::expected<void, Error>& answer) { return !answer && answer.error().code == ErrorCode::SizeLimit; };
    const std::array bad_column{GridSortKey{3, false}};
    const std::array twice{GridSortKey{1, false}, GridSortKey{1, true}};
    const std::array<GridSortKey, 10> too_many{};
    if (!refused(rig.grid().set_sort_keys(bad_column)) || !refused(rig.grid().set_sort_keys(twice)) || !refused(rig.grid().set_sort_keys(too_many))) return false;
    if (!rig.sorted_by({GridSortKey{1, true}, GridSortKey{0, false}})) return false;
    if (!rig.grid().set_sort_keys({}) || !rig.grid().sort_keys().empty() || rig.grid().display_order()[0] != 0 || rig.grid().display_order()[1] != 1) return false;
    // New rows clear the sort, and keep the current column.
    rig.click(70, 4);
    rig.ctrl(Key::Up);
    if (!rig.grid().set_rows(small_rows(3))) return false;
    rig.settle();
    return rig.grid().sort_keys().empty() && rig.grid().current_column() == 1;
}
static_assert(the_program_can_set_the_sort());

// Other keys with Ctrl go as they always did, and Ctrl on a release or in a grid with no focus does nothing.
[[nodiscard]] constexpr bool only_these_keys_are_the_sorts() {
    Rig<> rig{20};
    rig.click(10, 4);
    rig.click(10, row_y(0));
    rig.deliver(KeyEvent{Key::End, Modifier::Ctrl, true});
    if (rig.grid().selected() != GridRow{19}) return false;
    rig.deliver(KeyEvent{Key::Up, Modifier::Ctrl, false});
    if (!rig.grid().sort_keys().empty()) return false;
    jgui_test::notify(rig.core.ui(), ClearFocus{});
    rig.ctrl(Key::Up);
    return rig.grid().sort_keys().empty();
}
static_assert(only_these_keys_are_the_sorts());

// A shaper tall enough that the mark has room for its digit: a header of 16, so a mark of 14 rows.
struct TallShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        return TextRun{Size{Width{saturate_to<std::int32_t>(text.size() * 2)}, Height{12}}, std::vector<std::uint8_t>(text.size() * 2 * 12, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{12}; }
};
static_assert(detail::sort_mark_rows(Height{16}) == 14 && detail::sort_mark_width(Height{16}) == Width{13} && detail::sort_mark_room(Height{16}) == Width{16});
static_assert(detail::sort_mark_rows(Height{8}) == 6 && detail::sort_mark_width(Height{8}) == Width{5});
static_assert(detail::sort_mark_rows(Height{2}) == 0 && detail::sort_mark_width(Height{2}) == Width{} && detail::sort_mark_rows(Height{}) == 0);
static_assert(detail::sort_mark_rows(Height{3}) == 1 && detail::sort_mark_width(Height{3}) == Width{1});

[[nodiscard]] constexpr bool a_mark_holds_its_number_and_points_its_way() {
    Rig<TallShaper> rig{2, Size{Width{300}, Height{60}}};
    // Column 0: from 4 to 54, the buttons from 38, and the mark's 13 across start 16 left of those, so at 22, and go down from the header's top + 1.
    constexpr std::int32_t mark_left = 22;
    constexpr std::int32_t mark_top = top_edge + 1;
    constexpr std::int32_t second_left = 72;
    const auto digit_matches = [&](std::int32_t left_of_mark, std::int32_t digit, bool descending) {
        constexpr std::int32_t rows = 14;
        const std::int32_t top = mark_top + (descending ? 1 : rows - detail::sort_digit_rows - 1);
        const std::int32_t left = left_of_mark + 6 - 2;
        const auto& wanted = detail::sort_digits.at(static_cast<std::size_t>(digit - 1));
        for (std::int32_t row = 0; row < detail::sort_digit_rows; ++row)
            for (std::int32_t column = 0; column < detail::sort_digit_columns; ++column) {
                const bool on = ((wanted.at(static_cast<std::size_t>(row)) >> (4 - column)) & 1) != 0;
                if ((rig.pixel(left + column, top + row) == theme::sort_digit.argb) != on) return false;
                if (!on && rig.pixel(left + column, top + row) != theme::sort_mark.argb) return false;
            }
        return true;
    };
    rig.click(10, 4);
    // The tint, and only on that column.
    if (rig.pixel(30, top_edge + 1) != theme::grid_current_column.argb || rig.pixel(60, top_edge + 1) != theme::bar.argb) return false;
    if (rig.pixel(mark_left + 6, mark_top) == theme::sort_mark.argb) return false;  // no sort yet, no mark
    rig.ctrl(Key::Up);
    // Going up: one pixel at the top, the whole width at the bottom, and a 1 at that fat end.
    if (rig.pixel(mark_left + 6, mark_top) != theme::sort_mark.argb || rig.pixel(mark_left + 5, mark_top) == theme::sort_mark.argb) return false;
    if (rig.pixel(mark_left, mark_top + 13) != theme::sort_mark.argb || rig.pixel(mark_left + 12, mark_top + 13) != theme::sort_mark.argb) return false;
    if (!digit_matches(mark_left, 1, false)) return false;
    // A second column is 2.
    rig.click(60, 4);
    rig.ctrl(Key::Up);
    if (!digit_matches(second_left, 2, false)) return false;
    if (!digit_matches(mark_left, 1, false)) return false;
    // Turned, it points down and keeps its number, at the other end.
    rig.ctrl(Key::Down);
    if (rig.pixel(second_left, mark_top) != theme::sort_mark.argb || rig.pixel(second_left + 12, mark_top) != theme::sort_mark.argb || rig.pixel(second_left + 6, mark_top + 13) != theme::sort_mark.argb || rig.pixel(second_left + 5, mark_top + 13) == theme::sort_mark.argb) return false;
    if (!digit_matches(second_left, 2, true)) return false;
    // The first going leaves the second to be 1.
    rig.click(10, 4);
    rig.ctrl(Key::Left);
    return digit_matches(second_left, 1, true) && !(rig.pixel(mark_left + 6, mark_top) == theme::sort_mark.argb);
}
static_assert(a_mark_holds_its_number_and_points_its_way());

[[nodiscard]] constexpr bool a_header_press_ended_elsewhere_makes_nothing_current() {
    Rig<> rig{4};
    rig.press(10, 4);
    if (!jgui_test::capture_active(rig.core.ui())) return false;
    rig.release(70, 4);
    if (rig.grid().current_column()) return false;
    rig.press(10, 4);
    rig.release(10, 30);
    if (rig.grid().current_column()) return false;
    // Above a scroll bar is no column.
    Rig<> barred{20};
    barred.click(200, 4);
    return !barred.grid().current_column();
}
static_assert(a_header_press_ended_elsewhere_makes_nothing_current());

[[nodiscard]] constexpr bool a_boundary_drags_to_resize_within_limits() {
    Rig<> rig{4};
    // The first column's boundary is at x = 54.
    rig.press(54, 4);
    if (!jgui_test::capture_active(rig.core.ui())) return false;
    rig.move(74, 40);
    if (rig.grid().column_width(0) != Width{70}) return false;
    const std::uint64_t settled = rig.frames();
    rig.move(74, 50);
    if (rig.frames() != settled) return false;  // no change of width, nothing to draw
    rig.move(int_min, 4);
    if (rig.grid().column_width(0) != small_drag_floor) return false;
    rig.move(int_max, 4);
    if (rig.grid().column_width(0) != grid_column_most) return false;
    if (rig.grid().thumb_across().empty()) return false;  // a very wide column brings a bar
    rig.move(44, 4);
    rig.release(44, 4);
    if (rig.grid().column_width(0) != Width{40} || rig.grid().current_column()) return false;  // a drag does not also choose the column
    return !jgui_test::capture_active(rig.core.ui());
}
static_assert(a_boundary_drags_to_resize_within_limits());

// The columns reach the grid's edge: the last one takes a little room, a lot becomes a filler, and the last column never gets narrower than it was set.
[[nodiscard]] constexpr bool the_room_left_is_taken_up() {
    Rig<> rig{2};  // 50 + 50 + 50 in a body of 202 leaves 52, more than the last column: a filler
    if (rig.grid().column_width(2) != Width{50} || rig.grid().column_width(0) != Width{50}) return false;
    const std::int32_t filler_x = side + 150;
    if (rig.pixel(filler_x - 1, 20) != theme::bar_edge.argb || !rig.grid().thumb_across().empty()) return false;
    // It goes over the row lines to the bottom of the body, under a header like the others: the bar's colour, a line below and a line at its end.
    for (const std::int32_t y : {row_y(0), row_y(0) + small_row_height - 2, 60 - top_edge - 1})
        if (rig.pixel(filler_x, y) != theme::grid_filler.argb) return false;
    if (rig.pixel(filler_x, top_edge) != theme::bar.argb || rig.pixel(filler_x, top_edge + 7) != theme::bar_edge.argb) return false;
    if (rig.pixel(side + 201, top_edge + 2) != theme::bar_edge.argb) return false;
    if (rig.pixel(side + 201, 20) != theme::grid_filler.argb || rig.pixel(side + 202, 20) != theme::grid_border.argb) return false;
    // 52 across holds the long title, at 2 a byte with its padding.
    if (rig.pixel(filler_x + 4, top_edge + 2) != theme::text.argb || rig.pixel(filler_x + 29, top_edge + 5) != theme::text.argb) return false;
    if (rig.pixel(filler_x + 30, top_edge + 2) != theme::bar.argb) return false;
    // It is no column: its header does not sort, and it shows no boundary and no buttons.
    rig.click(filler_x + 14, top_edge + 2);
    if (rig.grid().current_column()) return false;
    rig.click(filler_x + 44, top_edge + 2);
    if (rig.grid().current_column() || rig.grid().column_changes() != 0) return false;
    rig.move(filler_x + 14, top_edge + 2);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    // A scroll bar takes some of the room, and what is left is a little: 40 goes to the last column.
    Rig<> tall{20};
    if (tall.grid().column_width(2) != Width{90} || tall.pixel(side + 189, 20) != theme::bar_edge.argb) return false;
    // Another column growing takes the room, and the filler goes.
    rig.press(side + 50, 4);
    rig.move(side + 70, 4);
    rig.release(side + 70, 4);
    if (rig.grid().column_width(0) != Width{70} || rig.grid().column_width(2) != Width{82} || rig.pixel(side + 201, 20) == theme::grid_filler.argb) return false;
    // Too narrow: the set width, and a bar.
    rig.deliver(ResizeEvent{Size{Width{60}, Height{60}}});
    if (rig.grid().column_width(2) != Width{50} || rig.grid().thumb_across().empty()) return false;
    // Room again brings the filler back.
    rig.deliver(ResizeEvent{Size{Width{300}, Height{60}}});
    return rig.grid().column_width(2) == Width{50} && rig.grid().thumb_across().empty() && rig.pixel(290, 20) == theme::grid_filler.argb;
}
static_assert(the_room_left_is_taken_up());

[[nodiscard]] constexpr bool the_pointer_shows_a_boundary() {
    Rig<> rig{4};
    rig.move(10, 4);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    rig.move(54, 4);
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.press(54, 4);
    rig.move(80, 40);  // nothing changes while it is held
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.move(80, 4);
    rig.release(80, 4);  // the boundary came along, so the pointer is still on it
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.move(10, 30);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    rig.move(54, 30);  // the same place in the body is no boundary
    return rig.backend().cursor() == CursorShape::Default;
}
static_assert(the_pointer_shows_a_boundary());

struct FrameGridTag;
using FramedGrid = DataGrid<FrameGridTag, SmallRow, NoGridMessage, Column<"L", &SmallRow::left, 50>, Column<"Name", &SmallRow::name, 50>>;

// The frame's edges keep their own shapes, also when a column drag ends over one.
[[nodiscard]] constexpr bool the_frame_keeps_its_edges() {
    ApplicationCore core{HeadlessBackend{Size{Width{200}, Height{100}}}, window(window_frame(FramedGrid{}))};
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    const auto go = [&](auto event) {
        backend.inject(event);
        require(core.process_pending(SolidShaper{}).has_value(), "a frame failed");
    };
    // The client area starts at 4, 24, the border takes 4 more each way, and the first boundary is 50 in.
    const Point boundary{Left{58}, Top{30}};
    go(PointerMove{boundary});
    if (backend.cursor() != CursorShape::ResizeColumn) return false;
    const Point edge{Left{1}, Top{50}};
    go(PointerMove{edge});
    if (backend.cursor() != CursorShape::ResizeLeft) return false;
    go(PointerMove{boundary});
    go(PointerButton{boundary, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    go(PointerMove{edge});
    if (backend.cursor() != CursorShape::ResizeColumn) return false;
    go(PointerButton{edge, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
    if (backend.cursor() != CursorShape::ResizeLeft) return false;
    // Scrolling over the frame's own parts is not for the content.
    if (core.ui().get<FrameGridTag>().scroll_down() != Height{}) return false;
    go(PointerMove{Point{Left{100}, Top{50}}});
    return backend.cursor() == CursorShape::Default;
}
static_assert(the_frame_keeps_its_edges());

// Under a menu bar, as a program would have it: the bar keeps its button, and the grid takes what is left.
struct MenuButtonTag;
struct CountClicks {
    int* clicks;
    template <class U>
    constexpr auto operator()(U&) const -> void {
        ++*clicks;
    }
};
[[nodiscard]] constexpr bool a_menu_bar_above_keeps_working() {
    int clicks = 0;
    ApplicationCore core{HeadlessBackend{Size{Width{200}, Height{100}}}, window(window_frame(menu_bar(button<MenuButtonTag>("x", CountClicks{&clicks})), FramedGrid{}))};
    if (!core.process_pending(SolidShaper{})) return false;
    auto& ui = core.ui();
    auto& grid = ui.get<FrameGridTag>();
    const Rect menu_button = ui.get<MenuButtonTag>().bounds();
    if (top_after(menu_button.bottom()) > grid.bounds().top()) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    const Point on_button = jgui_test::inside(menu_button, 2, 2);
    backend.inject(PointerMove{on_button});
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
    backend.inject(PointerScroll{on_button, ScrollDown::of_steps(1), ScrollAcross{}});
    if (!core.process_pending(SolidShaper{})) return false;
    return clicks == 1 && !grid.focused() && grid.scroll_down() == Height{};
}
static_assert(a_menu_bar_above_keeps_working());

// --- Damage ---
[[nodiscard]] constexpr bool motion_with_nothing_changing_draws_nothing() {
    Rig<> rig{20};
    const std::uint64_t settled = rig.frames();
    for (std::int32_t x = 0; x < 120; x += 7) rig.move(x, 30);
    for (std::int32_t y = 0; y < 60; y += 3) rig.move(50, y);
    rig.move(54, 4);
    rig.move(10, 4);
    rig.deliver(PointerLeave{});
    rig.move(500, 500);
    return rig.frames() == settled;
}
static_assert(motion_with_nothing_changing_draws_nothing());

// --- The pointer going away in the middle of something ---
[[nodiscard]] constexpr bool a_leave_ends_every_capture() {
    Rig<> rig{20};
    const Rect thumb = rig.grid().thumb_down();
    rig.press(number(thumb.left()) + 2, number(thumb.top()) + 2);
    rig.deliver(PointerLeave{});
    if (jgui_test::capture_active(rig.core.ui())) return false;
    rig.move(number(thumb.left()) + 2, 59);
    if (rig.grid().scroll_down() != Height{}) return false;
    rig.press(54, 4);
    rig.deliver(PointerLeave{});
    rig.move(70, 4);
    if (rig.grid().column_width(0) != Width{50}) return false;
    rig.press(10, 4);
    rig.deliver(PointerLeave{});
    rig.release(10, 4);
    return !rig.grid().current_column();
}
static_assert(a_leave_ends_every_capture());

[[nodiscard]] constexpr bool a_size_change_under_a_held_thumb_holds() {
    Rig<> rig{20};
    const Rect thumb = rig.grid().thumb_down();
    const std::int32_t x = number(thumb.left()) + 2;
    rig.press(x, number(thumb.top()) + 2);
    rig.move(x, 59);
    rig.deliver(ResizeEvent{Size{Width{210}, Height{500}}});
    if (rig.grid().scroll_down() != Height{}) return false;  // everything fits now, and no bar is left
    rig.move(x, 30);
    if (rig.grid().scroll_down() != Height{}) return false;
    rig.deliver(ResizeEvent{small_window});
    rig.move(x, 59);
    if (rig.grid().scroll_down() == Height{}) return false;  // the bar is back and still held
    rig.release(x, 59);
    return !jgui_test::capture_active(rig.core.ui());
}
static_assert(a_size_change_under_a_held_thumb_holds());

// --- Pixels, in static_assert ---
[[nodiscard]] constexpr bool the_picture_is_right() {
    Rig<> rig{20};
    // The header strip, in its own colour, with the first column's title on it.
    if (rig.pixel(60, top_edge + 7) != theme::bar_edge.argb || rig.pixel(115, top_edge + 1) != theme::bar.argb) return false;
    if (rig.pixel(side + 4, top_edge + 2) != theme::text.argb) return false;  // "L" starts after the padding, two down
    // A column line and a row line.
    if (rig.pixel(side + 49, top_edge + 12) != theme::bar_edge.argb || rig.pixel(100, row_y(0) + small_row_height - 2) != theme::bar_edge.argb) return false;
    // Nothing selected yet, then the highlight the width of the row.
    if (rig.pixel(100, row_y(1)) != theme::background.argb) return false;
    rig.click(10, row_y(1));
    if (rig.pixel(100, row_y(1)) != theme::button_pressed.argb || rig.pixel(100, row_y(0)) != theme::background.argb) return false;
    // The bar: its thumb at the top, and its trough below.
    const Rect thumb = rig.grid().thumb_down();
    if (rig.pixel(number(thumb.left()) + 3, number(thumb.top()) + 1) != theme::button_edge.argb) return false;
    if (rig.pixel(number(thumb.left()) + 3, 55) != theme::bar.argb) return false;
    // A sort mark, and it turns over: its top row is one pixel going up, and five going down, in a mark too small for a digit.
    rig.click(10, 4);
    rig.ctrl(Key::Up);
    const std::int32_t mark_left = side + 50 - 3 - 13 - 8;
    if (rig.pixel(mark_left + 2, top_edge + 1) != theme::sort_mark.argb || rig.pixel(mark_left, top_edge + 1) == theme::sort_mark.argb) return false;
    rig.ctrl(Key::Down);
    return rig.pixel(mark_left, top_edge + 1) == theme::sort_mark.argb && rig.pixel(mark_left + 4, top_edge + 1) == theme::sort_mark.argb;
}
static_assert(the_picture_is_right());

// Every side has its full width of border, focus darkens only the line touching the contents, and a press on it selects nothing.
[[nodiscard]] constexpr bool the_border_goes_all_round_and_keeps_its_colour() {
    Rig<> rig{20};
    // The window is 210 by 60. Each side, outermost pixel first.
    const auto line_is = [&](std::int32_t x, std::int32_t y, std::uint32_t colour) { return rig.pixel(x, y) == colour; };
    const auto soft = theme::grid_border.argb;
    const auto strong = theme::focus_edge.argb;
    const auto sides_are = [&](std::uint32_t last) {
        for (std::int32_t i = 0; i < side; ++i) {
            const std::uint32_t wanted = i == side - 1 ? last : soft;
            if (!line_is(i, 30, wanted) || !line_is(209 - i, 30, wanted) || !line_is(60, 59 - i, wanted)) return false;
        }
        for (std::int32_t i = 0; i < top_edge; ++i)
            if (!line_is(60, i, i == top_edge - 1 ? last : soft)) return false;
        return true;
    };
    if (!sides_are(soft)) return false;
    if (rig.grid().body_area().left() != Left{side} || rig.grid().header_area().top() != Top{top_edge}) return false;
    rig.click(0, row_y(1));
    if (!rig.grid().focused() || rig.grid().selected() || !sides_are(strong)) return false;
    // The bar and the body stop at the border.
    const Rect thumb = rig.grid().thumb_down();
    return number(thumb.right()) == 210 - side && number(rig.grid().body_area().bottom()) == 60 - top_edge;
}
static_assert(the_border_goes_all_round_and_keeps_its_colour());

// A cell's text is cut at its column, and scrolled text does not reach the header.
struct LongRow {
    std::string first{};
    std::string second{};
};
struct LongTag;
using LongGrid = DataGrid<LongTag, LongRow, NoGridMessage, Column<"A", &LongRow::first, 40>, Column<"B", &LongRow::second, 40>>;

[[nodiscard]] constexpr bool text_stays_inside_its_cell() {
    ApplicationCore core{HeadlessBackend{Size{Width{100}, Height{40}}}, window(LongGrid{})};
    auto& grid = core.ui().get<LongTag>();
    std::vector<LongRow> rows;
    for (int i = 0; i < 10; ++i) rows.push_back(LongRow{std::string(200, 'x'), std::string{}});
    if (!grid.set_rows(std::move(rows))) return false;
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    const auto colour = [&](std::int32_t x, std::int32_t y) { return backend.pixel(Point{Left{x}, Top{y}}); };
    if (colour(10, top_edge + 11) != theme::text.argb) return false;
    // Column A's last pixel is its line, and past it the empty column B is plain.
    if (colour(side + 39, top_edge + 11) != theme::bar_edge.argb || colour(side + 40, top_edge + 11) != theme::background.argb || colour(side + 60, top_edge + 11) != theme::background.argb) return false;
    // Half a row scrolled: the row's text is cut at the header's bottom edge.
    backend.inject(PointerScroll{Point{Left{10}, Top{20}}, ScrollDown::of_pixels(4), ScrollAcross{}});
    if (!core.process_pending(SolidShaper{})) return false;
    for (std::int32_t y = 0; y < top_edge + 7; ++y)
        if (colour(25, y) == theme::text.argb) return false;
    return colour(10, top_edge + 8) == theme::text.argb || colour(10, top_edge + 9) == theme::text.argb;
}
static_assert(text_stays_inside_its_cell());

// Only what shows is shaped, and a refused cell stays blank while a font failing ends the run.
[[nodiscard]] constexpr bool only_what_shows_is_shaped() {
    ApplicationCore core{HeadlessBackend{small_window}, window(SmallGrid{}, Echo{})};
    auto& grid = core.ui().get<GridTag>();
    if (!grid.set_rows(small_rows(5000))) return false;
    if (!core.process_pending(SolidShaper{})) return false;
    // 6 rows show, in part or whole in a body of 44, by 3 columns.
    return grid.cells_shaped() == 18;
}
static_assert(only_what_shows_is_shaped());

[[nodiscard]] constexpr bool refused_text_stays_blank() {
    ApplicationCore core{HeadlessBackend{Size{Width{100}, Height{40}}}, window(LongGrid{})};
    auto& grid = core.ui().get<LongTag>();
    if (!grid.set_rows(std::vector<LongRow>{LongRow{"refused", "fine"}})) return false;
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    if (backend.pixel(Point{Left{side + 5}, Top{top_edge + 11}}) != theme::background.argb || backend.pixel(Point{Left{side + 45}, Top{top_edge + 11}}) != theme::text.argb) return false;
    if (!grid.set_rows(std::vector<LongRow>{LongRow{"broken", ""}})) return false;
    return !core.process_pending(SolidShaper{}).has_value();
}
static_assert(refused_text_stays_blank());


// --- A column's two buttons, and what they do after the wait ---

// A shaper with a second face, wider and taller than the first, that can be made to refuse. It counts what the second face shapes.
struct WideFace {
    int* shaped{};
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (shaped != nullptr) ++*shaped;
        const Size size{Width{saturate_to<std::int32_t>(text.size() * 3)}, Height{6}};
        return TextRun{size, std::vector<std::uint8_t>(text.size() * 3 * 6, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{6}; }
};
struct TwoFaces {
    WideFace wide{};
    bool loads{true};
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const { return SolidShaper{}.shape(text); }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
    [[nodiscard]] constexpr std::expected<std::reference_wrapper<const WideFace>, Error> monospace_face() const {
        if (!loads) return make_error(ErrorCode::Font, "no monospace here");
        return std::cref(wide);
    }
};
static_assert(HasMonospaceFace<TwoFaces> && !HasMonospaceFace<SolidShaper> && TextShaper<SolidShaper>);

// Each letter its own width, so a count of its pixels says which it is.
struct LettersDiffer {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        std::int32_t across = saturate_to<std::int32_t>(text.size() * 2);
        if (text == "C") across = 4;
        if (text == "R") across = 6;
        if (text == "M") across = 8;
        return TextRun{Size{Width{across}, Height{4}}, std::vector<std::uint8_t>(static_cast<std::size_t>(across) * 4, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};

[[nodiscard]] constexpr bool the_buttons_show_their_letters_at_once() {
    Rig<LettersDiffer> rig{4};
    SmallGrid& grid = rig.grid();
    const Rect font = grid.font_button(0);
    const Rect align = grid.align_button(0);
    // Each is almost the whole header's height, and they sit side by side with a pixel between them.
    if (font.height() != Height{6} || align.height() != Height{6} || font.top() != align.top() || left_after(left_after(font.right()) + grid_controls_gap) != align.left()) return false;
    // Regular: blank, with its outline. Left: an L, 2 across and 4 down.
    if (rig.text_pixels(font) != 0 || rig.pixel(number(font.left()), number(font.top())) != theme::button_edge.argb) return false;
    if (rig.text_pixels(align) != 8) return false;
    rig.press_on(font);
    if (grid.column_font(0) != ColumnFont::Monospace || rig.text_pixels(font) != 32) return false;
    rig.press_on(font);
    if (grid.column_font(0) != ColumnFont::Regular || rig.text_pixels(font) != 0) return false;
    rig.press_on(align);
    if (grid.column_align(0) != ColumnAlign::AtCenter || rig.text_pixels(align) != 16) return false;
    rig.press_on(align);
    if (grid.column_align(0) != ColumnAlign::AtRight || rig.text_pixels(align) != 24) return false;
    rig.press_on(align);
    if (grid.column_align(0) != ColumnAlign::AtLeft || rig.text_pixels(align) != 8) return false;
    // Only that column, and nothing has taken effect yet.
    return grid.column_align(1) == ColumnAlign::AtLeft && grid.column_font_applied(0) == ColumnFont::Regular && grid.column_align_applied(0) == ColumnAlign::AtLeft;
}
static_assert(the_buttons_show_their_letters_at_once());

[[nodiscard]] constexpr bool the_column_changes_once_after_the_last_press() {
    int shaped_wide = 0;
    Rig<TwoFaces> rig{4, small_window, TwoFaces{WideFace{&shaped_wide}}};
    SmallGrid& grid = rig.grid();
    const Rect font = grid.font_button(0);
    const std::uint64_t before = rig.frames();
    rig.press_on(font);  // M
    rig.wait_ms(100);
    rig.press_on(font);  // blank
    rig.wait_ms(100);
    rig.press_on(font);  // M
    // A frame for each letter, and the other face not touched: nothing about the column has changed.
    if (rig.frames() != before + 3 || shaped_wide != 0 || grid.column_font_applied(0) != ColumnFont::Regular) return false;
    if (rig.core.next_wake() != MonotonicTime{}.after(grid_column_change_delay + std::chrono::milliseconds{200})) return false;
    rig.wait_ms(299);
    if (rig.frames() != before + 3 || grid.column_font_applied(0) != ColumnFont::Regular || grid.column_changes() != 0) return false;
    rig.wait_ms(1);
    if (rig.frames() != before + 4 || grid.column_font_applied(0) != ColumnFont::Monospace || grid.column_changes() != 1) return false;
    if (shaped_wide == 0 || rig.core.next_wake()) return false;
    // And it is over: time goes by and nothing draws.
    rig.wait_ms(100000);
    return rig.frames() == before + 4 && grid.column_changes() == 1;
}
static_assert(the_column_changes_once_after_the_last_press());

[[nodiscard]] constexpr bool each_press_starts_that_buttons_wait_again() {
    Rig<> rig{4};
    SmallGrid& grid = rig.grid();
    rig.press_on(grid.align_button(0));
    rig.wait_ms(250);
    rig.press_on(grid.align_button(0));
    rig.wait_ms(50);  // 300 since the first
    if (grid.column_align_applied(0) != ColumnAlign::AtLeft) return false;
    rig.wait_ms(249);
    if (grid.column_align_applied(0) != ColumnAlign::AtLeft) return false;
    rig.wait_ms(1);
    if (grid.column_align_applied(0) != ColumnAlign::AtRight) return false;
    // The other button's wait is its own.
    rig.press_on(grid.align_button(1));
    rig.wait_ms(200);
    rig.press_on(grid.font_button(1));
    rig.wait_ms(100);
    return grid.column_align_applied(1) == ColumnAlign::AtCenter && grid.column_font(1) == ColumnFont::Monospace && grid.column_font_applied(1) == ColumnFont::Regular && rig.core.next_wake().has_value();
}
static_assert(each_press_starts_that_buttons_wait_again());

[[nodiscard]] constexpr bool going_round_to_where_it_was_changes_nothing() {
    Rig<> rig{4};
    SmallGrid& grid = rig.grid();
    for (int i = 0; i < 3; ++i) rig.press_on(grid.align_button(0));
    const std::uint64_t frames = rig.frames();
    rig.wait_ms(300);
    return rig.frames() == frames && grid.column_changes() == 0 && grid.column_align(0) == ColumnAlign::AtLeft && !rig.core.next_wake();
}
static_assert(going_round_to_where_it_was_changes_nothing());

[[nodiscard]] constexpr bool each_alignment_has_its_pixels() {
    Rig<> rig{1};
    SmallGrid& grid = rig.grid();
    // Column 0 goes from 4 to 54. Its title, 2 across, has from 8 up to 26, before its padding and the sort mark's room. Its cell, "0", 2 across, has 8 to 50.
    const auto title_at = [&](std::int32_t x) { return rig.pixel(x, top_edge + 3) == theme::text.argb && rig.pixel(x + 1, top_edge + 3) == theme::text.argb; };
    const auto cell_at = [&](std::int32_t x) { return rig.pixel(x, row_y(0) + 3) == theme::text.argb && rig.pixel(x + 1, row_y(0) + 3) == theme::text.argb; };
    if (!title_at(8) || !cell_at(8) || title_at(16) || cell_at(28)) return false;
    rig.press_on(grid.align_button(0));
    if (!title_at(8) || !cell_at(8)) return false;  // not yet
    rig.wait_ms(300);
    if (title_at(8) || cell_at(8) || !title_at(16) || !cell_at(28)) return false;
    rig.press_on(grid.align_button(0));
    rig.wait_ms(300);
    if (title_at(16) || cell_at(28) || !title_at(24) || !cell_at(48)) return false;
    // The call has no wait, and takes the wait away.
    rig.press_on(grid.align_button(0));
    if (!grid.set_column_align(0, ColumnAlign::AtCenter)) return false;
    rig.settle();
    if (!title_at(16) || !cell_at(28) || rig.core.next_wake()) return false;
    rig.wait_ms(300);
    return title_at(16) && grid.column_align(0) == ColumnAlign::AtCenter;
}
static_assert(each_alignment_has_its_pixels());

// From the right end of column 0, which ends at 54: the boundary's reach, then the align button, the font button, and then an ordinary press on the header.
[[nodiscard]] constexpr bool a_press_goes_to_what_is_under_it() {
    Rig<> rig{4};
    SmallGrid& grid = rig.grid();
    if (grid.align_button(0).right() != Right{51} || grid.align_button(0).left() != Left{45} || grid.font_button(0).right() != Right{44} || grid.font_button(0).left() != Left{38}) return false;
    for (const std::int32_t x : {51, 52, 53, 54, 57}) {
        rig.press(x, 4);
        const bool held = jgui_test::capture_active(rig.core.ui());
        rig.deliver(PointerLeave{});
        if (!held) return false;
    }
    // Every height of the header, and the gap between them, is a button. None of these holds the pointer, sorts or selects.
    const auto pressed_only = [&](std::int32_t x, std::int32_t y) {
        rig.press(x, y);
        const bool held = jgui_test::capture_active(rig.core.ui());
        rig.release(x, y);
        return !held && !grid.current_column() && !grid.selected() && grid.column_width(0) == Width{50};
    };
    if (!pressed_only(50, 4) || grid.column_align(0) != ColumnAlign::AtCenter) return false;
    if (!pressed_only(45, 11) || grid.column_align(0) != ColumnAlign::AtRight) return false;
    if (!pressed_only(44, 4) || grid.column_font(0) != ColumnFont::Monospace) return false;
    if (!pressed_only(38, 11) || grid.column_font(0) != ColumnFont::Regular) return false;
    rig.move(45, 4);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    rig.click(37, 4);
    return grid.current_column() == 0;
}
static_assert(a_press_goes_to_what_is_under_it());

[[nodiscard]] constexpr bool settings_stay_through_all_of_it() {
    Rig<TwoFaces> rig{4};
    SmallGrid& grid = rig.grid();
    rig.press_on(grid.font_button(1));
    rig.press_on(grid.align_button(1));
    rig.wait_ms(300);
    const auto kept = [&] { return grid.column_font(1) == ColumnFont::Monospace && grid.column_font_applied(1) == ColumnFont::Monospace && grid.column_align(1) == ColumnAlign::AtCenter && grid.column_align_applied(1) == ColumnAlign::AtCenter; };
    if (!kept()) return false;
    rig.click(10, 4);
    rig.ctrl(Key::Up);
    if (!kept() || grid.sort_keys().empty()) return false;
    rig.press(number(grid.column_width(0)) + side, 4);
    rig.move(150, 4);
    rig.release(150, 4);
    if (!kept()) return false;
    if (!grid.set_rows(small_rows(9))) return false;
    rig.settle();
    rig.deliver(ResizeEvent{Size{Width{300}, Height{90}}});
    return kept();
}
static_assert(settings_stay_through_all_of_it());

[[nodiscard]] constexpr bool every_row_is_as_tall_as_the_tallest_font_in_use() {
    Rig<TwoFaces> rig{4};
    SmallGrid& grid = rig.grid();
    if (grid.row_height() != Height{8} || grid.header_area().height() != Height{8}) return false;
    if (!grid.set_column_font(1, ColumnFont::Monospace)) return false;
    rig.settle();
    if (grid.row_height() != Height{10} || grid.header_area().height() != Height{10}) return false;
    // In the second row: a column of the shorter font sits in the middle, a pixel lower than its padding. The other fills its line.
    const std::int32_t row_top = top_edge + 10 + 10;
    if (rig.pixel(8, row_top + 2) == theme::text.argb || rig.pixel(8, row_top + 3) != theme::text.argb) return false;
    if (rig.pixel(58, row_top + 2) != theme::text.argb) return false;
    if (!grid.set_column_font(1, ColumnFont::Regular)) return false;
    rig.settle();
    return grid.row_height() == Height{8};
}
static_assert(every_row_is_as_tall_as_the_tallest_font_in_use());

[[nodiscard]] constexpr bool a_font_that_will_not_load_leaves_the_column_regular() {
    Rig<TwoFaces> rig{4, small_window, TwoFaces{WideFace{}, false}};
    SmallGrid& grid = rig.grid();
    rig.press_on(grid.font_button(0));
    if (grid.column_font(0) != ColumnFont::Monospace || grid.take_font_refusal()) return false;
    const std::uint64_t frames = rig.frames();
    rig.wait_ms(300);
    if (grid.column_font(0) != ColumnFont::Regular || grid.column_font_applied(0) != ColumnFont::Regular || rig.frames() != frames + 1) return false;
    if (rig.text_pixels(grid.font_button(0)) != 0) return false;
    const auto refused = grid.take_font_refusal();
    if (!refused || refused->code != ErrorCode::Font || grid.take_font_refusal()) return false;
    // The same again from the call, and from a shaper with no such face at all.
    if (!grid.set_column_font(1, ColumnFont::Monospace)) return false;
    rig.settle();
    if (!grid.take_font_refusal() || grid.column_font(1) != ColumnFont::Regular) return false;
    Rig<> simple{4};
    simple.grid().set_every_column_font(ColumnFont::Monospace);
    simple.settle();
    return simple.grid().take_font_refusal().has_value() && !simple.grid().every_column_monospace();
}
static_assert(a_font_that_will_not_load_leaves_the_column_regular());

// A shaper that refuses one text, or fails on one, for the letters.
struct LetterTrouble {
    std::string_view refused{};
    std::string_view broken{};
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        if (text == refused) return make_error(ErrorCode::GlyphTooLarge, "refused");
        if (text == broken) return make_error(ErrorCode::Font, "broken");
        return SolidShaper{}.shape(text);
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};

[[nodiscard]] constexpr bool a_refused_letter_is_only_a_blank_button() {
    Rig<LetterTrouble> rig{2, small_window, LetterTrouble{"L", "not this one"}};
    const Rect align = rig.grid().align_button(0);
    if (rig.text_pixels(align) != 0 || rig.pixel(number(align.left()), number(align.top())) != theme::button_edge.argb) return false;
    rig.press_on(align);
    if (rig.text_pixels(align) != 8) return false;  // C is fine
    ApplicationCore core{HeadlessBackend{small_window}, window(Echo{}, SmallGrid{})};
    return !core.process_pending(LetterTrouble{"not this one", "M"}).has_value();
}
static_assert(a_refused_letter_is_only_a_blank_button(), "and a font failing goes the way it does for any caption");

[[nodiscard]] constexpr bool a_column_that_is_not_there_is_refused() {
    Rig<> rig{2};
    SmallGrid& grid = rig.grid();
    const std::uint64_t frames = rig.frames();
    const auto refused = [](const std::expected<void, Error>& answer) { return !answer && answer.error().code == ErrorCode::SizeLimit; };
    if (!refused(grid.set_column_font(3, ColumnFont::Monospace)) || !refused(grid.set_column_align(3, ColumnAlign::AtRight)) || !refused(grid.set_column_title(3, CheckedText{"x"}))) return false;
    if (!refused(grid.set_column_font(std::numeric_limits<std::size_t>::max(), ColumnFont::Regular))) return false;
    rig.settle();
    return rig.frames() == frames;
}
static_assert(a_column_that_is_not_there_is_refused());

// --- How wide a column is, and its title ---

struct TitledTag;
using TitledGrid = DataGrid<TitledTag, SmallRow, NoGridMessage, Column<"A long title here", &SmallRow::left, 30>, Column<"B", &SmallRow::name, 100>>;

// 17 bytes at 2 across, with padding, the sort mark's room, the buttons and the boundary's reach.
constexpr Width whole_long_title{4 + 34 + 4 + 8 + 13 + 3};

template <class Shaper = SolidShaper>
struct TitledRig {
    constexpr explicit TitledRig(Shaper with = Shaper{}) : shaper{with}, core{HeadlessBackend{Size{Width{400}, Height{60}}}, window(TitledGrid{})} { settle(); }
    [[nodiscard]] constexpr TitledGrid& grid() { return core.ui().template get<TitledTag>(); }
    constexpr void settle() { jgui::require(core.process_pending(shaper).has_value(), "a frame failed"); }
    template <class E>
    constexpr void deliver(const E& event) {
        jgui_test::backend(core).inject(event);
        settle();
    }
    [[nodiscard]] constexpr std::uint32_t pixel(std::int32_t x, std::int32_t y) { return jgui_test::backend(core).pixel(Point{Left{x}, Top{y}}); }
    // Holds the first column's boundary, and lets go at this position.
    constexpr void drag_first_to(std::int32_t x) {
        const std::int32_t boundary = side + number(grid().column_width(0));
        deliver(PointerButton{Point{Left{boundary}, Top{top_edge}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
        deliver(PointerMove{Point{Left{x}, Top{top_edge}}});
        deliver(PointerButton{Point{Left{x}, Top{top_edge}}, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
    }
    Shaper shaper;
    ApplicationCore<HeadlessBackend, Window<TitledGrid>> core;
};

[[nodiscard]] constexpr bool a_long_title_shows_whole_at_first() {
    TitledRig<> rig;
    if (rig.grid().column_width(0) != whole_long_title) return false;
    // Every pixel across of the title, solid, and nothing past it but the bar, up to the buttons.
    for (std::int32_t x = side + 4; x < side + 4 + 34; ++x)
        if (rig.pixel(x, top_edge + 3) != theme::text.argb) return false;
    if (rig.pixel(side + 4 + 34, top_edge + 3) != theme::bar.argb) return false;
    // A declared width past the title is kept.
    return rig.grid().column_width(1) == Width{100};
}
static_assert(a_long_title_shows_whole_at_first());

[[nodiscard]] constexpr bool a_drag_can_cut_the_title_and_stops_at_the_floor() {
    TitledRig<> rig;
    rig.drag_first_to(side + 40);
    TitledGrid& grid = rig.grid();
    if (grid.column_width(0) != Width{40}) return false;
    const Rect font = grid.font_button(0);
    // The title stops cleanly at the font button, whose outline and inside are its own.
    const std::int32_t cut = number(font.left());
    if (rig.pixel(cut - 1, top_edge + 3) != theme::text.argb || rig.pixel(cut, top_edge + 3) != theme::button_edge.argb || rig.pixel(cut + 1, top_edge + 3) != theme::button_normal.argb) return false;
    rig.drag_first_to(int_min);
    if (grid.column_width(0) != small_drag_floor) return false;
    // At the floor: padding, one letter of title, and the buttons whole.
    if (number(grid.font_button(0).left()) != side + 6 || grid.align_button(0).right() != Right{side + 22 - 3}) return false;
    return rig.pixel(side + 5, top_edge + 3) == theme::text.argb && rig.pixel(side + 6, top_edge + 3) == theme::button_edge.argb && rig.pixel(side + 7, top_edge + 3) == theme::button_normal.argb;
}
static_assert(a_drag_can_cut_the_title_and_stops_at_the_floor());

[[nodiscard]] constexpr bool a_column_no_one_has_dragged_follows_its_title_and_font() {
    TitledRig<TwoFaces> rig;
    TitledGrid& grid = rig.grid();
    if (grid.column_width(0) != whole_long_title) return false;
    std::int32_t mark_room = 8;
    const auto whole_of = [&mark_room](std::int32_t title) { return Width{4 + title + 4 + mark_room + 13 + 3}; };
    if (!grid.set_column_title(0, CheckedText{"A title that is longer than it was"})) return false;
    rig.settle();
    if (grid.column_width(0) != whole_of(68) || grid.column_title(0) != "A title that is longer than it was") return false;
    // Shorter comes back as far as the whole of this title, which is past the width declared.
    if (!grid.set_column_title(0, CheckedText{"Mid"})) return false;
    rig.settle();
    if (grid.column_width(0) != whole_of(6)) return false;
    if (!grid.set_column_title(0, CheckedText{""})) return false;
    rig.settle();
    if (grid.column_width(0) != whole_of(0)) return false;
    if (!grid.set_column_title(0, CheckedText{"A long title here"})) return false;
    if (!grid.set_column_font(0, ColumnFont::Monospace)) return false;
    rig.settle();
    // The other face is 3 across a byte, and a row of 10 makes the mark's room 10 too.
    mark_room = 10;
    if (grid.column_width(0) != whole_of(51)) return false;
    for (std::int32_t x = side + 4; x < side + 4 + 51; ++x)
        if (rig.pixel(x, top_edge + 3) != theme::text.argb) return false;
    if (!grid.set_column_font(0, ColumnFont::Regular)) return false;
    rig.settle();
    return grid.column_width(0) == whole_long_title;
}
static_assert(a_column_no_one_has_dragged_follows_its_title_and_font());

[[nodiscard]] constexpr bool a_dragged_column_keeps_the_width_it_was_given() {
    TitledRig<TwoFaces> rig;
    TitledGrid& grid = rig.grid();
    rig.drag_first_to(side + 40);
    if (grid.column_width(0) != Width{40}) return false;
    if (!grid.set_column_title(0, CheckedText{"A title that is longer than it was"})) return false;
    if (!grid.set_column_font(0, ColumnFont::Monospace)) return false;
    rig.settle();
    if (grid.column_width(0) != Width{40}) return false;
    if (!grid.set_column_title(0, CheckedText{"x"})) return false;
    rig.settle();
    if (grid.column_width(0) != Width{40}) return false;
    rig.drag_first_to(int_min);
    if (!grid.set_column_title(0, CheckedText{"A title that is longer than it was"})) return false;
    rig.settle();
    return grid.column_width(0) == small_drag_floor;
}
static_assert(a_dragged_column_keeps_the_width_it_was_given());

[[nodiscard]] constexpr bool the_sort_mark_hides_and_comes_back() {
    Rig<> rig{4};
    SmallGrid& grid = rig.grid();
    rig.click(10, 4);
    rig.ctrl(Key::Down);  // descending, whose top row is the mark's full width
    constexpr std::int32_t mark_at_50 = side + 50 - 3 - 13 - 8;
    if (rig.pixel(mark_at_50, top_edge + 1) != theme::sort_mark.argb) return false;
    // Dragged to 30, whole is 34, and there is no room for it: it does not draw, though the sort stands.
    rig.press(54, 4);
    rig.move(34, 4);
    rig.release(34, 4);
    if (grid.column_width(0) != Width{30} || !rig.sorted_by({GridSortKey{0, true}})) return false;
    for (std::int32_t x = side; x < side + 30; ++x)
        for (std::int32_t y = top_edge; y < top_edge + 7; ++y)
            if (rig.pixel(x, y) == theme::sort_mark.argb) return false;
    // The keys still work, with no mark to show it.
    rig.ctrl(Key::Up);
    if (!rig.sorted_by({GridSortKey{0, false}})) return false;
    rig.ctrl(Key::Down);
    rig.press(34, 4);
    rig.move(54, 4);
    rig.release(54, 4);
    return grid.column_width(0) == Width{50} && rig.pixel(mark_at_50, top_edge + 1) == theme::sort_mark.argb;
}
static_assert(the_sort_mark_hides_and_comes_back());

// A title past the most a column can be: the column stops at that, and the title is cut at the buttons, with nothing overflowing.
[[nodiscard]] constexpr bool a_title_past_the_limit_stops_there() {
    TitledRig<> rig;
    TitledGrid& grid = rig.grid();
    const std::string very_long(max_caption_bytes, 'x');
    const auto checked = CheckedText::check(very_long);
    if (!checked || !grid.set_column_title(0, *checked)) return false;
    rig.settle();
    return grid.column_width(0) == grid_column_most && !grid.thumb_across().empty() && grid.font_button(0).width() == small_button;
}
static_assert(a_title_past_the_limit_stops_there());

// Letters 6 across, so the filler's titles are 78 and 54 across: the two widths the filler can hold one or the other.
struct WideLetters {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view text) const {
        return TextRun{Size{Width{saturate_to<std::int32_t>(text.size() * 6)}, Height{4}}, std::vector<std::uint8_t>(text.size() * 6 * 4, 255)};
    }
    [[nodiscard]] constexpr Height line_height() const noexcept { return Height{4}; }
};
struct FillerTag;
using FillerGrid = DataGrid<FillerTag, SmallRow, NoGridMessage, Column<"Z", &SmallRow::left, 60>>;

// The filler is at least as wide as the column before it, and the rest of the window sets how much more. It shows the longest title that fits, then the shorter, then none.
[[nodiscard]] constexpr bool the_filler_has_the_title_that_fits() {
    const auto filler_title_ends = [](std::int32_t filler) {
        ApplicationCore core{HeadlessBackend{Size{Width{8 + 60 + filler}, Height{60}}}, window(FillerGrid{})};
        jgui::require(core.process_pending(WideLetters{}).has_value(), "a frame failed");
        std::int32_t last = 0;
        for (std::int32_t x = side + 60 + 4; x < side + 60 + filler - 1; ++x)
            if (jgui_test::backend(core).pixel(Point{Left{x}, Top{top_edge + 3}}) == theme::text.argb) last = x - (side + 60 + 3);
        return last;
    };
    return filler_title_ends(61) == 0 && filler_title_ends(62) == 54 && filler_title_ends(85) == 54 && filler_title_ends(86) == 78;
}
static_assert(the_filler_has_the_title_that_fits());

// --- The window closing with a change waiting, and the loop running to a deadline ---
[[nodiscard]] constexpr bool closing_with_a_change_waiting_is_harmless() {
    ApplicationCore core{HeadlessBackend{small_window}, window(Echo{}, SmallGrid{})};
    if (!core.process_pending(SolidShaper{})) return false;
    SmallGrid& grid = core.ui().get<GridTag>();
    HeadlessBackend& backend = jgui_test::backend(core);
    const Point on_button = jgui_test::inside(grid.align_button(0), 1, 1);
    backend.inject(PointerButton{on_button, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    backend.inject(CloseEvent{});
    const std::uint64_t frames = core.frames();
    if (!core.run(SolidShaper{}) || !core.closed()) return false;
    return core.frames() == frames && !core.next_wake() && grid.column_align(0) == ColumnAlign::AtCenter && grid.column_align_applied(0) == ColumnAlign::AtLeft;
}
static_assert(closing_with_a_change_waiting_is_harmless());

[[nodiscard]] constexpr bool the_loop_itself_runs_to_the_deadline() {
    ApplicationCore core{HeadlessBackend{small_window}, window(Echo{}, SmallGrid{})};
    if (!core.process_pending(SolidShaper{})) return false;
    SmallGrid& grid = core.ui().get<GridTag>();
    HeadlessBackend& backend = jgui_test::backend(core);
    backend.inject(PointerButton{jgui_test::inside(grid.align_button(0), 1, 1), MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
    // With nothing else to come, a headless wait with a deadline goes to it, and the next, with none, ends the run.
    const auto ended = core.run(SolidShaper{});
    if (ended || ended.error().code != ErrorCode::WouldBlock) return false;
    return grid.column_align_applied(0) == ColumnAlign::AtCenter && backend.now() == MonotonicTime{}.after(grid_column_change_delay);
}
static_assert(the_loop_itself_runs_to_the_deadline());

// --- What needs the real font or real sizes ---

void test_the_limit_on_rows(Checker& check) {
    struct Number {
        int value{};
    };
    struct NumberTag;
    DataGrid<NumberTag, Number, NoGridMessage, Column<"Value", &Number::value>> grid;
    check(grid.set_rows(std::vector<Number>{Number{1}}).has_value(), "a row");
    const auto refused = grid.set_rows(std::vector<Number>(max_grid_rows + 1));
    check(!refused.has_value() && refused.error().code == ErrorCode::SizeLimit, "one over the limit is refused");
    check(grid.rows().size() == 1, "and the old rows stay");
    check(grid.set_rows(std::vector<Number>(max_grid_rows)).has_value() && grid.rows().size() == max_grid_rows, "exactly the limit is taken");
    check(grid.display_order().size() == max_grid_rows && grid.display_order().back() == max_grid_rows - 1, "and each has its place");
}

struct RealTag;
struct RealRow {
    int number{};
    std::string name{};
    std::string empty{};
    Width wide{};
};
using RealGrid = DataGrid<RealTag, RealRow, NoGridMessage, Column<"Number", &RealRow::number, 130>, Column<"Name", &RealRow::name, 130>, Column<"Empty", &RealRow::empty, 130>, Column<"Wide", &RealRow::wide, 130>>;

void test_the_real_font(Checker& check) {
    const Size size{Width{640}, Height{480}};
    auto app = make_application(HeadlessBackend{size}, window(window_frame(RealGrid{})));
    check(app.has_value(), "application starts");
    std::vector<RealRow> rows;
    for (int i = 0; i < 1000; ++i) rows.push_back(RealRow{i, std::string(5000, 'w') + "\xC3\xA9", std::string{}, Width{i}});
    auto& grid = app->ui().get<RealTag>();
    check(grid.set_rows(std::move(rows)).has_value(), "rows taken");
    check(app->process_pending().has_value(), "first frame, with text longer than the shaper would take");
    auto& backend = jgui_test::backend(*app);

    const Rect header = grid.header_area();
    const Rect body = grid.body_area();
    check(number(grid.row_height()) > 4 && number(grid.row_height()) < 64, "a row's height comes from the font");
    check(grid.header_area().height() == grid.row_height(), "and so does the header's");
    check(jgui_test::count_other_than(backend, Rect{header.origin(), Size{Width{60}, header.height() - Height{1}}}, theme::bar) > 0, "the first title is drawn");

    // The long text stops at its column: the empty column beside it holds only its lines.
    const Left name_end = left_after(body.left() + Width{260});
    const Rect beside{Point{name_end, body.top()}, Size{Width{129}, grid.row_height() - Height{1}}};
    check(jgui_test::count_other_than(backend, beside, theme::background) == 0, "no text past its column");
    const Rect name_cell{Point{left_after(body.left() + Width{130}), body.top()}, Size{Width{129}, grid.row_height() - Height{1}}};
    check(jgui_test::count_other_than(backend, name_cell, theme::background) > 0, "and the text itself is there");

    // Scrolling all but two pixels of the first row under the header, its text and its line with it, leaves the header as it was.
    const Height scrolled = grid.row_height() - Height{2};
    std::vector<std::uint32_t> header_before;
    const auto read_header = [&] {
        std::vector<std::uint32_t> pixels;
        for (std::int32_t y = number(header.top()); y < number(header.bottom()); ++y)
            for (std::int32_t x = number(header.left()); x < number(header.right()); ++x)
                pixels.push_back(backend.pixel(Point{Left{x}, Top{y}}));
        return pixels;
    };
    header_before = read_header();
    const std::uint64_t frames = app->frames();
    backend.inject(PointerScroll{jgui_test::centre(body), ScrollDown::of_pixels(number(scrolled)), ScrollAcross{}});
    check(app->process_pending().has_value() && app->frames() == frames + 1 && grid.scroll_down() == scrolled, "that far down");
    check(read_header() == header_before, "no row reaches the header");

    // A click on what shows selects it, at the scrolled place.
    jgui_test::click(*app, jgui_test::inside(body, 300, number(grid.row_height()) * 2));
    check(grid.selected() == GridRow{2}, "the row under the pointer, as scrolled");
    const Top lit_row = (body.top() + (repeated_extent(grid.row_height(), 2) + Height{3})) - scrolled;
    check(backend.pixel(Point{left_after(body.left() + Width{200}), lit_row}) == theme::button_pressed.argb, "its row is lit");

    // The thumb is drawn where the grid says, in the bar beside the body.
    const Rect thumb = grid.thumb_down();
    check(thumb.left() >= left_after(body.right()) && backend.pixel(jgui_test::centre(thumb)) == theme::button_edge.argb, "the thumb");
    check(grid.thumb_across().empty(), "no bar across when the columns fit");

    // A fresh frame costs the same however many rows are behind it.
    const std::size_t shaped = grid.cells_shaped();
    check(shaped < 200, "only what shows is shaped");

    // The real faces: a press on the font button, the wait moved on by hand, and a column in the monospace font.
    const Height regular_row = grid.row_height();
    jgui_test::click(*app, jgui_test::centre(grid.font_button(1)));
    check(grid.column_font(1) == ColumnFont::Monospace && grid.column_font_applied(1) == ColumnFont::Regular, "the letter first");
    backend.advance_time(grid_column_change_delay);
    check(app->process_pending().has_value(), "the wait ends");
    check(grid.column_font_applied(1) == ColumnFont::Monospace, "and the column has it, on a system that has such a font");
    check(grid.row_height() >= regular_row, "with every row as tall as it needs");
    std::cout << "with the real fonts: a row is " << number(regular_row) << " high, and " << number(grid.row_height()) << " with monospace in use. A button is " << number(grid.font_button(1).width()) << " across.\n";
}

// What narrow columns come to with the real font, and the narrowest a drag can make one.
void report_the_real_widths(Checker& check) {
    struct One {
        int value{};
    };
    struct NarrowTag;
    using Narrow = DataGrid<NarrowTag, One, NoGridMessage, Column<"Left", &One::value, 60>, Column<"Top", &One::value, 60>, Column<"Height", &One::value, 60>, Column<"Slow", &One::value, 60>, Column<"Run_ID", &One::value, 70>,
                            Column<"SamePixels", &One::value, 90>, Column<"Previous_Write_ns", &One::value, 150>, Column<"Writes_Before_This", &One::value, 150>>;
    auto app = make_application(HeadlessBackend{Size{Width{1600}, Height{300}}}, window(Narrow{}));
    check(app.has_value() && app->process_pending().has_value(), "a grid of narrow columns");
    auto& grid = app->ui().get<NarrowTag>();
    for (std::size_t k = 0; k < 8; ++k) std::cout << grid.column_title(k) << " shows at " << number(grid.column_width(k)) << '\n';
    const std::int32_t boundary = number(grid.bounds().left()) + 4 + number(grid.column_width(0));
    auto& backend = jgui_test::backend(*app);
    backend.inject(PointerButton{Point{Left{boundary}, Top{10}}, MouseButton::Left, ButtonState::Pressed, jgui_test::test_serial});
    backend.inject(PointerMove{Point{Left{0}, Top{10}}});
    check(app->process_pending().has_value(), "dragged to the end");
    std::cout << "the narrowest a drag can make a column: " << number(grid.column_width(0)) << '\n';
}

}  // namespace

int main() {
    Checker check;
    test_the_limit_on_rows(check);
    test_the_real_font(check);
    report_the_real_widths(check);
    return check.finish();
}
