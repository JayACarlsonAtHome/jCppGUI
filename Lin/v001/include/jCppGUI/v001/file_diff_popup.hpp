// File: include/jCppGUI/v001/file_diff_popup.hpp
// Purpose: FileDiffPopup<Tag, Transfers>: which lines of two files differ, with a few lines around each change, or that they have none. It only shows: nothing can be changed from it. jgui.hpp does not include it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "button.hpp"
#include "datagrid.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "line_diff.hpp"
#include "popup_layer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileDiffPopupHeading;
template <class Popup>
struct FileDiffPopupDetail;
template <class Popup>
struct FileDiffPopupRight;
template <class Popup>
struct FileDiffPopupGrid;
template <class Popup>
struct FileDiffPopupButton;

inline constexpr Width file_diff_popup_across{1600};
inline constexpr Height file_diff_popup_down{1000};
inline constexpr Width file_diff_popup_margin_across{24};
inline constexpr Height file_diff_popup_margin_down{24};

namespace detail {

template <class Popup>
struct FileDiffPopupClose {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().close();
    }
};

[[nodiscard]] constexpr std::string diff_heading(const DiffReport& report) {
    if (report.outcome.too_different) return "More than " + decimal_text(static_cast<std::uint64_t>(most_diff_changes), false) + " lines differ, so they are not listed";
    if (report.outcome.left_only == 0 && report.outcome.right_only == 0) return "No lines differ";
    return count_with_plural(report.outcome.left_only, "line") + " only on the left, " + count_with_plural(report.outcome.right_only, "line") + " only on the right";
}

}  // namespace detail

template <class Tag, class Transfers>
class FileDiffPopup {
    using Heading = Label<FileDiffPopupHeading<Tag>>;
    using Detail = Label<FileDiffPopupDetail<Tag>>;
    using RightPath = Label<FileDiffPopupRight<Tag>>;
    using Grid = DataGrid<FileDiffPopupGrid<Tag>, DiffRow, NoGridMessage, Column<"Left", &DiffRow::left, 70>, Column<"Right", &DiffRow::right, 70>, Column<"Only in", &DiffRow::only_in, 70>, Column<"Line", &DiffRow::text, 1300>>;
    using CloseButton = Button<FileDiffPopupButton<Tag>, detail::FileDiffPopupClose<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Detail, RightPath, Grid, CloseButton>;

    constexpr FileDiffPopup() : children_{Heading{""}, Detail{""}, RightPath{""}, Grid{}, CloseButton{"OK", detail::FileDiffPopupClose<Tag>{}}} {
        // Code lines up only in this. Without such a font the grid stays with the usual one.
        std::get<Grid>(children_).set_every_column_font(ColumnFont::Monospace);
    }
    FileDiffPopup(const FileDiffPopup&) = delete;
    FileDiffPopup& operator=(const FileDiffPopup&) = delete;
    constexpr FileDiffPopup(FileDiffPopup&&) = default;
    FileDiffPopup& operator=(FileDiffPopup&&) = delete;
    constexpr ~FileDiffPopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return open_; }
    [[nodiscard]] constexpr std::string_view heading() const noexcept { return std::get<Heading>(children_).text(); }
    [[nodiscard]] constexpr std::string_view detail_text() const noexcept { return std::get<Detail>(children_).text(); }
    [[nodiscard]] constexpr std::string_view right_text() const noexcept { return std::get<RightPath>(children_).text(); }
    [[nodiscard]] constexpr std::span<const DiffRow> rows() const noexcept { return std::get<Grid>(children_).rows(); }

    constexpr void close() noexcept {
        if (open_) damaged_ = true;
        open_ = false;
    }

    constexpr void open_with(DiffReport report) {
        // Our own words and paths that could be shown, and each line was made safe to show.
        static_cast<void>(std::get<Heading>(children_).set_text(detail::diff_heading(report)));
        static_cast<void>(std::get<Detail>(children_).set_text(utf8_prefix("Left: " + report.first, max_caption_bytes)));
        static_cast<void>(std::get<RightPath>(children_).set_text(utf8_prefix("Right: " + report.second, max_caption_bytes)));
        has_list_ = !report.outcome.rows.empty();
        static_cast<void>(std::get<Grid>(children_).set_rows(std::move(report.outcome.rows)));  // bounded by the changes allowed, well within a grid
        open_ = true;
        damaged_ = true;
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        if (!open_) {
            box_ = Rect{};
            return area;
        }
        const Height line = text.line_height();
        const Height label_row = line + theme::label_pad_y + theme::label_pad_y;
        const Height button_row = line + theme::button_pad_y + theme::button_pad_y + theme::label_pad_y + theme::label_pad_y;
        const Width room_across = area.width() - std::min(area.width(), file_diff_popup_margin_across + file_diff_popup_margin_across);
        const Height room_down = area.height() - std::min(area.height(), file_diff_popup_margin_down + file_diff_popup_margin_down);
        const Height wanted = has_list_ ? std::min(file_diff_popup_down, room_down) : label_row + label_row + label_row + button_row + theme::bar_pad_y + theme::bar_pad_y;
        box_ = popup_box_in(area, std::min(file_diff_popup_across, room_across), wanted);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        auto below = std::get<Heading>(children_).layout(key, inside, text);
        if (!below) return std::unexpected(std::move(below.error()));
        below = std::get<Detail>(children_).layout(key, *below, text);
        if (!below) return std::unexpected(std::move(below.error()));
        below = std::get<RightPath>(children_).layout(key, *below, text);
        if (!below) return std::unexpected(std::move(below.error()));
        if (has_list_) {
            const Rect list{below->origin(), Size{below->width(), std::max(below->height() - button_row, Height{})}};
            if (auto placed = std::get<Grid>(children_).layout(key, list, text); !placed) return std::unexpected(std::move(placed.error()));
        }
        const Rect button_room{Point{inside.left(), top_after(inside.bottom() - button_row + theme::label_pad_y)}, Size{inside.width(), button_row - theme::label_pad_y}};
        if (auto placed = std::get<CloseButton>(children_).layout(key, button_room, text); !placed) return std::unexpected(std::move(placed.error()));
        return area;
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        if (!open_) return;
        const Canvas local = canvas.clipped(box_);
        local.fill_rect(box_, theme::background);
        local.stroke_rect(box_, theme::frame_edge);
        Canvas inner = canvas.clipped(box_);
        std::get<Heading>(children_).paint(key, inner);
        std::get<Detail>(children_).paint(key, inner);
        std::get<RightPath>(children_).paint(key, inner);
        if (has_list_) std::get<Grid>(children_).paint(key, inner);
        std::get<CloseButton>(children_).paint(key, inner);
    }

    // Everything is taken while it is open, wherever the pointer is.
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerPress& event) {
        if (!open_) return false;
        static_cast<void>(detail::deliver_to_topmost(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerScroll& event) {
        if (!open_) return false;
        static_cast<void>(detail::deliver_to_topmost(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        if (!open_) return false;
        static_cast<void>(detail::deliver(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const KeyEvent& event) {
        if (!open_) return false;
        if (event.pressed && (event.keysym == Key::Escape || event.keysym == Key::Return)) {
            close();
            return true;
        }
        static_cast<void>(detail::deliver(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return detail::deliver(children_, key, ui, event) || damaged_;
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey& key, U& ui, const E& event) {
        return detail::deliver(children_, key, ui, event);
    }

    // The transfers come before this in the tree, so what they finish is seen in the same turn.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        if (auto report = ui.template get<Transfers>().take_diff_report(); report) open_with(std::move(*report));
        detail::notify_all(children_, key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        damaged_ = false;
        detail::notify_all(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey& key, U& ui, const E& event) {
        detail::notify_all(children_, key, ui, event);
    }

private:
    children_type children_;
    Rect box_{};
    bool open_{};
    bool has_list_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
