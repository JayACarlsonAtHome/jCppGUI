// File: tests/test_datagrid.cpp
// Purpose: DataGrid. In static_assert, the whole grid from injected events to the state and the pixels, with a shaper that
//          needs no font. At run time, with the real font: the limit on rows and what the picture looks like.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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

using SmallGrid = DataGrid<GridTag, SmallRow, Picked, Column<"L", &SmallRow::left, 30>, Column<"Name", &SmallRow::name, 30>, Column<"On", &SmallRow::on, 24>>;
static_assert(Widget<SmallGrid>);

constexpr Size small_window{Width{120}, Height{60}};

[[nodiscard]] constexpr std::vector<SmallRow> small_rows(std::int32_t count) {
    std::vector<SmallRow> rows;
    for (std::int32_t i = 0; i < count; ++i) rows.push_back(SmallRow{Left{i}, std::string{"n"}, i % 2 == 0});
    return rows;
}

// A tree with one grid and its echo, a shaper, and short ways to send it things. Each test is a function that runs in constant evaluation.
template <std::size_t = 0>
struct Rig {
    constexpr explicit Rig(std::int32_t rows, Size size = small_window) : core{HeadlessBackend{size}, window(Echo{}, SmallGrid{})} {
        jgui::require(grid().set_rows(small_rows(rows)).has_value(), "rows refused");
        settle();
    }
    [[nodiscard]] constexpr SmallGrid& grid() { return core.ui().template get<GridTag>(); }
    [[nodiscard]] constexpr Echo& echo() { return core.ui().template get<EchoTag>(); }
    [[nodiscard]] constexpr HeadlessBackend& backend() { return jgui_test::backend(core); }
    constexpr void settle() { jgui::require(core.process_pending(SolidShaper{}).has_value(), "a frame failed"); }
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
    constexpr void wheel(std::int32_t steps) { deliver(PointerScroll{at(10, 20), ScrollDown::of_steps(steps), ScrollAcross{}}); }
    [[nodiscard]] static constexpr Point at(std::int32_t x, std::int32_t y) { return Point{Left{x}, Top{y}}; }
    [[nodiscard]] constexpr std::uint32_t pixel(std::int32_t x, std::int32_t y) { return backend().pixel(at(x, y)); }
    [[nodiscard]] constexpr std::uint64_t frames() const { return core.frames(); }

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
    if (few.grid().body_area().width() != Width{112}) return false;
    Rig<> many{20};
    if (many.grid().thumb_down().empty() || !many.grid().thumb_across().empty()) return false;
    if (many.grid().body_area().width() != Width{100}) return false;
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
    rig.deliver(ResizeEvent{Size{Width{120}, Height{200}}});
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
[[nodiscard]] constexpr bool a_header_sorts_and_a_second_click_reverses() {
    Rig<> rig{0};
    // Ties on purpose: two Bs, with their rows' own order to keep.
    if (!rig.grid().set_rows(std::vector<SmallRow>{SmallRow{Left{0}, "B", true}, SmallRow{Left{1}, "A", true}, SmallRow{Left{2}, "B", false}, SmallRow{Left{3}, "C", false}})) return false;
    rig.settle();
    rig.click(10, row_y(0));  // row 0, a "B"
    rig.click(40, 4);         // the second column's header
    const auto order = [&](std::array<std::uint32_t, 4> wanted) {
        const auto shown = rig.grid().display_order();
        for (std::size_t i = 0; i < wanted.size(); ++i)
            if (shown[i] != wanted[i]) return false;
        return true;
    };
    if (rig.grid().sort() != GridSort{1, false} || !order({1, 0, 2, 3})) return false;
    if (rig.grid().selected() != GridRow{0} || rig.echo().told != 1) return false;  // the same row, and nothing to tell
    if (rig.grid().rows()[0].name != "B") return false;  // the rows themselves have not moved
    rig.key(Key::Down);
    if (rig.grid().selected() != GridRow{2}) return false;  // the next one as shown
    rig.click(40, 4);
    if (rig.grid().sort() != GridSort{1, true} || !order({3, 0, 2, 1})) return false;  // ties in their own order both ways
    if (rig.grid().selected() != GridRow{2}) return false;
    rig.click(40, 4);
    if (rig.grid().sort() != GridSort{1, false}) return false;
    rig.click(10, 4);  // another column starts ascending
    if (rig.grid().sort() != GridSort{0, false} || !order({0, 1, 2, 3})) return false;
    rig.click(70, 4);  // a bool: false first
    return order({2, 3, 0, 1});
}
static_assert(a_header_sorts_and_a_second_click_reverses());

[[nodiscard]] constexpr bool a_header_press_ended_elsewhere_does_not_sort() {
    Rig<> rig{4};
    rig.press(10, 4);
    if (!jgui_test::capture_active(rig.core.ui())) return false;
    rig.release(40, 4);
    if (rig.grid().sort()) return false;
    rig.press(10, 4);
    rig.release(10, 30);
    if (rig.grid().sort()) return false;
    // Above a scroll bar is no column.
    Rig<> barred{20};
    barred.click(110, 4);
    return !barred.grid().sort();
}
static_assert(a_header_press_ended_elsewhere_does_not_sort());

[[nodiscard]] constexpr bool a_boundary_drags_to_resize_within_limits() {
    Rig<> rig{4};
    // The first column's boundary is at x = 34.
    rig.press(34, 4);
    if (!jgui_test::capture_active(rig.core.ui())) return false;
    rig.move(54, 40);
    if (rig.grid().column_width(0) != Width{50}) return false;
    const std::uint64_t settled = rig.frames();
    rig.move(54, 50);
    if (rig.frames() != settled) return false;  // no change of width, nothing to draw
    rig.move(int_min, 4);
    if (rig.grid().column_width(0) != grid_column_least) return false;
    rig.move(int_max, 4);
    if (rig.grid().column_width(0) != grid_column_most) return false;
    if (rig.grid().thumb_across().empty()) return false;  // a very wide column brings a bar
    rig.move(44, 4);
    rig.release(44, 4);
    if (rig.grid().column_width(0) != Width{40} || rig.grid().sort()) return false;  // a drag does not also sort
    return !jgui_test::capture_active(rig.core.ui());
}
static_assert(a_boundary_drags_to_resize_within_limits());

// The columns reach the grid's edge: the last one takes a little room, a lot becomes a filler, and the last column never gets narrower than it was set.
[[nodiscard]] constexpr bool the_room_left_is_taken_up() {
    Rig<> rig{2};  // 30 + 30 + 24 in a body of 112 leaves 28, more than the last column: a filler
    if (rig.grid().column_width(2) != Width{24} || rig.grid().column_width(0) != Width{30}) return false;
    const std::int32_t filler_x = side + 84;
    if (rig.pixel(filler_x - 1, 20) != theme::bar_edge.argb || !rig.grid().thumb_across().empty()) return false;
    // It goes over the row lines to the bottom of the body, under a header like the others: the bar's colour, a line below and a line at its end.
    for (const std::int32_t y : {row_y(0), row_y(0) + small_row_height - 2, 60 - top_edge - 1})
        if (rig.pixel(filler_x, y) != theme::grid_filler.argb) return false;
    if (rig.pixel(filler_x, top_edge) != theme::bar.argb || rig.pixel(filler_x, top_edge + 7) != theme::bar_edge.argb) return false;
    if (rig.pixel(side + 111, top_edge + 2) != theme::bar_edge.argb) return false;
    if (rig.pixel(side + 111, 20) != theme::grid_filler.argb || rig.pixel(side + 112, 20) != theme::grid_border.argb) return false;
    // 28 across holds the short title, at 2 a byte with its padding, and not the long one.
    if (rig.pixel(filler_x + 4, top_edge + 2) != theme::text.argb || rig.pixel(filler_x + 21, top_edge + 5) != theme::text.argb) return false;
    if (rig.pixel(filler_x + 22, top_edge + 2) != theme::bar.argb) return false;
    // It is no column: its header does not sort, and it shows no boundary.
    rig.click(filler_x + 14, top_edge + 2);
    if (rig.grid().sort()) return false;
    rig.move(filler_x + 14, top_edge + 2);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    // 24 across holds neither title.
    Rig<> untitled{2, Size{Width{116}, Height{60}}};
    if (untitled.pixel(filler_x + 4, top_edge + 2) != theme::bar.argb) return false;
    // 116 across holds the long one, which runs on past where the short one ends.
    Rig<> wide{2, Size{Width{212}, Height{60}}};
    if (wide.pixel(filler_x + 4 + 25, top_edge + 2) != theme::text.argb || wide.pixel(filler_x + 4 + 26, top_edge + 2) != theme::bar.argb) return false;
    // A scroll bar takes some of the room, and what is left is a little: 16 goes to the last column.
    Rig<> tall{20};
    if (tall.grid().column_width(2) != Width{40} || tall.pixel(side + 99, 20) != theme::bar_edge.argb) return false;
    // Another column growing takes the room, and the filler goes.
    rig.press(side + 30, 4);
    rig.move(side + 50, 4);
    rig.release(side + 50, 4);
    if (rig.grid().column_width(0) != Width{50} || rig.grid().column_width(2) != Width{32} || rig.pixel(side + 111, 20) == theme::grid_filler.argb) return false;
    // Too narrow: the set width, and a bar.
    rig.deliver(ResizeEvent{Size{Width{60}, Height{60}}});
    if (rig.grid().column_width(2) != Width{24} || rig.grid().thumb_across().empty()) return false;
    // Room again brings the filler back.
    rig.deliver(ResizeEvent{Size{Width{200}, Height{60}}});
    return rig.grid().column_width(2) == Width{24} && rig.grid().thumb_across().empty() && rig.pixel(190, 20) == theme::grid_filler.argb;
}
static_assert(the_room_left_is_taken_up());

[[nodiscard]] constexpr bool the_pointer_shows_a_boundary() {
    Rig<> rig{4};
    rig.move(10, 4);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    rig.move(34, 4);
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.press(34, 4);
    rig.move(80, 40);  // nothing changes while it is held
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.move(80, 4);
    rig.release(80, 4);  // the boundary came along, so the pointer is still on it
    if (rig.backend().cursor() != CursorShape::ResizeColumn) return false;
    rig.move(10, 30);
    if (rig.backend().cursor() != CursorShape::Default) return false;
    rig.move(34, 30);  // the same place in the body is no boundary
    return rig.backend().cursor() == CursorShape::Default;
}
static_assert(the_pointer_shows_a_boundary());

struct FrameGridTag;
using FramedGrid = DataGrid<FrameGridTag, SmallRow, NoGridMessage, Column<"L", &SmallRow::left, 30>, Column<"Name", &SmallRow::name, 30>>;

// The frame's edges keep their own shapes, also when a column drag ends over one.
[[nodiscard]] constexpr bool the_frame_keeps_its_edges() {
    ApplicationCore core{HeadlessBackend{Size{Width{200}, Height{100}}}, window(window_frame(FramedGrid{}))};
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    const auto go = [&](auto event) {
        backend.inject(event);
        require(core.process_pending(SolidShaper{}).has_value(), "a frame failed");
    };
    // The client area starts at 4, 24, the border takes 4 more each way, and the first boundary is 30 in.
    const Point boundary{Left{38}, Top{30}};
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
    rig.move(34, 4);
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
    rig.press(34, 4);
    rig.deliver(PointerLeave{});
    rig.move(70, 4);
    if (rig.grid().column_width(0) != Width{30}) return false;
    rig.press(10, 4);
    rig.deliver(PointerLeave{});
    rig.release(10, 4);
    return !rig.grid().sort();
}
static_assert(a_leave_ends_every_capture());

[[nodiscard]] constexpr bool a_size_change_under_a_held_thumb_holds() {
    Rig<> rig{20};
    const Rect thumb = rig.grid().thumb_down();
    const std::int32_t x = number(thumb.left()) + 2;
    rig.press(x, number(thumb.top()) + 2);
    rig.move(x, 59);
    rig.deliver(ResizeEvent{Size{Width{120}, Height{500}}});
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
    if (rig.pixel(side + 29, top_edge + 12) != theme::bar_edge.argb || rig.pixel(100, row_y(0) + small_row_height - 2) != theme::bar_edge.argb) return false;
    // Nothing selected yet, then the highlight the width of the row.
    if (rig.pixel(100, row_y(1)) != theme::background.argb) return false;
    rig.click(10, row_y(1));
    if (rig.pixel(100, row_y(1)) != theme::button_pressed.argb || rig.pixel(100, row_y(0)) != theme::background.argb) return false;
    // The bar: its thumb at the top, and its trough below.
    const Rect thumb = rig.grid().thumb_down();
    if (rig.pixel(number(thumb.left()) + 3, number(thumb.top()) + 1) != theme::button_edge.argb) return false;
    if (rig.pixel(number(thumb.left()) + 3, 55) != theme::bar.argb) return false;
    // A sort mark, and it turns over: the top row of the triangle is one pixel going up, and five going down.
    rig.click(10, 4);
    const std::int32_t mark_left = side + 30 - 14;
    if (rig.pixel(mark_left + 4, top_edge + 2) != theme::text.argb || rig.pixel(mark_left + 2, top_edge + 2) == theme::text.argb) return false;
    rig.click(10, 4);
    return rig.pixel(mark_left + 2, top_edge + 2) == theme::text.argb && rig.pixel(mark_left + 6, top_edge + 2) == theme::text.argb;
}
static_assert(the_picture_is_right());

// Every side has its full width of border, focus darkens only the line touching the contents, and a press on it selects nothing.
[[nodiscard]] constexpr bool the_border_goes_all_round_and_keeps_its_colour() {
    Rig<> rig{20};
    // The window is 120 by 60. Each side, outermost pixel first.
    const auto line_is = [&](std::int32_t x, std::int32_t y, std::uint32_t colour) { return rig.pixel(x, y) == colour; };
    const auto soft = theme::grid_border.argb;
    const auto strong = theme::focus_edge.argb;
    const auto sides_are = [&](std::uint32_t last) {
        for (std::int32_t i = 0; i < side; ++i) {
            const std::uint32_t wanted = i == side - 1 ? last : soft;
            if (!line_is(i, 30, wanted) || !line_is(119 - i, 30, wanted) || !line_is(60, 59 - i, wanted)) return false;
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
    return number(thumb.right()) == 120 - side && number(rig.grid().body_area().bottom()) == 60 - top_edge;
}
static_assert(the_border_goes_all_round_and_keeps_its_colour());

// A cell's text is cut at its column, and scrolled text does not reach the header.
struct LongRow {
    std::string first{};
    std::string second{};
};
struct LongTag;
using LongGrid = DataGrid<LongTag, LongRow, NoGridMessage, Column<"A", &LongRow::first, 30>, Column<"B", &LongRow::second, 30>>;

[[nodiscard]] constexpr bool text_stays_inside_its_cell() {
    ApplicationCore core{HeadlessBackend{Size{Width{80}, Height{40}}}, window(LongGrid{})};
    auto& grid = core.ui().get<LongTag>();
    std::vector<LongRow> rows;
    for (int i = 0; i < 10; ++i) rows.push_back(LongRow{std::string(200, 'x'), std::string{}});
    if (!grid.set_rows(std::move(rows))) return false;
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    const auto colour = [&](std::int32_t x, std::int32_t y) { return backend.pixel(Point{Left{x}, Top{y}}); };
    if (colour(10, top_edge + 11) != theme::text.argb) return false;
    // Column A's last pixel is its line, and past it the empty column B is plain.
    if (colour(side + 29, top_edge + 11) != theme::bar_edge.argb || colour(side + 30, top_edge + 11) != theme::background.argb || colour(side + 40, top_edge + 11) != theme::background.argb) return false;
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
    ApplicationCore core{HeadlessBackend{Size{Width{80}, Height{40}}}, window(LongGrid{})};
    auto& grid = core.ui().get<LongTag>();
    if (!grid.set_rows(std::vector<LongRow>{LongRow{"refused", "fine"}})) return false;
    if (!core.process_pending(SolidShaper{})) return false;
    HeadlessBackend& backend = jgui_test::backend(core);
    if (backend.pixel(Point{Left{side + 5}, Top{top_edge + 11}}) != theme::background.argb || backend.pixel(Point{Left{side + 35}, Top{top_edge + 11}}) != theme::text.argb) return false;
    if (!grid.set_rows(std::vector<LongRow>{LongRow{"broken", ""}})) return false;
    return !core.process_pending(SolidShaper{}).has_value();
}
static_assert(refused_text_stays_blank());

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
using RealGrid = DataGrid<RealTag, RealRow, NoGridMessage, Column<"Number", &RealRow::number, 80>, Column<"Name", &RealRow::name, 100>, Column<"Empty", &RealRow::empty, 80>, Column<"Wide", &RealRow::wide, 80>>;

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
    check(jgui_test::count_other_than(backend, Rect{header.origin(), Size{Width{79}, header.height() - Height{1}}}, theme::bar) > 0, "the first title is drawn");

    // The long text stops at its column: the empty column beside it holds only its lines.
    const Left name_end = left_after(body.left() + Width{180});
    const Rect beside{Point{name_end, body.top()}, Size{Width{79}, grid.row_height() - Height{1}}};
    check(jgui_test::count_other_than(backend, beside, theme::background) == 0, "no text past its column");
    const Rect name_cell{Point{left_after(body.left() + Width{80}), body.top()}, Size{Width{99}, grid.row_height() - Height{1}}};
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
}

}  // namespace

int main() {
    Checker check;
    test_the_limit_on_rows(check);
    test_the_real_font(check);
    return check.finish();
}
