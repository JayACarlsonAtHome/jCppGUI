// File: include/jCppGUI/v001/file_match_popup.hpp
// Purpose: FileMatchPopup<Tag, Transfers>: says how matching a selection came out: that they all match, or a list of what did not. jgui.hpp does not include it.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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
#include "panels.hpp"
#include "popup_layer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileMatchPopupHeading;
template <class Popup>
struct FileMatchPopupDetail;
template <class Popup>
struct FileMatchPopupGrid;
template <class Popup>
struct FileMatchPopupButton;

inline constexpr Width file_match_popup_across{1000};
inline constexpr Height file_match_popup_down{520};

namespace detail {

template <class Popup>
struct FileMatchPopupClose {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().close();
    }
};

// A match is only said of what was finished with nothing found, so a stopped one never says it.
[[nodiscard]] constexpr std::string match_heading(const MatchReport& report) {
    if (report.checked.differences != 0) {
        std::string text = count_with_plural(report.checked.differences, "thing");
        text.append(report.checked.differences == 1 ? " does not match" : " do not match");
        if (report.stopped) text.append(", and it was stopped before the end");
        return text;
    }
    if (report.stopped) return "Stopped before the end, so this says nothing of what was not reached";
    return "They all match";
}

[[nodiscard]] constexpr std::string match_detail(const MatchReport& report) {
    std::string text = match_progress_text(report.checked);
    text.append(". Times and permissions were not compared.");
    if (report.checked.differences > report.findings.size()) text.append(" Only the first ").append(decimal_text(report.findings.size(), false)).append(" are listed.");
    return text;
}

}  // namespace detail

template <class Tag, class Transfers>
class FileMatchPopup {
    using Heading = Label<FileMatchPopupHeading<Tag>>;
    using Detail = Label<FileMatchPopupDetail<Tag>>;
    using Grid = DataGrid<FileMatchPopupGrid<Tag>, MatchFinding, NoGridMessage, Column<"Where", &MatchFinding::where, 400>, Column<"What was found", &MatchFinding::why, 540>>;
    using OkButton = Button<FileMatchPopupButton<Tag>, detail::FileMatchPopupClose<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Detail, Grid, OkButton>;

    constexpr FileMatchPopup() : children_{Heading{""}, Detail{""}, Grid{}, OkButton{"OK", detail::FileMatchPopupClose<Tag>{}}} {}
    FileMatchPopup(const FileMatchPopup&) = delete;
    FileMatchPopup& operator=(const FileMatchPopup&) = delete;
    constexpr FileMatchPopup(FileMatchPopup&&) = default;
    FileMatchPopup& operator=(FileMatchPopup&&) = delete;
    constexpr ~FileMatchPopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return open_; }
    [[nodiscard]] constexpr std::string_view heading() const noexcept { return std::get<Heading>(children_).text(); }
    [[nodiscard]] constexpr std::string_view detail_text() const noexcept { return std::get<Detail>(children_).text(); }
    [[nodiscard]] constexpr std::span<const MatchFinding> findings() const noexcept { return std::get<Grid>(children_).rows(); }

    constexpr void close() noexcept {
        if (open_) damaged_ = true;
        open_ = false;
    }

    constexpr void open_with(MatchReport report) {
        // Our own words and paths that could be shown, and a finding is cut to what a line holds.
        for (MatchFinding& finding : report.findings) {
            finding.where = std::string{utf8_prefix(finding.where, max_caption_bytes)};
            finding.why = std::string{utf8_prefix(finding.why, max_caption_bytes)};
        }
        static_cast<void>(std::get<Heading>(children_).set_text(detail::match_heading(report)));
        static_cast<void>(std::get<Detail>(children_).set_text(detail::match_detail(report)));
        has_list_ = !report.findings.empty();
        static_cast<void>(std::get<Grid>(children_).set_rows(std::move(report.findings)));  // at most most_match_findings, well within a grid
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
        const Height wanted = has_list_ ? file_match_popup_down : label_row + label_row + button_row;
        box_ = popup_box_in(area, file_match_popup_across, wanted);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        auto below = std::get<Heading>(children_).layout(key, inside, text);
        if (!below) return std::unexpected(std::move(below.error()));
        below = std::get<Detail>(children_).layout(key, *below, text);
        if (!below) return std::unexpected(std::move(below.error()));
        const Rect list{below->origin(), Size{below->width(), std::max(below->height() - button_row, Height{})}};
        if (has_list_) {
            if (auto placed = std::get<Grid>(children_).layout(key, list, text); !placed) return std::unexpected(std::move(placed.error()));
        }
        const Rect button_room{Point{inside.left(), top_after(inside.bottom() - button_row + theme::label_pad_y)}, Size{inside.width(), button_row - theme::label_pad_y}};
        if (auto placed = std::get<OkButton>(children_).layout(key, button_room, text); !placed) return std::unexpected(std::move(placed.error()));
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
        if (has_list_) std::get<Grid>(children_).paint(key, inner);
        std::get<OkButton>(children_).paint(key, inner);
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
        if (auto report = ui.template get<Transfers>().take_match_report(); report) open_with(std::move(*report));
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
