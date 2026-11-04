// File: include/jCppGUI/v001/file_clipboard_popup.hpp
// Purpose: FileClipboardPopup<Tag, Transfers>: asks a person before text the sanitizer found something in goes to the clipboard. Nothing has reached the clipboard while this is open. Only a click on its Continue button lets it
//          go, and every key says abort, because a key pressed a moment too late must not decide this. jgui.hpp does not include it.
#pragma once

#include <expected>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "button.hpp"
#include "clipboard_text.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "popup_layer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Popup>
struct FileClipboardPopupHeading;
template <class Popup>
struct FileClipboardPopupFound;
template <class Popup>
struct FileClipboardPopupText;
template <class Popup>
struct FileClipboardPopupWarning;
template <class Popup>
struct FileClipboardPopupAbort;
template <class Popup>
struct FileClipboardPopupContinue;

inline constexpr Width file_clipboard_popup_across{900};

namespace detail {

template <class Popup>
struct FileClipboardPopupSayAbort {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_abort(u);
    }
};

template <class Popup>
struct FileClipboardPopupSayContinue {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_continue(u);
    }
};

}  // namespace detail

template <class Tag, class Transfers>
class FileClipboardPopup {
    using Heading = Label<FileClipboardPopupHeading<Tag>>;
    using Found = Label<FileClipboardPopupFound<Tag>>;
    using TextLine = Label<FileClipboardPopupText<Tag>>;
    using Warning = Label<FileClipboardPopupWarning<Tag>>;
    using AbortButton = Button<FileClipboardPopupAbort<Tag>, detail::FileClipboardPopupSayAbort<Tag>>;
    using ContinueButton = Button<FileClipboardPopupContinue<Tag>, detail::FileClipboardPopupSayContinue<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, Found, TextLine, Warning, AbortButton, ContinueButton>;

    constexpr FileClipboardPopup()
        : children_{Heading{"This text may not be safe to paste, and nothing has been copied"}, Found{""}, TextLine{""}, Warning{"On Continue, control and invisible characters are replaced, and shell characters are kept. Read it before you run it anywhere."},
                    AbortButton{"Abort", detail::FileClipboardPopupSayAbort<Tag>{}}, ContinueButton{"Continue", detail::FileClipboardPopupSayContinue<Tag>{}}} {}
    FileClipboardPopup(const FileClipboardPopup&) = delete;
    FileClipboardPopup& operator=(const FileClipboardPopup&) = delete;
    constexpr FileClipboardPopup(FileClipboardPopup&&) = default;
    FileClipboardPopup& operator=(FileClipboardPopup&&) = delete;
    constexpr ~FileClipboardPopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return question_.has_value(); }
    [[nodiscard]] constexpr std::string_view found_text() const noexcept { return std::get<Found>(children_).text(); }
    [[nodiscard]] constexpr std::string_view text_shown() const noexcept { return std::get<TextLine>(children_).text(); }

    template <class U>
    constexpr void answer_abort(U& ui) {
        if (!question_) return;
        close();
        ui.template get<Transfers>().tell("Nothing was copied");
    }

    // With the serial of the press on the button itself: the one that asked may be too old for a compositor by now.
    template <class U>
    constexpr void answer_continue(U& ui) {
        if (!question_ || !last_press_) return;
        ui.copy_after_question(*last_press_, std::move(question_->text));
        close();
        ui.template get<Transfers>().tell("Copied, with what was named replaced or kept as it said");
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
        box_ = popup_box_in(area, file_clipboard_popup_across, label_row + label_row + label_row + label_row + button_row + theme::bar_pad_y + theme::bar_pad_y);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        std::expected<Rect, Error> below{inside};
        const auto place = [&](auto& part) {
            if (below) below = part.layout(key, *below, text);
        };
        place(std::get<Heading>(children_));
        place(std::get<Found>(children_));
        place(std::get<TextLine>(children_));
        place(std::get<Warning>(children_));
        if (!below) return std::unexpected(std::move(below.error()));
        // Apart, so a hand going for one does not land on the other.
        std::expected<Rect, Error> beside{Rect{Point{inside.left(), top_after(inside.bottom() - button_row + theme::label_pad_y)}, Size{inside.width(), button_row - theme::label_pad_y}}};
        if (beside) beside = std::get<AbortButton>(children_).layout(key, *beside, text);
        if (!beside) return std::unexpected(std::move(beside.error()));
        constexpr Width apart{48};
        const Rect far_off{Point{left_after(beside->left() + apart), beside->top()}, Size{beside->width() - apart, beside->height()}};
        if (auto placed = std::get<ContinueButton>(children_).layout(key, far_off, text); !placed) return std::unexpected(std::move(placed.error()));
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
        last_press_ = event.serial;
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

    // No key reaches the buttons, so none of them can say continue.
    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!question_) return false;
        if (event.pressed && (event.keysym == Key::Escape || event.keysym == Key::Return)) answer_abort(ui);
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

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        if (auto question = ui.take_clipboard_question(); question) open_with(std::move(*question));
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
    constexpr void open_with(ClipboardQuestion question) {
        // Words of our own and text the sanitizer has been through, so these are not refused.
        static_cast<void>(std::get<Found>(children_).set_text(clipboard_findings_text(question.found)));
        static_cast<void>(std::get<TextLine>(children_).set_text(question.text.view()));
        question_ = std::move(question);
        last_press_.reset();
        damaged_ = true;
    }

    constexpr void close() {
        question_.reset();
        damaged_ = true;
    }

    children_type children_;
    std::optional<ClipboardQuestion> question_{};
    std::optional<Serial> last_press_{};
    Rect box_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
