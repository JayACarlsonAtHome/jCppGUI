// File: include/jCppGUI/v001/file_delete_popup.hpp
// Purpose: FileDeletePopup<Tag, Transfers>: asks whether to delete what was counted, saying how many. It goes to the trash, and only on a click on the Delete button. Every key, Return included, only ever says no,
//          because a key pressed a moment too late must not delete anything. jgui.hpp does not include it.
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "button.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "popup_layer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileDeletePopupHeading;
template <class Popup>
struct FileDeletePopupCount;
template <class Popup>
struct FileDeletePopupWhat;
template <class Popup>
struct FileDeletePopupWarning;
template <class Popup>
struct FileDeletePopupCancel;
template <class Popup>
struct FileDeletePopupConfirm;

inline constexpr Width file_delete_popup_across{800};

namespace detail {

template <class Popup>
struct FileDeletePopupSayNo {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_no(u);
    }
};

template <class Popup>
struct FileDeletePopupSayYes {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_yes(u);
    }
};

[[nodiscard]] constexpr std::string grouped_count(std::uint64_t count) { return CellFormat<FileBytes>::text(FileBytes{true, count}); }

[[nodiscard]] constexpr std::string delete_count_line(const DeleteCount& count) { return "File count to be deleted: " + grouped_count(count.files); }

[[nodiscard]] constexpr std::string delete_what_line(const DeleteQuestion& question) {
    std::string text = "Also " + count_with_plural(question.count.folders, "folder") + ", and " + grouped_count(question.count.bytes) + " bytes, from ";
    // The name comes first, because a long path is cut off at its end.
    text.append(question.paths.size() == 1 ? std::string{last_name_of(question.paths.front())} : count_with_plural(question.paths.size(), "selected thing"));
    text.append(" in ").append(parent_of_path(question.paths.front()));
    return text;
}

}  // namespace detail

template <class Tag, class Transfers>
class FileDeletePopup {
    using Heading = Label<FileDeletePopupHeading<Tag>>;
    using Count = Label<FileDeletePopupCount<Tag>>;
    using What = Label<FileDeletePopupWhat<Tag>>;
    using Warning = Label<FileDeletePopupWarning<Tag>>;
    using CancelButton = Button<FileDeletePopupCancel<Tag>, detail::FileDeletePopupSayNo<Tag>>;
    using ConfirmButton = Button<FileDeletePopupConfirm<Tag>, detail::FileDeletePopupSayYes<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Count, What, Warning, CancelButton, ConfirmButton>;

    constexpr FileDeletePopup()
        : children_{Heading{"Are you sure?"}, Count{""}, What{""}, Warning{"It goes to the trash, and your desktop's trash can bring it back. A link goes as a link."},
                    CancelButton{"Cancel", detail::FileDeletePopupSayNo<Tag>{}}, ConfirmButton{"Delete", detail::FileDeletePopupSayYes<Tag>{}}} {}
    FileDeletePopup(const FileDeletePopup&) = delete;
    FileDeletePopup& operator=(const FileDeletePopup&) = delete;
    constexpr FileDeletePopup(FileDeletePopup&&) = default;
    FileDeletePopup& operator=(FileDeletePopup&&) = delete;
    constexpr ~FileDeletePopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return open_; }
    [[nodiscard]] constexpr std::string_view heading() const noexcept { return std::get<Heading>(children_).text(); }
    [[nodiscard]] constexpr std::string_view count_text() const noexcept { return std::get<Count>(children_).text(); }
    [[nodiscard]] constexpr std::string_view what_text() const noexcept { return std::get<What>(children_).text(); }

    constexpr void open_with(const DeleteQuestion& question) {
        // A count and a path that could be shown, cut to what a line holds.
        static_cast<void>(std::get<Count>(children_).set_text(detail::delete_count_line(question.count)));
        static_cast<void>(std::get<What>(children_).set_text(utf8_prefix(detail::delete_what_line(question), max_caption_bytes)));
        open_ = true;
        damaged_ = true;
    }

    template <class U>
    constexpr void answer_no(U& ui) {
        close();
        ui.template get<Transfers>().cancel_delete();
    }

    template <class U>
    constexpr void answer_yes(U& ui) {
        close();
        ui.template get<Transfers>().confirm_delete();
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
        const Height wanted = label_row + label_row + label_row + label_row + button_row + theme::bar_pad_y + theme::bar_pad_y;
        box_ = popup_box_in(area, file_delete_popup_across, wanted);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        std::expected<Rect, Error> below{inside};
        const auto place = [&](auto& part) {
            if (below) below = part.layout(key, *below, text);
        };
        place(std::get<Heading>(children_));
        place(std::get<Count>(children_));
        place(std::get<What>(children_));
        place(std::get<Warning>(children_));
        if (!below) return std::unexpected(std::move(below.error()));
        // Apart, so a hand going for one does not land on the other.
        std::expected<Rect, Error> beside{Rect{Point{inside.left(), top_after(inside.bottom() - button_row + theme::label_pad_y)}, Size{inside.width(), button_row - theme::label_pad_y}}};
        if (beside) beside = std::get<CancelButton>(children_).layout(key, *beside, text);
        if (!beside) return std::unexpected(std::move(beside.error()));
        constexpr Width apart{48};
        const Rect far_off{Point{left_after(beside->left() + apart), beside->top()}, Size{beside->width() - apart, beside->height()}};
        if (auto placed = std::get<ConfirmButton>(children_).layout(key, far_off, text); !placed) return std::unexpected(std::move(placed.error()));
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

    // No key reaches the buttons, so none of them can say yes.
    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!open_) return false;
        if (event.pressed && (event.keysym == Key::Escape || event.keysym == Key::Return)) answer_no(ui);
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
        if (auto question = ui.template get<Transfers>().take_delete_question(); question) open_with(*question);
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
    constexpr void close() noexcept {
        if (open_) damaged_ = true;
        open_ = false;
    }

    children_type children_;
    Rect box_{};
    bool open_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
