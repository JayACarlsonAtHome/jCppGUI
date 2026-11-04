// File: include/jCppGUI/v001/datagrid.hpp
// Purpose: DataGrid<Tag, Row, Notify, Columns...>: a read-only table of rows the program gives it. Each column is its own type,
//          naming a member of Row, and its text comes from that member's CellFormat. Only rows that show are shaped and
//          painted, so what a frame costs does not depend on how many rows there are. The general parts are in scroll_bar.hpp,
//          spans.hpp, selection_step.hpp, index_sort.hpp and cell_text.hpp, and this holds only what is a grid's own.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
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
#include "panels.hpp"
#include "scroll_bar.hpp"
#include "selection_step.hpp"
#include "spans.hpp"
#include "text.hpp"
#include "timer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t max_grid_rows = 1'000'000;
inline constexpr std::int32_t max_grid_row_height = 2048;
inline constexpr std::int32_t grid_column_default = 120;
inline constexpr Width grid_column_most{4096};
inline constexpr Width grid_boundary_reach{4};
inline constexpr std::int32_t grid_wheel_rows = 3;
inline constexpr Width grid_sideways_step{48};
inline constexpr std::size_t grid_most_sort_keys = 9;
inline constexpr Width grid_sort_mark_gap{3};  // between the sort mark and the font button
inline constexpr std::size_t grid_cell_most_bytes = 1024;
inline constexpr Width grid_control_letter_pad{2};  // each side of the widest letter in a header button: its outline and one pixel
inline constexpr Width grid_controls_gap{1};
// How long after the last press on a column's header button the column itself changes. The button's letter changes at once.
inline constexpr std::chrono::milliseconds grid_column_change_delay{300};

// Every row, at the tallest a row can be, and any window under it, stays inside what a length can hold.
static_assert(std::int64_t{max_grid_row_height} * std::int64_t{max_grid_rows} + 2 * max_buffer_side < std::int64_t{std::numeric_limits<std::int32_t>::max()});

// A row in the order the program gave them, whatever order they show in.
struct GridRow {
    std::size_t index{};
    friend constexpr bool operator==(const GridRow&, const GridRow&) = default;
};

// One column the rows are sorted by, and which way.
struct GridSortKey {
    std::size_t column{};
    bool descending{};
    friend constexpr bool operator==(const GridSortKey&, const GridSortKey&) = default;
};

// What font a column's title and cells are drawn in. A shaper with no monospace face refuses that one, and the column stays regular.
enum class ColumnFont : std::uint8_t { Regular, Monospace };

// Where in its column a column's title and cells sit.
enum class ColumnAlign : std::uint8_t { AtLeft, AtCenter, AtRight };

[[nodiscard]] constexpr ColumnAlign next_column_align(ColumnAlign align) noexcept {
    switch (align) {
        case ColumnAlign::AtLeft: return ColumnAlign::AtCenter;
        case ColumnAlign::AtCenter: return ColumnAlign::AtRight;
        case ColumnAlign::AtRight:
        default: return ColumnAlign::AtLeft;
    }
}

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
    [[nodiscard]] static consteval Width start_width() { return std::min(Width{StartWidth}, grid_column_most); }
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

// Where text this wide starts in a room this wide. Text too wide for the room starts where the room does, whatever the alignment, so its start shows.
[[nodiscard]] constexpr Left aligned_left(Left from, Width room, Width text, ColumnAlign align) noexcept {
    const Width spare = room - text;
    switch (align) {
        case ColumnAlign::AtCenter: return left_after(from + smaller_half_of(spare));
        case ColumnAlign::AtRight: return left_after(from + spare);
        case ColumnAlign::AtLeft:
        default: return from;
    }
}

// Where something this high goes to sit in the middle of a room this high.
[[nodiscard]] constexpr Top centred_top(Top from, Height room, Height content) noexcept { return top_after(from + smaller_half_of(room - content)); }

// The boundary's reach inside its own column, which ends with the pixel before the next column. The buttons stop short of it, so a boundary drag always starts.
inline constexpr Width grid_boundary_reach_inside = grid_boundary_reach - Width{1};

// Both of a header's buttons, side by side.
[[nodiscard]] constexpr Width controls_width(Width button) noexcept { return button + grid_controls_gap + button; }

// The sort mark is a triangle almost the header's height, a pixel off each end, and it widens by a pixel each side every second row.
[[nodiscard]] constexpr std::int32_t sort_mark_rows(Height header) noexcept {
    return saturate_to<std::int32_t>(whole_units_in(header - Height{2}, Height{1}));
}
[[nodiscard]] constexpr std::int32_t sort_mark_half_at(std::int32_t row) noexcept { return row / 2; }
[[nodiscard]] constexpr Width sort_mark_width(Height header) noexcept {
    const std::int32_t rows = sort_mark_rows(header);
    return rows == 0 ? Width{} : Width{2 * sort_mark_half_at(rows - 1) + 1};
}
// What the header's right end keeps free for the mark, sorted or not.
[[nodiscard]] constexpr Width sort_mark_room(Height header) noexcept { return sort_mark_width(header) + grid_sort_mark_gap; }

// The priority inside the mark, 5 across and 7 down, drawn here rather than shaped: a glyph's height and width are the font's to say, and this always fits the shape, with no
// blur to fight the mark's colour. Only a mark of this many rows has room for it, in rows wide enough at its fat end.
inline constexpr std::int32_t sort_digit_rows = 7;
inline constexpr std::int32_t sort_digit_columns = 5;
inline constexpr std::int32_t sort_mark_rows_for_a_digit = 14;
inline constexpr std::array<std::array<std::uint8_t, sort_digit_rows>, grid_most_sort_keys> sort_digits{{
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    {0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111},
    {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110},
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
}};

// A column that shows its whole title: padding, the title, padding, the sort mark's room, both buttons and the boundary's reach.
[[nodiscard]] constexpr Width whole_title_width(Width title, Width button, Height header) noexcept {
    return theme::cell_pad_x + title + theme::cell_pad_x + sort_mark_room(header) + controls_width(button) + grid_boundary_reach_inside;
}

// The narrowest a drag can make a column: padding, one character, both buttons and the boundary's reach.
[[nodiscard]] constexpr Width drag_floor_width(Width one_character, Width button) noexcept {
    return std::min(theme::cell_pad_x + one_character + controls_width(button) + grid_boundary_reach_inside, grid_column_most);
}

// A header's two buttons, font then alignment, at the right end of the column's header, almost its full height: a pixel above, and the header's line below.
struct ColumnButtons {
    Rect font{};
    Rect align{};
};
[[nodiscard]] constexpr ColumnButtons column_buttons_in(const Rect& cell, Width button) noexcept {
    constexpr Height off_the_edge{1};
    const Top top = top_after(cell.top() + off_the_edge);
    const Size size{button, cell.height() - off_the_edge - off_the_edge};
    const Left align_left = cell.right() - (grid_boundary_reach_inside + button);
    const Left font_left = (align_left + Width{}) - (grid_controls_gap + button);
    return ColumnButtons{Rect{Point{font_left, top}, size}, Rect{Point{align_left, top}, size}};
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

    constexpr DataGrid()
        : headers_{detail::ShapedCaption{Columns::title()}...}, monospace_headers_{detail::ShapedCaption{Columns::title()}...}, widths_{Columns::start_width()...}, shown_{Columns::start_width()...} {}
    DataGrid(const DataGrid&) = delete;
    DataGrid& operator=(const DataGrid&) = delete;
    constexpr DataGrid(DataGrid&&) = default;
    DataGrid& operator=(DataGrid&&) = delete;
    constexpr ~DataGrid() = default;

    // Takes the rows, in the order they first show. The selection and the sort do not carry over to new rows, and
    // no message says so, because the caller knows. The current column and each column's settings stay. More than the limit is refused, and the old rows stay.
    [[nodiscard]] constexpr std::expected<void, Error> set_rows(std::vector<Row> rows) {
        if (rows.size() > max_grid_rows) return make_error(ErrorCode::SizeLimit, "a grid holds at most 1000000 rows");
        rows_ = std::move(rows);
        order_ = identity_order(static_cast<std::uint32_t>(rows_.size()));
        selected_position_.reset();
        chosen_.clear();
        anchor_row_.reset();
        narrow_to_row_.reset();
        sort_count_ = 0;
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
    // Every selected row, in the rows' own order. selected() is the current one among them.
    [[nodiscard]] constexpr std::span<const std::uint32_t> selected_rows() const noexcept { return chosen_; }
    // What the rows are sorted by, most important first. A key's place here is the number in its mark, less one.
    [[nodiscard]] constexpr std::span<const GridSortKey> sort_keys() const noexcept { return std::span<const GridSortKey>{sort_keys_}.first(sort_count_); }
    // The column the keys act on, from a click on its header.
    [[nodiscard]] constexpr std::optional<std::size_t> current_column() const noexcept { return current_column_; }
    // The row under this point, as a press there would select it. Empty anywhere else.
    [[nodiscard]] constexpr std::optional<GridRow> row_at(Point at) const noexcept {
        if (!body_.contains(at)) return std::nullopt;
        const auto position = item_index_at(DownAxis::distance_into(body_, at) + down_.offset(), row_height_, rows_.size());
        if (!position) return std::nullopt;
        return GridRow{order_[*position]};
    }
    // Where a column starts now, as scrolled.
    [[nodiscard]] constexpr Left column_left(std::size_t column) const { return header_cell(column).left(); }

    // Selects this row and brings it into view, or selects nothing, which a row that is not there also means. No message goes, because the caller knows.
    constexpr void select_row(std::optional<GridRow> row) {
        const std::optional<std::size_t> position = row && row->index < order_.size() ? position_of_index(order_, static_cast<std::uint32_t>(row->index)) : std::nullopt;
        if (position != selected_position_ || chosen_.size() > 1) damaged_ = true;
        selected_position_ = position;
        chosen_.clear();
        anchor_row_.reset();
        if (position) {
            chosen_.push_back(order_[*position]);
            anchor_row_ = order_[*position];
        }
        narrow_to_row_.reset();
        if (position && down_.reveal(repeated_extent(row_height_, *position), row_height_)) damaged_ = true;
    }

    // Selects these rows, with this one current and in view. Rows not there are left out, and one current that is not among them gives way to the first. No message goes.
    constexpr void select_rows(std::vector<std::uint32_t> rows, std::optional<GridRow> current) {
        std::erase_if(rows, [&](std::uint32_t row) { return row >= order_.size(); });
        std::ranges::sort(rows);
        rows.erase(std::ranges::unique(rows).begin(), rows.end());
        if (rows.empty()) {
            select_row(std::nullopt);
            return;
        }
        const bool current_is_there = current && current->index < order_.size() && std::ranges::binary_search(rows, static_cast<std::uint32_t>(current->index));
        const std::uint32_t row = current_is_there ? static_cast<std::uint32_t>(current->index) : rows.front();
        select_row(GridRow{row});
        chosen_ = std::move(rows);
        damaged_ = true;
    }

    // Every row, with the current one staying if there is one, and otherwise the first. It does not scroll, and no message goes.
    constexpr void select_all_rows() {
        if (rows_.empty()) return;
        chosen_ = identity_order(static_cast<std::uint32_t>(rows_.size()));
        if (!selected_position_) selected_position_ = 0;
        if (!anchor_row_) anchor_row_ = order_[*selected_position_];
        damaged_ = true;
    }

    // What is under way counts as done with several rows, so letting go leaves them all selected.
    constexpr void keep_selection_on_release() noexcept { narrow_to_row_.reset(); }

    // Sorts by these, most important first, or by the rows' own order with none. More than the limit, a column that is not there and a column twice are refused, and the sort stays as it was.
    [[nodiscard]] constexpr std::expected<void, Error> set_sort_keys(std::span<const GridSortKey> keys) {
        if (keys.size() > grid_most_sort_keys) return make_error(ErrorCode::SizeLimit, "a grid sorts by at most 9 columns");
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].column >= column_count) return make_error(ErrorCode::SizeLimit, "the grid has no column at that position");
            for (std::size_t k = 0; k < i; ++k)
                if (keys[k].column == keys[i].column) return make_error(ErrorCode::SizeLimit, "a column can be only one of a sort's keys");
        }
        std::ranges::copy(keys, sort_keys_.begin());
        sort_count_ = keys.size();
        apply_sort();
        return {};
    }
    [[nodiscard]] constexpr Height scroll_down() const noexcept { return down_.offset(); }
    [[nodiscard]] constexpr Width scroll_across() const noexcept { return across_.offset(); }
    [[nodiscard]] constexpr Width column_width(std::size_t column) const { return shown_.at(column); }
    [[nodiscard]] constexpr Height row_height() const noexcept { return row_height_; }
    [[nodiscard]] constexpr bool focused() const noexcept { return focused_; }
    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr Rect header_area() const noexcept { return header_; }
    [[nodiscard]] constexpr Rect body_area() const noexcept { return body_; }
    [[nodiscard]] constexpr Rect thumb_down() const noexcept { return down_.thumb(); }
    [[nodiscard]] constexpr Rect thumb_across() const noexcept { return across_.thumb(); }
    // How many cells the last layout shaped: what a frame's text cost goes with.
    [[nodiscard]] constexpr std::size_t cells_shaped() const noexcept { return cells_shaped_; }

    // What a column's buttons show, which is what was last chosen, and what its cells and title show, which follows after the wait.
    [[nodiscard]] constexpr ColumnFont column_font(std::size_t column) const { return fonts_.at(column).chosen(); }
    [[nodiscard]] constexpr ColumnAlign column_align(std::size_t column) const { return aligns_.at(column).chosen(); }
    [[nodiscard]] constexpr ColumnFont column_font_applied(std::size_t column) const { return fonts_.at(column).applied(); }
    [[nodiscard]] constexpr ColumnAlign column_align_applied(std::size_t column) const { return aligns_.at(column).applied(); }
    // How many times a press has really changed how a column looks, after its wait.
    [[nodiscard]] constexpr std::size_t column_changes() const noexcept { return column_changes_; }
    [[nodiscard]] constexpr std::string_view column_title(std::size_t column) const { return headers_.at(column).text(); }
    [[nodiscard]] constexpr Rect font_button(std::size_t column) const { return column_buttons(column).font; }
    [[nodiscard]] constexpr Rect align_button(std::size_t column) const { return column_buttons(column).align; }
    [[nodiscard]] constexpr bool every_column_monospace() const noexcept {
        return std::ranges::all_of(fonts_, [](const DelayedChoice<ColumnFont>& font) { return font.chosen() == ColumnFont::Monospace; });
    }

    // These take effect at once, with no wait, and end any wait that column's button had started. A column that is not there is refused.
    [[nodiscard]] constexpr std::expected<void, Error> set_column_font(std::size_t column, ColumnFont font) {
        if (column >= column_count) return make_error(ErrorCode::SizeLimit, "the grid has no column at that position");
        fonts_[column].set_now(font);
        damaged_ = true;
        return {};
    }
    [[nodiscard]] constexpr std::expected<void, Error> set_column_align(std::size_t column, ColumnAlign align) {
        if (column >= column_count) return make_error(ErrorCode::SizeLimit, "the grid has no column at that position");
        aligns_[column].set_now(align);
        damaged_ = true;
        return {};
    }
    constexpr void set_every_column_font(ColumnFont font) noexcept {
        for (DelayedChoice<ColumnFont>& each : fonts_) each.set_now(font);
        damaged_ = true;
    }

    // Another title for a column, in place of the one it was declared with or last given. A column no one has dragged follows it, wider or narrower,
    // so its title stays whole. A column that is not there is refused.
    [[nodiscard]] constexpr std::expected<void, Error> set_column_title(std::size_t column, CheckedText title) {
        if (column >= column_count) return make_error(ErrorCode::SizeLimit, "the grid has no column at that position");
        headers_[column].replace(title);
        monospace_headers_[column].replace(title);
        damaged_ = true;
        return {};
    }

    // Why a column asked for monospace and stayed regular, once for each time that happened, for the program to show. Empty otherwise.
    [[nodiscard]] constexpr std::optional<Error> take_font_refusal() noexcept { return std::exchange(font_refusal_, std::nullopt); }

    // Takes all that is offered and leaves nothing below.
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper& text) {
        bounds_ = area;
        inner_ = area.inset(theme::grid_border_across, theme::grid_border_down);
        row_height_ = std::clamp(tallest_line_in_use(text) + theme::cell_pad_y + theme::cell_pad_y, Height{1}, Height{max_grid_row_height});
        // The titles first, because the columns' widths come from them.
        if (auto shaped = shape_headers(text); !shaped) return std::unexpected(std::move(shaped.error()));
        place_parts();
        if (auto shaped = shape_visible_cells(text); !shaped) return std::unexpected(std::move(shaped.error()));
        return Rect{Point{area.left(), top_after(area.bottom())}, Size{area.width(), Height{}}};
    }

    constexpr void paint(const TreeKey&, Canvas& canvas) {
        const Canvas whole = canvas.clipped(bounds_);
        paint_border(whole);
        const Canvas local = whole.clipped(inner_);
        local.fill_rect(inner_, theme::background);
        paint_body(local.clipped(body_));
        paint_header(local);
        paint_filler(local);
        down_.paint(local);
        across_.paint(local);
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
            press_body(ui, event.position, event.modifiers);
        }
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerRelease& event) {
        // A plain press on one of several selected rows, let go with no drag, was a click on that row.
        if (event.button == MouseButton::Left && narrow_to_row_) {
            const std::uint32_t row = *std::exchange(narrow_to_row_, std::nullopt);
            if (const std::optional<std::size_t> position = position_of_index(order_, row); position) select_position(ui, *position);
            return true;
        }
        if (event.button != MouseButton::Left || drag_ == Drag::None) return false;
        const Drag ended = std::exchange(drag_, Drag::None);
        down_.release();
        across_.release();
        if (ended == Drag::HeaderPress && header_.contains(event.position) && !boundary_at(event.position) && column_at(event.position) == dragged_column_) make_current(dragged_column_);
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
        // These arrows with Ctrl are the current column's sort, and never what they do without it, even when they end up doing nothing.
        if (event.modifiers.holds(Modifier::Ctrl)) {
            if (event.keysym == Key::Up || event.keysym == Key::Down) {
                sort_current_column(event.keysym == Key::Down);
                return true;
            }
            if (event.keysym == Key::Left || event.keysym == Key::Right) {
                clear_current_column_sort();
                return true;
            }
        }
        if (event.keysym == Key::Left || event.keysym == Key::Right) {
            const ScrollAcross one_step = ScrollAcross::of_steps(event.keysym == Key::Left ? -1 : 1);
            if (across_.scroll_by(one_step, grid_sideways_step)) damaged_ = true;
            return true;
        }
        const std::optional<SelectionStep> step = selection_step_for_key(event.keysym);
        if (!step) return false;
        const std::optional<std::size_t> landed = stepped_selection(selected_position_, *step, rows_.size(), whole_units_in(down_.view(), row_height_));
        if (landed) {
            if (event.modifiers.holds(Modifier::Shift)) {
                extend_to_position(ui, *landed);
            } else {
                select_position(ui, *landed);
            }
        }
        return true;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const PointerLeave&) {
        narrow_to_row_.reset();
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
        return drag_ != Drag::None || narrow_to_row_.has_value();
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

    // A grid not showing is told too, so what a press started ends on time, and what it changed shows when the grid does.
    template <class U>
    constexpr void notify(const TreeKey&, U& ui, const TimersDue& timers) {
        for (std::size_t k = 0; k < column_count; ++k) {
            if (fonts_[k].step(timers, grid_column_change_delay, ui)) note_column_change();
            if (aligns_[k].step(timers, grid_column_change_delay, ui)) note_column_change();
        }
    }

private:
    enum class Drag : std::uint8_t { None, ThumbDown, ThumbAcross, ColumnBoundary, HeaderPress };

    template <std::size_t I>
    using ColumnAt = std::tuple_element_t<I, std::tuple<Columns...>>;

    // Where the first column starts, as scrolled.
    [[nodiscard]] constexpr Left columns_origin() const noexcept { return (body_.left() + Width{}) - across_.offset(); }

    [[nodiscard]] constexpr std::optional<std::size_t> boundary_at(Point at) const noexcept {
        if (!header_.contains(at)) return std::nullopt;
        return boundary_near(std::span<const Width>{shown_}, columns_origin(), at.left(), grid_boundary_reach);
    }

    [[nodiscard]] constexpr std::optional<std::size_t> column_at(Point at) const noexcept {
        return span_index_at(std::span<const Width>{shown_}, columns_origin(), at.left());
    }

    // A column's place in the header.
    [[nodiscard]] constexpr Rect header_cell(std::size_t column) const {
        const Left start = left_after(columns_origin() + total_width(std::span<const Width>{shown_}.first(column)));
        return Rect{Point{start, header_.top()}, Size{shown_.at(column), header_.height()}};
    }

    [[nodiscard]] constexpr detail::ColumnButtons column_buttons(std::size_t column) const { return detail::column_buttons_in(header_cell(column), button_width_); }

    // The whole height of the header counts as a button, and so does the gap between them, so a press that just misses one does not sort.
    constexpr bool press_button_at(Point at) {
        const auto column = column_at(at);
        if (!column) return false;
        const detail::ColumnButtons buttons = column_buttons(*column);
        if (at.left() < buttons.font.left() || at.left() >= left_after(buttons.align.right())) return false;
        if (at.left() < buttons.align.left()) {
            DelayedChoice<ColumnFont>& font = fonts_[*column];
            font.choose(font.chosen() == ColumnFont::Monospace ? ColumnFont::Regular : ColumnFont::Monospace);
        } else {
            DelayedChoice<ColumnAlign>& align = aligns_[*column];
            align.choose(next_column_align(align.chosen()));
        }
        damaged_ = true;
        return true;
    }

    constexpr void note_column_change() noexcept {
        ++column_changes_;
        damaged_ = true;
    }

    // The tallest font any column has in effect, which all rows are sized to. This is where monospace is first asked for, and where a refusal puts the columns back.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr Height tallest_line_in_use(const Shaper& text) {
        if (std::ranges::none_of(fonts_, [](const DelayedChoice<ColumnFont>& font) { return font.applied() == ColumnFont::Monospace; })) return text.line_height();
        if constexpr (HasMonospaceFace<Shaper>) {
            auto face = text.monospace_face();
            if (face) return std::max(text.line_height(), face->get().line_height());
            refuse_monospace(std::move(face.error()));
        } else {
            refuse_monospace(Error{ErrorCode::Font, "this shaper has no monospace face"});
        }
        return text.line_height();
    }

    constexpr void refuse_monospace(Error why) {
        for (DelayedChoice<ColumnFont>& font : fonts_)
            if (font.applied() == ColumnFont::Monospace) font.set_now(ColumnFont::Regular);
        font_refusal_ = std::move(why);
    }

    // How wide a column shows before any room left over: until it is dragged at least its whole title, and after that what the drag left it, at least the floor.
    [[nodiscard]] constexpr Width wanted_width(std::size_t column) const noexcept {
        const Width least = dragged_[column] ? drag_floor_ : whole_titles_[column];
        return std::min(std::max(widths_[column], least), grid_column_most);
    }

    [[nodiscard]] constexpr const detail::ShapedCaption& title_shown(std::size_t column) const noexcept {
        return fonts_[column].applied() == ColumnFont::Monospace ? monospace_headers_[column] : headers_[column];
    }

    [[nodiscard]] constexpr CursorShape cursor_at(Point at) const noexcept {
        return boundary_at(at) ? CursorShape::ResizeColumn : CursorShape::Default;
    }

    template <class U>
    constexpr void show_cursor(U& ui, CursorShape shape) {
        resize_cursor_shown_ = shape == CursorShape::ResizeColumn;
        ui.request_cursor(shape);
    }

    // Where each part goes, inside the border, from the bounds and from how much there is to show. Anything that changes either comes here,
    // and this is what keeps both offsets clamped.
    constexpr void place_parts() noexcept {
        const Height header_height = std::min(row_height_, inner_.height());
        for (std::size_t k = 0; k < column_count; ++k) shown_[k] = wanted_width(k);
        const Size content{total_width(std::span<const Width>{shown_}), repeated_extent(row_height_, rows_.size())};
        const Size room{inner_.width(), inner_.height() - header_height};
        const ScrollBarsNeeded bars = scroll_bars_needed(content, room, theme::scroll_bar_across, theme::scroll_bar_down);
        const Width body_width = bars.down ? room.width() - theme::scroll_bar_across : room.width();
        const Height body_height = bars.across ? room.height() - theme::scroll_bar_down : room.height();
        header_ = Rect{inner_.origin(), Size{body_width, header_height}};
        body_ = Rect{Point{inner_.left(), top_after(header_.bottom())}, Size{body_width, body_height}};
        const Rect track_down = bars.down ? Rect{Point{left_after(body_.right()), body_.top()}, Size{inner_.right() - left_after(body_.right()), body_height}} : Rect{};
        const Rect track_across = bars.across ? Rect{Point{body_.left(), top_after(body_.bottom())}, Size{body_width, inner_.bottom() - top_after(body_.bottom())}} : Rect{};
        down_.place(track_down, content.height(), body_height);
        across_.place(track_across, content.width(), body_width);
        // The columns end at the grid. The last one takes a little room left over, and a lot is a plain filler, which is no column: its header says so
        // and takes no click or drag. The last column never shows narrower than it was set, so no text is cut for this.
        filler_ = Rect{};
        filler_header_ = Rect{};
        if (content.width() < body_width) {
            const Width spare = body_width - content.width();
            if (spare < shown_.back()) {
                shown_.back() = shown_.back() + spare;
            } else {
                const Left filler_left = left_after(body_.left() + content.width());
                filler_ = Rect{Point{filler_left, body_.top()}, Size{spare, body_height}};
                filler_header_ = Rect{Point{filler_left, header_.top()}, Size{spare, header_height}};
            }
        }
    }

    // Four strips around the contents. Focus darkens the line touching them, so the soft colour always shows.
    constexpr void paint_border(const Canvas& whole) const {
        const Width across = theme::grid_border_across;
        const Height down = theme::grid_border_down;
        whole.fill_rect(Rect{bounds_.origin(), Size{bounds_.width(), down}}, theme::grid_border);
        whole.fill_rect(Rect{Point{bounds_.left(), bounds_.bottom() - down}, Size{bounds_.width(), down}}, theme::grid_border);
        whole.fill_rect(Rect{Point{bounds_.left(), inner_.top()}, Size{across, inner_.height()}}, theme::grid_border);
        whole.fill_rect(Rect{Point{bounds_.right() - across, inner_.top()}, Size{across, inner_.height()}}, theme::grid_border);
        if (focused_) whole.stroke_rect(bounds_.inset(across - Width{1}, down - Height{1}), theme::focus_edge);
    }

    template <class Bar>
    constexpr ScrollBarPress press_a_bar(Bar& bar, Point at, Drag drag) noexcept {
        const ScrollBarPress result = bar.press(at);
        if (result == ScrollBarPress::Grabbed) drag_ = drag;
        if (result == ScrollBarPress::Paged) damaged_ = true;
        return result;
    }

    constexpr void press_header(Point at) {
        if (const auto boundary = boundary_at(at); boundary) {
            drag_ = Drag::ColumnBoundary;
            dragged_column_ = *boundary;
            pressed_at_ = at.left();
            width_at_press_ = shown_[*boundary];
        } else if (press_button_at(at)) {
            return;
        } else if (const auto column = column_at(at); column) {
            drag_ = Drag::HeaderPress;
            dragged_column_ = *column;
        }
    }

    template <class U>
    constexpr void press_body(U& ui, Point at, ModifierMask modifiers) {
        const Height into_view = DownAxis::distance_into(body_, at);
        const auto position = item_index_at(into_view + down_.offset(), row_height_, rows_.size());
        if (!position) return;
        if (modifiers.holds(Modifier::Shift)) {
            extend_to_position(ui, *position);
            return;
        }
        if (modifiers.holds(Modifier::Ctrl)) {
            toggle_position(ui, *position);
            return;
        }
        // On one of several selected rows it may be the start of something done to all of them, so they stay until it is let go.
        if (chosen_.size() > 1 && row_is_chosen(order_[*position])) {
            narrow_to_row_ = order_[*position];
            anchor_row_ = order_[*position];
            selected_position_ = *position;
            damaged_ = true;
            return;
        }
        select_position(ui, *position);
    }

    [[nodiscard]] constexpr bool row_is_chosen(std::uint32_t row) const { return std::ranges::binary_search(chosen_, row); }

    // Every row from the last one picked to this one, in the order they show, and nothing else.
    template <class U>
    constexpr void extend_to_position(U& ui, std::size_t position) {
        if (!anchor_row_) {
            select_position(ui, position);
            return;
        }
        const std::size_t anchor = position_of_index(order_, *anchor_row_).value_or(position);
        const std::size_t first = std::min(anchor, position);
        const std::size_t last = std::max(anchor, position);
        chosen_.assign(order_.begin() + static_cast<std::ptrdiff_t>(first), order_.begin() + static_cast<std::ptrdiff_t>(last) + 1);
        std::ranges::sort(chosen_);
        selected_position_ = position;
        static_cast<void>(down_.reveal(repeated_extent(row_height_, position), row_height_));
        damaged_ = true;
        send_selected(ui);
    }

    // In or out of the selection, leaving the rest. Taking out the current row makes the first one left current.
    template <class U>
    constexpr void toggle_position(U& ui, std::size_t position) {
        const std::uint32_t row = order_[position];
        const auto at = std::ranges::lower_bound(chosen_, row);
        damaged_ = true;
        anchor_row_ = row;
        if (at != chosen_.end() && *at == row) {
            chosen_.erase(at);
            selected_position_ = chosen_.empty() ? std::nullopt : position_of_index(order_, chosen_.front());
            if (selected_position_) send_selected(ui);
            return;
        }
        chosen_.insert(at, row);
        selected_position_ = position;
        static_cast<void>(down_.reveal(repeated_extent(row_height_, position), row_height_));
        send_selected(ui);
    }

    template <class U>
    constexpr void send_selected(U& ui) {
        if constexpr (!std::same_as<Notify, NoGridMessage>) {
            if (selected_position_) ui.template send<Notify>(GridRow{order_[*selected_position_]});
        }
    }

    // The selection moves here and shows, and the message goes last, so a receiver finds the grid settled, free to give it new rows.
    template <class U>
    constexpr void select_position(U& ui, std::size_t position) {
        const bool changed = selected_position_ != position || chosen_.size() > 1;
        selected_position_ = position;
        chosen_.assign(1, order_[position]);
        anchor_row_ = order_[position];
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
        const Width width = dragged_width(pressed_at_, width_at_press_, at.left(), drag_floor_, grid_column_most);
        if (!dragged_column_ || width == wanted_width(*dragged_column_)) return;
        widths_[*dragged_column_] = width;
        dragged_[*dragged_column_] = true;
        place_parts();
        damaged_ = true;
    }

    constexpr void make_current(std::optional<std::size_t> column) noexcept {
        if (current_column_ == column) return;
        current_column_ = column;
        damaged_ = true;
    }

    // Where a column stands among the keys. The count, when it is not one.
    [[nodiscard]] constexpr std::size_t sort_key_place(std::size_t column) const noexcept {
        const auto keys = sort_keys();
        return static_cast<std::size_t>(std::ranges::find(keys, column, &GridSortKey::column) - keys.begin());
    }

    // A column not in the sort comes in last, while there is room. One going the other way turns and keeps its number. One already going this way is left alone, and nothing is drawn.
    constexpr void sort_current_column(bool descending) {
        if (!current_column_) return;
        const std::size_t place = sort_key_place(*current_column_);
        if (place < sort_count_) {
            if (sort_keys_[place].descending == descending) return;
            sort_keys_[place].descending = descending;
        } else {
            if (sort_count_ == grid_most_sort_keys) return;
            sort_keys_[sort_count_] = GridSortKey{*current_column_, descending};
            ++sort_count_;
        }
        apply_sort();
    }

    // Takes the current column out of the sort, and the later numbers close up. With nothing left, the rows are in their own order.
    constexpr void clear_current_column_sort() {
        if (!current_column_) return;
        const std::size_t place = sort_key_place(*current_column_);
        if (place == sort_count_) return;
        for (std::size_t k = place; k + 1 < sort_count_; ++k) sort_keys_[k] = sort_keys_[k + 1];
        --sort_count_;
        apply_sort();
    }

    // Always from the rows' own order, so rows equal on every key stay in that order, whichever way each key goes. The selection stays on its row.
    constexpr void apply_sort() {
        const std::optional<std::uint32_t> kept = selected_position_ ? std::optional<std::uint32_t>{order_[*selected_position_]} : std::nullopt;
        order_ = identity_order(static_cast<std::uint32_t>(rows_.size()));
        if (sort_count_ != 0) {
            const std::span<const Row> rows{rows_};
            stable_sort_indices(order_, [rows, keys = sort_keys()](std::uint32_t a, std::uint32_t b) {
                for (const GridSortKey& key : keys) {
                    const int compared = compare_in_column(key.column, rows[a], rows[b]);
                    if (compared != 0) return key.descending ? compared > 0 : compared < 0;
                }
                return false;
            });
        }
        if (kept) selected_position_ = position_of_index(order_, *kept);
        damaged_ = true;
    }

    // Negative when a comes first going up, positive when b does, and zero when they tie. Each column reads its own typed member.
    [[nodiscard]] static constexpr int compare_in_column(std::size_t column, const Row& a, const Row& b) {
        int answer = 0;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            const auto try_column = [&]<std::size_t K>(std::integral_constant<std::size_t, K>) {
                if (K != column) return false;
                const auto& first = ColumnAt<K>::value_of(a);
                const auto& second = ColumnAt<K>::value_of(b);
                if (first < second) answer = -1;
                if (second < first) answer = 1;
                return true;
            };
            static_cast<void>((try_column(std::integral_constant<std::size_t, I>{}) || ...));
        }(std::index_sequence_for<Columns...>{});
        return answer;
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_headers(const Shaper& text) {
        for (detail::ShapedCaption* letter : {&letter_monospace_, &letter_left_, &letter_center_, &letter_right_})
            if (auto shaped = letter->shape_with(text); !shaped) return std::unexpected(std::move(shaped.error()));
        const Width widest = std::max({letter_monospace_.size_with(text).width(), letter_left_.size_with(text).width(), letter_center_.size_with(text).width(), letter_right_.size_with(text).width()});
        button_width_ = widest + grid_control_letter_pad + grid_control_letter_pad;
        drag_floor_ = detail::drag_floor_width(letter_monospace_.size_with(text).width(), button_width_);
        for (std::size_t k = 0; k < column_count; ++k) {
            if (auto shaped = shape_title(k, text); !shaped) return std::unexpected(std::move(shaped.error()));
            const auto& run = title_shown(k).run();
            whole_titles_[k] = detail::whole_title_width(run ? run->size().width() : Width{}, button_width_, row_height_);
        }
        if (auto shaped = filler_title_long_.shape_with(text); !shaped) return std::unexpected(std::move(shaped.error()));
        if (auto shaped = filler_title_short_.shape_with(text); !shaped) return std::unexpected(std::move(shaped.error()));
        return {};
    }

    // In the column's own font. A column stays regular with no monospace face to have, so that one is here whenever it is in effect.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_title(std::size_t column, const Shaper& text) {
        if constexpr (HasMonospaceFace<Shaper>) {
            if (fonts_[column].applied() == ColumnFont::Monospace) {
                auto face = text.monospace_face();
                if (face) return monospace_headers_[column].shape_with(face->get());
                return std::unexpected(std::move(face.error()));
            }
        }
        return headers_[column].shape_with(text);
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
                const Right end = start + shown_[K];
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
        if constexpr (HasMonospaceFace<Shaper>) {
            if (fonts_[K].applied() == ColumnFont::Monospace) {
                auto face = text.monospace_face();
                if (face) return shape_column_with<K>(face->get());
                return std::unexpected(std::move(face.error()));
            }
        }
        return shape_column_with<K>(text);
    }

    template <std::size_t K, TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> shape_column_with(const Shaper& text) {
        using C = ColumnAt<K>;
        const std::size_t most_bytes = detail::cell_bytes_worth_shaping(shown_[K]);
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
            if (row_is_chosen(order_[position])) body.fill_rect(row, theme::button_pressed);
            Left start = columns_origin();
            for (std::size_t k = 0; k < column_count; ++k) {
                const Right end = start + shown_[k];
                if (const auto& run = cells_[r * column_count + k]; run) {
                    const Canvas cell = body.clipped(Rect{Point{start, top}, Size{shown_[k], row_height_}});
                    const Width room = shown_[k] - theme::cell_pad_x - theme::cell_pad_x;
                    const Left left = detail::aligned_left(left_after(start + theme::cell_pad_x), room, run->size().width(), aligns_[k].applied());
                    cell.draw_text(Point{left, detail::centred_top(top, row_height_, run->size().height())}, *run, theme::text);
                }
                start = left_after(end);
            }
            body.fill_rect(Rect{Point{body_.left(), row.bottom() - one_down}, Size{body_.width(), one_down}}, theme::bar_edge);
        }
        Left start = columns_origin();
        for (const Width width : shown_) {
            const Right end = start + width;
            body.fill_rect(Rect{Point{end - one_across, body_.top()}, Size{one_across, body_.height()}}, theme::bar_edge);
            start = left_after(end);
        }
    }

    constexpr void paint_header(const Canvas& local) const {
        constexpr Height one_down{1};
        constexpr Width one_across{1};
        // The strip goes the whole way across, so nothing is left blank above a scroll bar.
        const Rect strip{inner_.origin(), Size{inner_.width(), header_.height()}};
        local.fill_rect(strip, theme::bar);
        const Canvas head = local.clipped(header_);
        Left start = columns_origin();
        for (std::size_t k = 0; k < column_count; ++k) {
            const Right end = start + shown_[k];
            const Rect cell_area{Point{start, header_.top()}, Size{shown_[k], header_.height()}};
            const Canvas cell = head.clipped(cell_area);
            if (current_column_ == k) cell.fill_rect(cell_area, theme::grid_current_column);
            const detail::ColumnButtons buttons = detail::column_buttons_in(cell_area, button_width_);
            // With its whole title and the sort mark's room, the title sits left of the room. In a column dragged narrower, there is no room for a mark, and the title has all there is up to the buttons, where it is cut.
            const bool mark_fits = whole_titles_[k] <= shown_[k];
            const Left mark_left = (buttons.font.left() + Width{}) - detail::sort_mark_room(header_.height());
            const Left title_end = mark_fits ? mark_left : buttons.font.left();
            if (const auto& run = title_shown(k).run(); run) {
                const Canvas title_area = cell.clipped(Rect{cell_area.origin(), Size{(title_end + Width{}) - start, header_.height()}});
                const Left from = left_after(start + theme::cell_pad_x);
                const Width room = ((title_end + Width{}) - from) - theme::cell_pad_x;
                title_area.draw_text(Point{detail::aligned_left(from, room, run->size().width(), aligns_[k].applied()), detail::centred_top(header_.top(), header_.height(), run->size().height())}, *run, theme::text);
            }
            if (const std::size_t place = sort_key_place(k); mark_fits && place < sort_count_) paint_sort_mark(cell, mark_left, sort_keys_[place].descending, place);
            paint_button(cell, buttons.font, fonts_[k].chosen() == ColumnFont::Monospace ? &letter_monospace_ : nullptr);
            paint_button(cell, buttons.align, &letter_for(aligns_[k].chosen()));
            head.fill_rect(Rect{Point{end - one_across, header_.top()}, Size{one_across, header_.height()}}, theme::bar_edge);
            start = left_after(end);
        }
        local.fill_rect(Rect{Point{strip.left(), strip.bottom() - one_down}, Size{strip.width(), one_down}}, theme::bar_edge);
    }

    // The filler under a header like any other, with the longest title that fits it with its padding, if one does.
    constexpr void paint_filler(const Canvas& local) const {
        constexpr Width one_across{1};
        local.fill_rect(filler_, theme::grid_filler);
        if (filler_header_.empty()) return;
        local.fill_rect(Rect{Point{filler_header_.right() - one_across, filler_header_.top()}, Size{one_across, filler_header_.height()}}, theme::bar_edge);
        const Canvas head = local.clipped(filler_header_);
        const Left left = left_after(filler_header_.left() + theme::cell_pad_x);
        for (const detail::ShapedCaption* title : {&filler_title_long_, &filler_title_short_}) {
            if (const auto& run = title->run(); run && run->size().width() + theme::cell_pad_x + theme::cell_pad_x <= filler_header_.width()) {
                head.draw_text(Point{left, detail::centred_top(filler_header_.top(), filler_header_.height(), run->size().height())}, *run, theme::text);
                return;
            }
        }
    }

    // A solid triangle almost the header's height, its point up for ascending and down for descending, with its priority at its fat end when there is room for that.
    constexpr void paint_sort_mark(const Canvas& cell, Left mark_left, bool descending, std::size_t place) const {
        const std::int32_t rows = detail::sort_mark_rows(header_.height());
        if (rows == 0) return;
        const std::int32_t widest = detail::sort_mark_half_at(rows - 1);
        const Top mark_top = top_after(header_.top() + Height{1});
        const Left middle = left_after(mark_left + Width{widest});
        for (std::int32_t row = 0; row < rows; ++row) {
            const std::int32_t half = detail::sort_mark_half_at(descending ? rows - 1 - row : row);
            const Left left = (middle + Width{}) - Width{half};
            cell.fill_rect(Rect{Point{left, top_after(mark_top + Height{row})}, Size{Width{2 * half + 1}, Height{1}}}, theme::sort_mark);
        }
        if (rows < detail::sort_mark_rows_for_a_digit) return;
        const Top digit_top = top_after(mark_top + Height{descending ? 1 : rows - detail::sort_digit_rows - 1});
        const Left digit_left = (middle + Width{}) - Width{detail::sort_digit_columns / 2};
        const auto& digit = detail::sort_digits.at(place);
        for (std::int32_t row = 0; row < detail::sort_digit_rows; ++row) {
            for (std::int32_t column = 0; column < detail::sort_digit_columns; ++column) {
                const bool on = ((digit.at(static_cast<std::size_t>(row)) >> (detail::sort_digit_columns - 1 - column)) & 1) != 0;
                if (on) cell.fill_rect(Rect{Point{left_after(digit_left + Width{column}), top_after(digit_top + Height{row})}, Size{Width{1}, Height{1}}}, theme::sort_digit);
            }
        }
    }

    [[nodiscard]] constexpr const detail::ShapedCaption& letter_for(ColumnAlign align) const noexcept {
        switch (align) {
            case ColumnAlign::AtCenter: return letter_center_;
            case ColumnAlign::AtRight: return letter_right_;
            case ColumnAlign::AtLeft:
            default: return letter_left_;
        }
    }

    // A button with its outline, and its letter if it has one and the shaper did not refuse it.
    static constexpr void paint_button(const Canvas& cell, const Rect& area, const detail::ShapedCaption* letter) {
        const Canvas button = cell.clipped(area);
        button.fill_rect(area, theme::button_normal);
        button.stroke_rect(area, theme::button_edge);
        if (letter == nullptr || !letter->run()) return;
        const Size size = letter->run()->size();
        button.draw_text(Point{detail::aligned_left(area.left(), area.width(), size.width(), ColumnAlign::AtCenter), detail::centred_top(area.top(), area.height(), size.height())}, *letter->run(), theme::text);
    }

    std::vector<Row> rows_{};
    std::vector<std::uint32_t> order_{};
    // Each column's title in each font. The monospace one is shaped only once that font is in effect for that column.
    std::array<detail::ShapedCaption, column_count> headers_;
    std::array<detail::ShapedCaption, column_count> monospace_headers_;
    // The widths set, by the declaration or by a drag, and the widths as drawn, from those and from the rule in wanted_width, with any room left over in the last column.
    std::array<Width, column_count> widths_;
    std::array<Width, column_count> shown_;
    std::array<Width, column_count> whole_titles_{};
    std::array<bool, column_count> dragged_{};
    std::array<DelayedChoice<ColumnFont>, column_count> fonts_{};
    std::array<DelayedChoice<ColumnAlign>, column_count> aligns_{};
    // The buttons' letters, always in the regular font, whatever a column's own is.
    detail::ShapedCaption letter_monospace_{CheckedText{"M"}};
    detail::ShapedCaption letter_left_{CheckedText{"L"}};
    detail::ShapedCaption letter_center_{CheckedText{"C"}};
    detail::ShapedCaption letter_right_{CheckedText{"R"}};
    Width button_width_{};
    Width drag_floor_{};
    std::optional<Error> font_refusal_{};
    std::size_t column_changes_{};
    // What layout shaped for the rows in view, by row and then column. Bounded by the window's height and the column count.
    std::vector<std::optional<TextRun>> cells_{};
    ItemRange visible_{};
    std::size_t cells_shaped_{};
    ScrollBar<DownAxis> down_{};
    ScrollBar<AcrossAxis> across_{};
    Rect bounds_{};
    Rect inner_{};
    Rect filler_{};
    Rect filler_header_{};
    detail::ShapedCaption filler_title_long_{CheckedText{"Gap Fill Only"}};
    detail::ShapedCaption filler_title_short_{CheckedText{"Fill Only"}};
    Rect header_{};
    Rect body_{};
    Height row_height_{};
    std::optional<std::size_t> selected_position_{};  // the current row, which is always one of chosen_
    std::vector<std::uint32_t> chosen_{};             // every selected row, in the rows' own order, so a sort leaves them alone
    std::optional<std::uint32_t> anchor_row_{};       // where a Shift range starts
    std::optional<std::uint32_t> narrow_to_row_{};    // a press on one of several, waiting to be let go
    std::array<GridSortKey, grid_most_sort_keys> sort_keys_{};
    std::size_t sort_count_{};
    std::optional<std::size_t> current_column_{};
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
