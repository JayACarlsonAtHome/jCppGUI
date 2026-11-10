// File: include/jCppGUI/v001/file_notice_popup.hpp
// Purpose: FileNoticePopup<Tag, Transfers>: says something a person must read and close, a heading and a line, when asking for something could not go ahead. jgui.hpp does not include it.
#pragma once

#include <expected>
#include <string_view>
#include <tuple>
#include <utility>

#include "button.hpp"
#include "copy_line.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "panels.hpp"
#include "popup_layer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileNoticePopupHeading;
template <class Popup>
struct FileNoticePopupDetail;
template <class Popup>
struct FileNoticePopupButton;

inline constexpr Width file_notice_popup_across{1000};

namespace detail {

template <class Popup>
struct FileNoticePopupClose {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().close();
    }
};

}  // namespace detail

template <class Tag, class Transfers>
class FileNoticePopup {
    using Heading = CopyLine<FileNoticePopupHeading<Tag>>;
    using Detail = CopyLine<FileNoticePopupDetail<Tag>>;
    using OkButton = Button<FileNoticePopupButton<Tag>, detail::FileNoticePopupClose<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Detail, OkButton>;

    constexpr FileNoticePopup() : children_{Heading{""}, Detail{""}, OkButton{"OK", detail::FileNoticePopupClose<Tag>{}}} {}
    FileNoticePopup(const FileNoticePopup&) = delete;
    FileNoticePopup& operator=(const FileNoticePopup&) = delete;
    constexpr FileNoticePopup(FileNoticePopup&&) = default;
    FileNoticePopup& operator=(FileNoticePopup&&) = delete;
    constexpr ~FileNoticePopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return open_; }
    [[nodiscard]] constexpr std::string_view heading() const noexcept { return std::get<Heading>(children_).text(); }
    [[nodiscard]] constexpr std::string_view detail_text() const noexcept { return std::get<Detail>(children_).text(); }

    constexpr void close() noexcept {
        if (open_) damaged_ = true;
        open_ = false;
    }

    constexpr void open_with(const FileNotice& notice) {
        show_in(std::get<Heading>(children_), notice.heading);
        show_in(std::get<Detail>(children_), notice.detail);
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
        box_ = popup_box_in(area, file_notice_popup_across, label_row + label_row + button_row);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        auto below = std::get<Heading>(children_).layout(key, inside, text);
        if (!below) return std::unexpected(std::move(below.error()));
        below = std::get<Detail>(children_).layout(key, *below, text);
        if (!below) return std::unexpected(std::move(below.error()));
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
        detail::paint_in_order(children_, key, inner);
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

    // The transfers come before this in the tree, so what they say is seen in the same turn.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        if (auto notice = ui.template get<Transfers>().take_notice(); notice) open_with(*notice);
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
    // Cut to fit. A name here could be shown, so a refusal only means that rule was broken, and the line says so.
    template <class Line>
    static constexpr void show_in(Line& line, std::string_view text) {
        if (!line.set_text(utf8_prefix(text, max_caption_bytes))) static_cast<void>(line.set_text("This message cannot be shown"));
    }

    children_type children_;
    Rect box_{};
    bool open_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
