// File: include/jCppGUI/v001/file_name_popup.hpp
// Purpose: FileNamePopup<Tag, Transfers>: asks a person to type a name, for Touch, Rename and ScreenShot. Return and OK use it, and Escape and Cancel change nothing. Unlike the popups that guard a delete, Return may say yes here, because none of these can replace or remove
//          anything. A name that is refused stays in the box with the reason under it, to be corrected. jgui.hpp does not include it.
#pragma once

#include <expected>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "button.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "popup_layer.hpp"
#include "text_entry.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileNamePopupHeading;
template <class Popup>
struct FileNamePopupWhere;
template <class Popup>
struct FileNamePopupEntry;
template <class Popup>
struct FileNamePopupProblem;
template <class Popup>
struct FileNamePopupCancel;
template <class Popup>
struct FileNamePopupConfirm;

inline constexpr Width file_name_popup_across{800};

namespace detail {

template <class Popup>
struct FileNamePopupSayNo {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_no(u);
    }
};

template <class Popup>
struct FileNamePopupSayYes {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_yes(u);
    }
};

[[nodiscard]] constexpr std::string_view file_name_heading(NameAsk ask) noexcept {
    switch (ask) {
        case NameAsk::Touch: return "Touch: type a name. A new file is made, or a name already there gets the time of now.";
        case NameAsk::Rename: return "Rename: type the new name. A name already there is refused.";
        case NameAsk::ScreenShot:
        default: return "ScreenShot: type a name for the picture of this window. A name already there is refused.";
    }
}

}  // namespace detail

template <class Tag, class Transfers>
class FileNamePopup {
    using Heading = Label<FileNamePopupHeading<Tag>>;
    using Where = Label<FileNamePopupWhere<Tag>>;
    using Entry = TextEntry<FileNamePopupEntry<Tag>>;
    using Problem = Label<FileNamePopupProblem<Tag>>;
    using CancelButton = Button<FileNamePopupCancel<Tag>, detail::FileNamePopupSayNo<Tag>>;
    using ConfirmButton = Button<FileNamePopupConfirm<Tag>, detail::FileNamePopupSayYes<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Where, Entry, Problem, CancelButton, ConfirmButton>;

    constexpr FileNamePopup()
        : children_{Heading{""}, Where{""}, Entry{most_file_name_bytes}, Problem{""}, CancelButton{"Cancel", detail::FileNamePopupSayNo<Tag>{}}, ConfirmButton{"OK", detail::FileNamePopupSayYes<Tag>{}}} {}
    FileNamePopup(const FileNamePopup&) = delete;
    FileNamePopup& operator=(const FileNamePopup&) = delete;
    constexpr FileNamePopup(FileNamePopup&&) = default;
    FileNamePopup& operator=(FileNamePopup&&) = delete;
    constexpr ~FileNamePopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return question_.has_value(); }
    [[nodiscard]] constexpr std::string_view heading() const noexcept { return std::get<Heading>(children_).text(); }
    [[nodiscard]] constexpr std::string_view where() const noexcept { return std::get<Where>(children_).text(); }
    [[nodiscard]] constexpr std::string_view typed() const noexcept { return std::get<Entry>(children_).text(); }
    [[nodiscard]] constexpr std::string_view problem() const noexcept { return std::get<Problem>(children_).text(); }

    constexpr void open_with(NameQuestion question) {
        // Words of our own, a path that could be shown cut to what a line holds, and a name that was in a row, so these are not refused.
        static_cast<void>(std::get<Heading>(children_).set_text(detail::file_name_heading(question.ask)));
        static_cast<void>(std::get<Where>(children_).set_text(utf8_prefix("In " + question.folder, max_caption_bytes)));
        static_cast<void>(std::get<Entry>(children_).set_text(question.current));
        static_cast<void>(std::get<Problem>(children_).set_text(""));
        question_ = std::move(question);
        damaged_ = true;
    }

    template <class U>
    constexpr void answer_no(U& ui) {
        if (!question_) return;
        close();
        ui.template get<Transfers>().tell("Nothing was changed");
    }

    template <class U>
    constexpr void answer_yes(U& ui) {
        if (!question_) return;
        auto given = ui.template get<Transfers>().give_name(*question_, std::get<Entry>(children_).text());
        if (given) {
            // This popup closes first, so the next frame, which is the one the picture is of, does not show it.
            if (question_->ask == NameAsk::ScreenShot) ui.request_picture();
            close();
            return;
        }
        // What a source says of a name holds only one that can show, so this is not refused.
        static_cast<void>(std::get<Problem>(children_).set_text(utf8_prefix(given.error().message.units(), max_caption_bytes)));
        damaged_ = true;
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        if (!question_) {
            box_ = Rect{};
            return area;
        }
        const Height line = text.line_height();
        const Height label_row = line + theme::label_pad_y + theme::label_pad_y;
        const Height button_row = line + theme::button_pad_y + theme::button_pad_y + theme::label_pad_y + theme::label_pad_y;
        box_ = popup_box_in(area, file_name_popup_across, label_row + label_row + label_row + label_row + button_row + theme::bar_pad_y + theme::bar_pad_y);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        std::expected<Rect, Error> below{inside};
        const auto place = [&](auto& part) {
            if (below) below = part.layout(key, *below, text);
        };
        place(std::get<Heading>(children_));
        place(std::get<Where>(children_));
        place(std::get<Entry>(children_));
        place(std::get<Problem>(children_));
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
        if (!question_) return;
        const Canvas local = canvas.clipped(box_);
        local.fill_rect(box_, theme::background);
        local.stroke_rect(box_, theme::frame_edge);
        Canvas inner = canvas.clipped(box_);
        detail::paint_in_order(children_, key, inner);
    }

    // Everything is taken while it is open, wherever the pointer is.
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerPress& event) {
        if (!question_) return false;
        static_cast<void>(detail::deliver_to_topmost(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerScroll& event) {
        if (!question_) return false;
        static_cast<void>(detail::deliver_to_topmost(children_, key, ui, event));
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        if (!question_) return false;
        static_cast<void>(detail::deliver(children_, key, ui, event));
        return true;
    }

    // The keys go to the box and never to the buttons, so Return here is always the box's answer.
    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!question_) return false;
        if (!event.pressed) return true;
        if (event.keysym == Key::Escape) {
            answer_no(ui);
        } else if (event.keysym == Key::Return || event.keysym == Key::KeypadEnter) {
            answer_yes(ui);
        } else if (std::get<Entry>(children_).take_key(event) && !problem().empty()) {
            static_cast<void>(std::get<Problem>(children_).set_text(""));
        }
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

    // The transfers come before this in the tree, so what they were asked is seen in the same turn.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        if (auto question = ui.template get<Transfers>().take_name_question(); question) open_with(std::move(*question));
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
    constexpr void close() {
        question_.reset();
        damaged_ = true;
    }

    children_type children_;
    std::optional<NameQuestion> question_{};
    Rect box_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
