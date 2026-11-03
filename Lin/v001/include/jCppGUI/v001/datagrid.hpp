// File: include/jCppGUI/v001/datagrid.hpp
// Purpose: DataGrid<Tag, Row, Notify, Columns...>: a read-only table of rows the program gives it. Each column is its own type,
//          naming a member of Row, and its text comes from that member's CellFormat. Only rows that show are shaped and
//          painted, so what a frame costs does not depend on how many rows there are. The general parts are in scroll_bar.hpp,
//          spans.hpp, selection_step.hpp, index_sort.hpp and cell_text.hpp, and this holds only what is a grid's own.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "cell_text.hpp"
#include "error.hpp"
#include "events.hpp"
#include "geometry.hpp"
#include "index_sort.hpp"
#include "message.hpp"
#include "scroll_bar.hpp"
#include "selection_step.hpp"
#include "spans.hpp"
#include "text.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t max_grid_rows = 1'000'000;
inline constexpr std::int32_t max_grid_row_height = 2048;
inline constexpr std::int32_t grid_column_default = 120;
inline constexpr Width grid_column_least{24};
inline constexpr Width grid_column_most{4096};
inline constexpr Width grid_boundary_reach{4};
inline constexpr std::int32_t grid_wheel_rows = 3;
inline constexpr Width grid_sideways_step{48};
inline constexpr Width grid_sort_mark_room{14};
inline constexpr std::size_t grid_cell_most_bytes = 1024;

// Every row, at the tallest a row can be, and any window under it, stays inside what a length can hold.
static_assert(std::int64_t{max_grid_row_height} * std::int64_t{max_grid_rows} + 2 * max_buffer_side < std::int64_t{std::numeric_limits<std::int32_t>::max()});

// A row in the order the program gave them, whatever order they show in.
struct GridRow {
    std::size_t index{};
    friend constexpr bool operator==(const GridRow&, const GridRow&) = default;
};

// Which column the rows are sorted by.
struct GridSort {
    std::size_t column{};
    bool descending{};
    friend constexpr bool operator==(const GridSort&, const GridSort&) = default;
};

// For a grid that tells nobody when its selection changes.
struct NoGridMessage {};

// A column's title, known at compile time.
template <std::size_t N>
struct ColumnName {
    char text[N]{};
    consteval ColumnName(const char (&name)[N]) { std::copy_n(name, N, text); }
    [[nodiscard]] consteval std::string_view view() const { return std::string_view{text}; }
};

template <std::size_t N>
ColumnName(const char (&)[N]) -> ColumnName<N>;

namespace detail {
template <class M>
struct ColumnMember;
template <class R, class V>
struct ColumnMember<V R::*> {
    using Row = R;
    using Value = V;
};
}  // namespace detail

// A column: its title, the member of the row it shows, and how wide it starts. Each is its own type.
template <ColumnName Name, auto Member, std::int32_t StartWidth = grid_column_default>
struct Column {
    using Row = typename detail::ColumnMember<decltype(Member)>::Row;
    using Value = typename detail::ColumnMember<decltype(Member)>::Value;
    static_assert(HasCellFormat<Value>, "this column's member has no cell format");
    static_assert(std::totally_ordered<Value>, "this column's member has no order to sort by");

    [[nodiscard]] static consteval std::string_view name() { return Name.view(); }
    [[nodiscard]] static consteval CheckedText title() { return CheckedText{Name.view()}; }
    [[nodiscard]] static consteval Width start_width() { return std::clamp(Width{StartWidth}, grid_column_least, grid_column_most); }
    [[nodiscard]] static constexpr const Value& value_of(const Row& row) noexcept { return row.*Member; }
    [[nodiscard]] static constexpr Value& member_of(Row& row) noexcept { return row.*Member; }
};

namespace detail {

template <class... Columns>
[[nodiscard]] consteval bool column_names_differ() {
    const std::array<std::string_view, sizeof...(Columns)> names{Columns::name()...};
    for (std::size_t i = 0; i < names.size(); ++i)
        for (std::size_t k = i + 1; k < names.size(); ++k)
            if (names[i] == names[k]) return false;
    return true;
}

// How much of a cell's text is worth shaping in a column this wide. A glyph narrower than two pixels is not real text, so
// more than this cannot show, and text of any length costs the same.
[[nodiscard]] constexpr std::size_t cell_bytes_worth_shaping(Width column) noexcept {
    return std::min(whole_units_in(column, Width{2}) + 8, grid_cell_most_bytes);
}

}  // namespace detail

// Notify is a Message that takes one GridRow, sent when the selection changes, or NoGridMessage.
template <class Tag, class Row, class Notify, class... Columns>
class DataGrid {
    static_assert(sizeof...(Columns) > 0, "a grid needs at least one column");
    static_assert((std::same_as<typename Columns::Row, Row> && ...), "a column's member belongs to another row type");
    static_assert(detail::column_names_differ<Columns...>(), "two columns of a grid have the same name");

    static constexpr std::size_t column_count = sizeof...(Columns);

public:
    using tag = Tag;
    using row_type = Row;
    using column_types = std::tuple<Columns...>;

    constexpr DataGrid() : headers_{detail::ShapedCaption{Columns::title()}...}, widths_{Columns::start_width()...} {}
    DataGrid(const DataGrid&) = delete;
    DataGrid& operator=(const DataGrid&) = delete;
    constexpr DataGrid(DataGrid&&) = default;
    DataGrid& operator=(DataGrid&&) = delete;
    constexpr ~DataGrid() = default;

    // Takes the rows, in the order they first show. The selection and the sort do not carry over to new rows, and
    // no message says so, because the caller knows. More than the limit is refused, and the old rows stay.
    [[nodiscard]] constexpr std::expected<void, Error> set_rows(std::vector<Row> rows) {
        if (rows.size() > max_grid_rows) return make_error(ErrorCode::SizeLimit, "a grid holds at most 1000000 rows");
        rows_ = std::move(rows);
        order_ = identity_order(static_cast<std::uint32_t>(rows_.size()));
        selected_position_.reset();
        sort_.reset();
        place_parts();
        damaged_ = true;
        return {};
    }

    [[nodiscard]] constexpr std::span<const Row> rows() const noexcept { return rows_; }
    // Each position on the screen's row in the order given.
    [[nodiscard]] constexpr std::span<const std::uint32_t> display_order() const noexcept { return order_; }
    [[nodiscard]] constexpr std::optional<GridRow> selected() const noexcept {
        if (!selected_position_) return std::nullopt;
        return GridRow{order_[*selected_position_]};
    }
    [[nodiscard]] constexpr std::optional<GridSort> sort() const noexcept { return sort_; }
    [[nodiscard]] constexpr Height scroll_down() const noexcept { return down_.offset(); }
    [[nodiscard]] constexpr Width scroll_across() const noexcept { return across_.offset(); }
    [[nodiscard]] constexpr Width column_width(std::size_t column) const { return widths_.at(column); }
    [[nodiscard]] constexpr Height row_height() const noexcept { return row_height_; }
    [[nodiscard]] constexpr bool focused() const noexcept { return focused_; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr Rect header_area() const noexcept { return header_; }
    [[nodiscard]] constexpr Rect body_area() const noexcept { return body_; }
    [[nodiscard]] constexpr Rect thumb_down() const noexcept { return down_.thumb(); }
    [[nodiscard]] constexpr Rect thumb_across() const noexcept { return across_.thumb(); }
    // How many cells the last layout shaped: what a frame's text cost goes with.
    [[nodiscard]] constexpr std::size_t cells_shaped() const noexcept { return cells_shaped_; }

    // Takes all that is offered and leaves nothing below.
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper& text) {
        bounds_ = area;
        row_height_ = std::clamp(text.line_height() + theme::cell_pad_y + theme::cell_pad_y, Height{1}, Height{max_grid_row_height});
        place_parts();
        if (auto shaped = shape_headers(text); !shaped) return std::unexpected(std::move(shaped.error()));
        if (auto shaped = shape_visible_cells(text); !shaped) return std::unexpected(std::move(shaped.error()));
        return Rect{Point{area.left(), top_after(area.bottom())}, Size{area.width(), Height{}}};
    }

    constexpr void paint(const TreeKey&, Canvas& canvas) {
        const Canvas local = canvas.clipped(bounds_);
        local.fill_rect(bounds_, theme::background);
        paint_body(local.clipped(body_));
        paint_header(local);
        down_.paint(local);
        across_.paint(local);
        if (focused_) local.stroke_rect(bounds_, theme::focus_edge);
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerPress& event) {
        if (event.button != MouseButton::Left || !bounds_.contains(event.position)) return false;
        focused_ = true;
        if (const auto bar = press_a_bar(down_, event.position, Drag::ThumbDown); bar != ScrollBarPress::Missed) return true;
        if (const auto bar = press_a_bar(across_, event.position, Drag::ThumbAcross); bar != ScrollBarPress::Missed) return true;
        if (header_.contains(event.position)) {
            press_header(event.position);
        } else if (body_.contains(event.position)) {
            press_body(ui, event.position);
        }
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerRelease& event) {
        if (event.button != MouseButton::Left || drag_ == Drag::None) return false;
        const Drag ended = std::exchange(drag_, Drag::None);
        down_.release();
        across_.release();
        if (ended == Drag::HeaderPress && header_.contains(event.position) && !boundary_at(event.position) && column_at(event.position) == dragged_column_) sort_by_header(dragged_column_);
        // No motion will say what the pointer looks like where this ended. Outside this widget, that is not for it to say.
        if (bounds_.contains(event.position)) show_cursor(ui, cursor_at(event.position));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerMove& event) {
        if (drag_ != Drag::None) {
            follow_drag(event.position);
            return true;
        }
        if (event.captured) return false;
        const bool inside = bounds_.contains(event.position);
        if (inside) {
            show_cursor(ui, cursor_at(event.position));
        } else if (resize_cursor_shown_) {
            show_cursor(ui, CursorShape::Default);
        }
        return inside;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerScroll& event) {
        if (!bounds_.contains(event.position)) return false;
        // A thumb being held says where the offset is.
        if (drag_ == Drag::ThumbDown || drag_ == Drag::ThumbAcross) return true;
        const Height wheel_step = repeated_extent(row_height_, static_cast<std::size_t>(grid_wheel_rows));
        const bool moved_down = down_.scroll_by(event.down, wheel_step);
        const bool moved_across = across_.scroll_by(event.across, grid_sideways_step);
        if (moved_down || moved_across) damaged_ = true;
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!focused_ || !event.pressed) return false;
        if (event.keysym == Key::Left || event.keysym == Key::Right) {
            const ScrollAcross one_step = ScrollAcross::of_steps(event.keysym == Key::Left ? -1 : 1);
            if (across_.scroll_by(one_step, grid_sideways_step)) damaged_ = true;
            return true;
        }
        const std::optional<SelectionStep> step = selection_step_for_key(event.keysym);
        if (!step) return false;
        const std::optional<std::size_t> landed = stepped_selection(selected_position_, *step, rows_.size(), whole_units_in(down_.view(), row_height_));
        if (landed) select_position(ui, *landed);
        return true;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const PointerLeave&) {
        drag_ = Drag::None;
        down_.release();
        across_.release();
        resize_cursor_shown_ = false;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearFocus&) {
        focused_ = false;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const HoldsCapture&) {
        return drag_ != Drag::None;
    }

    // Focus is compared with what was painted, because a press in this grid takes it away and gives it back.
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const QueryDamage&) {
        return damaged_ || focused_ != focus_painted_;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearDamage&) {
        damaged_ = false;
        focus_painted_ = focused_;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const CollectDead&) {}

private:
    enum class Drag : std::uint8_t { None, ThumbDown, ThumbAcross, ColumnBoundary, HeaderPress };

    template <std::size_t I>
    using ColumnAt = std::tuple_element_t<I, std::tuple<Columns...>>;

    // Where the first column starts, as scrolled.
    [[nodiscard]] constexpr Left columns_origin() const noexcept { return (body_.left() + Width{}) - across_.offset(); }

    [[nodiscard]] constexpr std::optional<std::size_t> boundary_at(Point at) const noexcept {
        if (!header_.contains(at)) return std::nullopt;
        return boundary_near(std::span<const Width>{widths_}, columns_origin(), at.left(), grid_boundary_reach);
    }

    [[nodiscard]] constexpr std::optional<std::size_t> column_at(Point at) const noexcept {
        return span_index_at(std::span<const Width>{widths_}, columns_origin(), at.left());
    }

    [[nodiscard]] constexpr CursorShape cursor_at(Point at) const noexcept {
        return boundary_at(at) ? CursorShape::ResizeColumn : CursorShape::Default;
    }

    template <class U>
    constexpr void show_cursor(U& ui, CursorShape shape) {
        resize_cursor_shown_ = shape == CursorShape::ResizeColumn;
        ui.request_cursor(shape);
    }

    // Where each part goes, from the bounds and from how much there is to show. Anything that changes either comes here,
    // and this is what keeps both offsets clamped.
    constexpr void place_parts() noexcept {
        const Height header_height = std::min(row_height_, bounds_.height());
        const Size content{total_width(std::span<const Width>{widths_}), repeated_extent(row_height_, rows_.size())};
        const Size room{bounds_.width(), bounds_.height() - header_height};
        const ScrollBarsNeeded bars = scroll_bars_needed(content, room, theme::scroll_bar_across, theme::scroll_bar_down);
        const Width body_width = bars.down ? room.width() - theme::scroll_bar_across : room.width();
        const Height body_height = bars.across ? room.height() - theme::scroll_bar_down : room.height();
        header_ = Rect{bounds_.origin(), Size{body_width, header_height}};
        body_ = Rect{Point{bounds_.left(), top_after(header_.bottom())}, Size{body_width, body_height}};
        const Rect track_down = bars.down ? Rect{Point{left_after(body_.right()), body_.top()}, Size{bounds_.right() - left_after(body_.right()), body_height}} : Rect{};
        const Rect track_across = bars.across ? Rect{Point{body_.left(), top_after(body_.bottom())}, Size{body_width, bounds_.bottom() - top_after(body_.bottom())}} : Rect{};
        down_.place(track_down, content.height(), body_height);
        across_.place(track_across, content.width(), body_width);
    }

    template <class Bar>
    constexpr ScrollBarPress press_a_bar(Bar& bar, Point at, Drag drag) noexcept {
        const ScrollBarPress result = bar.press(at);
        if (result == ScrollBarPress::Grabbed) drag_ = drag;
        if (result == ScrollBarPress::Paged) damaged_ = true;
        return result;
    }

    constexpr void press_header(Point at) noexcept {
        if (const auto boundary = boundary_at(at); boundary) {
            drag_ = Drag::ColumnBoundary;
            dragged_column_ = *boundary;
            pressed_at_ = at.left();
            width_at_press_ = widths_[*boundary];
        } else if (const auto column = column_at(at); column) {
            drag_ = Drag::HeaderPress;
            dragged_column_ = *column;
        }
    }

    template <class U>
    constexpr void press_body(U& ui, Point at) {
        const Height into_view = DownAxis::distance_into(body_, at);
        if (const auto position = item_index_at(into_view + down_.offset(), row_height_, rows_.size()); position) select_position(ui, *position);
    }

    // The selection moves here and shows, and the message goes last, so a receiver finds the grid settled, free to give it new rows.
    template <class U>
    constexpr void select_position(U& ui, std::size_t position) {
        const bool changed = selected_position_ != position;
        selected_position_ = position;
        if (down_.reveal(repeated_extent(row_height_, position), row_height_)) damaged_ = true;
        if (!changed) return;
        damaged_ = true;
        if constexpr (!std::same_as<Notify, NoGridMessage>) {
            ui.template send<Notify>(GridRow{order_[position]});
        }
    }

    constexpr void follow_drag(Point at) noexcept {
        if (drag_ == Drag::ThumbDown && down_.drag_to(at)) damaged_ = true;
        if (drag_ == Drag::ThumbAcross && across_.drag_to(at)) damaged_ = true;
        if (drag_ != Drag::ColumnBoundary) return;
        const Width width = dragged_width(pressed_at_, width_at_press_, at.left(), grid_column_least, grid_column_most);
        if (!dragged_column_ || width == widths_[*dragged_column_]) return;
        widths_[*dragged_column_] = width;
        place_parts();
        damaged_ = true;
    }

    constexpr void sort_by_header(std::optional<std::size_t> column) {
        if (!column) return;
        const bool descending = sort_ && sort_->column == *column && !sort_->descending;
        const std::optional<std::uint32_t> kept = selected_position_ ? std::optional<std::uint32_t>{order_[*selected_position_]} : std::nullopt;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((I == *column ? sort_rows<ColumnAt<I>>(descending) : void()), ...);
        }(std::index_sequence_for<Columns...>{});
        sort_ = GridSort{*column, descending};
        if (kept) selected_position_ = position_of_index(order_, *kept);
        damaged_ = true;
    }

    // Always from the rows' own order, so rows that compare equal stay in that order in both directions.
    template <class C>
    constexpr void sort_rows(bool descending) {
        order_ = identity_order(static_cast<std::uint32_t>(rows_.size()));
        const std::span<const Row> rows{rows_};
        stable_sort_indices(order_, [rows, descending](std::uint32_t a, std::uint32_t b) {
            const auto& first = C::value_of(rows[a]);
            const auto& second = C::value_of(rows[b]);
            return descending ? second < first : first < second;
        });
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_headers(const Shaper& text) {
        for (detail::ShapedCaption& header : headers_)
            if (auto shaped = header.shape_with(text); !shaped) return shaped;
        return {};
    }

    // Only what shows: the rows in view, and of those the columns in view.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_visible_cells(const Shaper& text) {
        visible_ = visible_item_range(down_.offset(), body_.height(), row_height_, rows_.size());
        cells_.clear();
        cells_.resize(visible_.count * column_count);
        cells_shaped_ = 0;
        std::expected<void, Error> result{};
        Left start = columns_origin();
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            const auto shape_one_column = [&]<std::size_t K>(std::integral_constant<std::size_t, K>) {
                const Right end = start + widths_[K];
                const bool shows = left_after(end) > body_.left() && start < left_after(body_.right());
                if (result && shows) result = shape_column<K>(text);
                start = left_after(end);
            };
            (shape_one_column(std::integral_constant<std::size_t, I>{}), ...);
        }(std::index_sequence_for<Columns...>{});
        return result;
    }

    template <std::size_t K, TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_column(const Shaper& text) {
        using C = ColumnAt<K>;
        const std::size_t most_bytes = detail::cell_bytes_worth_shaping(widths_[K]);
        for (std::size_t r = 0; r < visible_.count; ++r) {
            const auto& cell = CellFormat<typename C::Value>::text(C::value_of(rows_[order_[visible_.first + r]]));
            const std::string_view whole{cell};
            if (whole.empty()) continue;
            auto shaped = text.shape(utf8_prefix(whole, most_bytes));
            ++cells_shaped_;
            if (shaped) {
                cells_[r * column_count + K] = std::move(*shaped);
            } else if (!detail::refuses_the_text(shaped.error().code)) {
                return std::unexpected(std::move(shaped.error()));
            }
        }
        return {};
    }

    // Reads only what layout shaped, never the rows.
    constexpr void paint_body(const Canvas& body) const {
        constexpr Height one_down{1};
        constexpr Width one_across{1};
        for (std::size_t r = 0; r < visible_.count; ++r) {
            const std::size_t position = visible_.first + r;
            const Top top = (body_.top() + repeated_extent(row_height_, position)) - down_.offset();
            const Rect row{Point{body_.left(), top}, Size{body_.width(), row_height_}};
            if (selected_position_ == position) body.fill_rect(row, theme::button_pressed);
            Left start = columns_origin();
            for (std::size_t k = 0; k < column_count; ++k) {
                const Right end = start + widths_[k];
                if (const auto& run = cells_[r * column_count + k]; run) {
                    const Canvas cell = body.clipped(Rect{Point{start, top}, Size{widths_[k], row_height_}});
                    cell.draw_text(Point{left_after(start + theme::cell_pad_x), top_after(top + theme::cell_pad_y)}, *run, theme::text);
                }
                start = left_after(end);
            }
            body.fill_rect(Rect{Point{body_.left(), row.bottom() - one_down}, Size{body_.width(), one_down}}, theme::bar_edge);
        }
        Left start = columns_origin();
        for (const Width width : widths_) {
            const Right end = start + width;
            body.fill_rect(Rect{Point{end - one_across, body_.top()}, Size{one_across, body_.height()}}, theme::bar_edge);
            start = left_after(end);
        }
    }

    constexpr void paint_header(const Canvas& local) const {
        constexpr Height one_down{1};
        constexpr Width one_across{1};
        // The strip goes the whole way across, so nothing is left blank above a scroll bar.
        const Rect strip{bounds_.origin(), Size{bounds_.width(), header_.height()}};
        local.fill_rect(strip, theme::bar);
        const Canvas head = local.clipped(header_);
        Left start = columns_origin();
        for (std::size_t k = 0; k < column_count; ++k) {
            const Right end = start + widths_[k];
            const Rect cell_area{Point{start, header_.top()}, Size{widths_[k], header_.height()}};
            const Canvas cell = head.clipped(cell_area);
            if (const auto& run = headers_[k].run(); run) cell.draw_text(Point{left_after(start + theme::cell_pad_x), top_after(header_.top() + theme::cell_pad_y)}, *run, theme::text);
            if (sort_ && sort_->column == k) paint_sort_mark(cell, cell_area, sort_->descending);
            head.fill_rect(Rect{Point{end - one_across, header_.top()}, Size{one_across, header_.height()}}, theme::bar_edge);
            start = left_after(end);
        }
        local.fill_rect(Rect{Point{strip.left(), strip.bottom() - one_down}, Size{strip.width(), one_down}}, theme::bar_edge);
    }

    // A small solid triangle at the far end of the header: its point up for ascending, down for descending.
    static constexpr void paint_sort_mark(const Canvas& cell, const Rect& area, bool descending) {
        constexpr std::int32_t rows = 5;
        const Left mark_left = area.right() - grid_sort_mark_room;
        const Top mark_top = top_after(area.top() + theme::cell_pad_y);
        for (std::int32_t row = 0; row < rows; ++row) {
            const std::int32_t half = descending ? rows - 1 - row : row;
            const Left left = (left_after(mark_left + Width{rows - 1}) + Width{}) - Width{half};
            const Top top = top_after(mark_top + Height{row});
            cell.fill_rect(Rect{Point{left, top}, Size{Width{2 * half + 1}, Height{1}}}, theme::text);
        }
    }

    std::vector<Row> rows_{};
    std::vector<std::uint32_t> order_{};
    std::array<detail::ShapedCaption, column_count> headers_;
    std::array<Width, column_count> widths_;
    // What layout shaped for the rows in view, by row and then column. Bounded by the window's height and the column count.
    std::vector<std::optional<TextRun>> cells_{};
    ItemRange visible_{};
    std::size_t cells_shaped_{};
    ScrollBar<DownAxis> down_{};
    ScrollBar<AcrossAxis> across_{};
    Rect bounds_{};
    Rect header_{};
    Rect body_{};
    Height row_height_{};
    std::optional<std::size_t> selected_position_{};
    std::optional<GridSort> sort_{};
    Drag drag_{Drag::None};
    std::optional<std::size_t> dragged_column_{};
    Left pressed_at_{};
    Width width_at_press_{};
    bool focused_{};
    bool focus_painted_{};
    bool resize_cursor_shown_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
