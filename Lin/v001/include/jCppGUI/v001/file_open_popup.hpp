// File: include/jCppGUI/v001/file_open_popup.hpp
// Purpose: FileOpenPopup<Tag, Transfers>: asks what program to open a file with, when it has no default that can be started. It lists the common ones that are installed, each its own button, and takes a program's name typed in a box. jgui.hpp does not include it.
#pragma once

#include <cstddef>
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
struct FileOpenPopupHeading;
template <class Popup>
struct FileOpenPopupWhat;
template <class Popup>
struct FileOpenPopupEntry;
template <class Popup>
struct FileOpenPopupProblem;
template <class Popup, std::size_t I>
struct FileOpenPopupChoice;
template <class Popup>
struct FileOpenPopupCancel;
template <class Popup>
struct FileOpenPopupConfirm;

inline constexpr Width file_open_popup_across{800};
inline constexpr std::size_t file_open_choices_across = 2;
inline constexpr std::size_t most_typed_program_bytes = 255;

namespace detail {

template <class Popup>
struct FileOpenPopupSayNo {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_no(u);
    }
};

template <class Popup>
struct FileOpenPopupSayTyped {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_typed(u);
    }
};

template <class Popup, std::size_t I>
struct FileOpenPopupSayChoice {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Popup>().answer_choice(u, I);
    }
};

}  // namespace detail

template <class Tag, class Transfers, class Indexes>
class BasicFileOpenPopup;

// One button for each place on the list, each its own type. Those the list does not reach take no room and cannot be pressed.
template <class Tag, class Transfers, std::size_t... I>
class BasicFileOpenPopup<Tag, Transfers, std::index_sequence<I...>> {
    using Heading = Label<FileOpenPopupHeading<Tag>>;
    using What = Label<FileOpenPopupWhat<Tag>>;
    using Entry = TextEntry<FileOpenPopupEntry<Tag>>;
    using Problem = Label<FileOpenPopupProblem<Tag>>;
    template <std::size_t N>
    using ChoiceButton = Button<FileOpenPopupChoice<Tag, N>, detail::FileOpenPopupSayChoice<Tag, N>>;
    using CancelButton = Button<FileOpenPopupCancel<Tag>, detail::FileOpenPopupSayNo<Tag>>;
    using ConfirmButton = Button<FileOpenPopupConfirm<Tag>, detail::FileOpenPopupSayTyped<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Heading, What, Entry, Problem, ChoiceButton<I>..., CancelButton, ConfirmButton>;

    constexpr BasicFileOpenPopup()
        : children_{Heading{"This file has no default program. Click one, or type the name of a program and press Return."}, What{""}, Entry{most_typed_program_bytes}, Problem{""},
                    ChoiceButton<I>{"", detail::FileOpenPopupSayChoice<Tag, I>{}}..., CancelButton{"Cancel", detail::FileOpenPopupSayNo<Tag>{}}, ConfirmButton{"Open typed", detail::FileOpenPopupSayTyped<Tag>{}}} {}
    BasicFileOpenPopup(const BasicFileOpenPopup&) = delete;
    BasicFileOpenPopup& operator=(const BasicFileOpenPopup&) = delete;
    constexpr BasicFileOpenPopup(BasicFileOpenPopup&&) = default;
    BasicFileOpenPopup& operator=(BasicFileOpenPopup&&) = delete;
    constexpr ~BasicFileOpenPopup() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return box_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool is_open() const noexcept { return question_.has_value(); }
    [[nodiscard]] constexpr std::string_view what_text() const noexcept { return std::get<What>(children_).text(); }
    [[nodiscard]] constexpr std::string_view problem() const noexcept { return std::get<Problem>(children_).text(); }
    [[nodiscard]] constexpr std::size_t choice_count() const noexcept { return question_ ? question_->choices.size() : 0; }

    constexpr void open_with(OpenQuestion question) {
        // A path that could be shown, cut to what a line holds, and titles of our own, so these are not refused.
        static_cast<void>(std::get<What>(children_).set_text(utf8_prefix(std::string{last_name_of(question.path)} + " in " + parent_of_path(question.path), max_caption_bytes)));
        static_cast<void>(std::get<Entry>(children_).set_text(""));
        static_cast<void>(std::get<Problem>(children_).set_text(utf8_prefix(question.reason, max_caption_bytes)));
        (set_choice_caption<I>(question), ...);
        question_ = std::move(question);
        damaged_ = true;
    }

    template <class U>
    constexpr void answer_no(U& ui) {
        if (!question_) return;
        close();
        ui.template get<Transfers>().tell("Nothing was opened");
    }

    template <class U>
    constexpr void answer_choice(U& ui, std::size_t index) {
        if (!question_ || index >= question_->choices.size()) return;
        settle(ui.template get<Transfers>().open_with_choice(question_->path, question_->choices[index]));
    }

    template <class U>
    constexpr void answer_typed(U& ui) {
        if (!question_) return;
        settle(ui.template get<Transfers>().open_with_typed(question_->path, std::get<Entry>(children_).text()));
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        if (!question_) {
            box_ = Rect{};
            (static_cast<void>(place_choice<I>(key, Rect{}, Rect{}, text)), ...);
            return area;
        }
        const Height line = text.line_height();
        const Height label_row = line + theme::label_pad_y + theme::label_pad_y;
        const Height button_row = line + theme::button_pad_y + theme::button_pad_y + theme::label_pad_y + theme::label_pad_y;
        const std::size_t choice_rows = (question_->choices.size() + file_open_choices_across - 1) / file_open_choices_across;
        box_ = popup_box_in(area, file_open_popup_across, label_row + label_row + label_row + label_row + repeated_extent(button_row, choice_rows) + button_row + theme::bar_pad_y + theme::bar_pad_y);
        const Rect inside = box_.inset(theme::label_pad_x, theme::bar_pad_y);
        std::expected<Rect, Error> below{inside};
        const auto place = [&](auto& part) {
            if (below) below = part.layout(key, *below, text);
        };
        place(std::get<Heading>(children_));
        place(std::get<What>(children_));
        place(std::get<Entry>(children_));
        place(std::get<Problem>(children_));
        if (!below) return std::unexpected(std::move(below.error()));
        std::expected<void, Error> placed{};
        const auto keep_first_failure = [&](std::expected<void, Error> result) {
            if (placed && !result) placed = std::move(result);
        };
        (keep_first_failure(place_choice<I>(key, *below, Rect{Point{below->left(), below->top()}, Size{smaller_half_of(below->width()), button_row}}, text)), ...);
        if (!placed) return std::unexpected(std::move(placed.error()));
        // Apart, so a hand going for one does not land on the other.
        std::expected<Rect, Error> beside{Rect{Point{inside.left(), top_after(inside.bottom() - button_row + theme::label_pad_y)}, Size{inside.width(), button_row - theme::label_pad_y}}};
        if (beside) beside = std::get<CancelButton>(children_).layout(key, *beside, text);
        if (!beside) return std::unexpected(std::move(beside.error()));
        constexpr Width apart{48};
        const Rect far_off{Point{left_after(beside->left() + apart), beside->top()}, Size{beside->width() - apart, beside->height()}};
        if (auto confirmed = std::get<ConfirmButton>(children_).layout(key, far_off, text); !confirmed) return std::unexpected(std::move(confirmed.error()));
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

    // The keys go to the box and never to the buttons, so Return is always what was typed.
    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!question_) return false;
        if (!event.pressed) return true;
        if (event.keysym == Key::Escape) {
            answer_no(ui);
        } else if (event.keysym == Key::Return || event.keysym == Key::KeypadEnter) {
            answer_typed(ui);
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
        if (auto question = ui.template get<Transfers>().take_open_question(); question) open_with(std::move(*question));
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
    // A title of our own, so it is not refused. Past the end of the list it has no caption.
    template <std::size_t N>
    constexpr void set_choice_caption(const OpenQuestion& question) {
        const std::string_view title = N < question.choices.size() ? question.choices[N].title : std::string_view{};
        if (auto checked = CheckedText::check(title); checked) std::get<ChoiceButton<N>>(children_).set_caption(*checked);
    }

    // Down the list in rows of two, in places worked out from the first. One the list does not reach is given no room.
    template <std::size_t N, TextShaper Shaper>
    constexpr std::expected<void, Error> place_choice(const TreeKey& key, Rect room, Rect first, const Shaper& text) {
        const bool shown = question_ && N < question_->choices.size();
        const Width across = repeated_extent(first.width(), N % file_open_choices_across);
        const Height down = repeated_extent(first.height(), N / file_open_choices_across);
        const Rect place{Point{left_after(first.left() + across), top_after(first.top() + down)}, first.size()};
        auto laid = std::get<ChoiceButton<N>>(children_).layout(key, shown ? room.intersect(place) : Rect{}, text);
        if (!laid) return std::unexpected(std::move(laid.error()));
        return {};
    }

    // On a yes it closes, and a refusal stays open with the reason under the box.
    constexpr void settle(std::expected<void, Error> result) {
        if (result) {
            close();
            return;
        }
        static_cast<void>(std::get<Problem>(children_).set_text(utf8_prefix(result.error().message.units(), max_caption_bytes)));
        damaged_ = true;
    }

    constexpr void close() {
        question_.reset();
        damaged_ = true;
    }

    children_type children_;
    std::optional<OpenQuestion> question_{};
    Rect box_{};
    bool damaged_{};
};

template <class Tag, class Transfers>
using FileOpenPopup = BasicFileOpenPopup<Tag, Transfers, std::make_index_sequence<most_open_choices>>;

}  // namespace jgui::inline v001
