// File: include/jCppGUI/v001/text_entry.hpp
// Purpose: TextEntry<Tag>: one line a person can type in, with a caret and a limit in bytes. Whatever holds it passes it the keys, so it types only while it is meant to. Text must be able to show, so a keymap cannot put a control
//          character or invalid UTF-8 in it. It knows nothing of what the text is for. jgui.hpp does not include it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "widget.hpp"

namespace jgui::inline v001 {

namespace detail {

[[nodiscard]] constexpr bool utf8_continues_at(std::string_view text, std::size_t at) noexcept { return at < text.size() && (static_cast<std::uint8_t>(text[at]) & 0xC0U) == 0x80U; }

// Where the character before this place starts. The text is valid, so this stops.
[[nodiscard]] constexpr std::size_t utf8_start_before(std::string_view text, std::size_t at) noexcept {
    if (at == 0) return 0;
    --at;
    while (at > 0 && utf8_continues_at(text, at)) --at;
    return at;
}

// Where the character after this place starts.
[[nodiscard]] constexpr std::size_t utf8_start_after(std::string_view text, std::size_t at) noexcept {
    if (at >= text.size()) return text.size();
    ++at;
    while (utf8_continues_at(text, at)) ++at;
    return at;
}

}  // namespace detail

template <class Tag>
class TextEntry {
public:
    using tag = Tag;

    constexpr explicit TextEntry(std::size_t most_bytes) : caption_{CheckedText{""}}, most_bytes_{most_bytes} {}
    TextEntry(const TextEntry&) = delete;
    TextEntry& operator=(const TextEntry&) = delete;
    constexpr TextEntry(TextEntry&&) = default;
    TextEntry& operator=(TextEntry&&) = delete;
    constexpr ~TextEntry() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return text_; }
    [[nodiscard]] constexpr std::size_t caret() const noexcept { return caret_; }

    // With the caret at the end. Text that could not be typed is refused, and what was there stays.
    [[nodiscard]] constexpr std::expected<void, Error> set_text(std::string_view text) {
        if (text.size() > most_bytes_) return make_error(ErrorCode::TextTooLong, "that text is too long for this entry");
        if (!text_can_show(text)) return make_error(ErrorCode::InvalidUtf8, "that text cannot be shown");
        text_ = text;
        caret_ = text_.size();
        changed();
        return {};
    }

    // True when it was a key this understands. With Ctrl, Alt or Super held a key is a command and types nothing. A character that will not fit is not cut, and text that could not show is not taken.
    constexpr bool take_key(const KeyEvent& event) {
        if (!event.pressed || event.modifiers.holds(Modifier::Ctrl) || event.modifiers.holds(Modifier::Alt) || event.modifiers.holds(Modifier::Super)) return false;
        if (event.keysym == Key::BackSpace) {
            if (caret_ == 0) return true;
            const std::size_t start = detail::utf8_start_before(text_, caret_);
            text_.erase(start, caret_ - start);
            caret_ = start;
            changed();
            return true;
        }
        if (event.keysym == Key::Delete) {
            if (caret_ == text_.size()) return true;
            text_.erase(caret_, detail::utf8_start_after(text_, caret_) - caret_);
            changed();
            return true;
        }
        if (event.keysym == Key::Left) return move_caret_to(detail::utf8_start_before(text_, caret_));
        if (event.keysym == Key::Right) return move_caret_to(detail::utf8_start_after(text_, caret_));
        if (event.keysym == Key::Home) return move_caret_to(0);
        if (event.keysym == Key::End) return move_caret_to(text_.size());
        const std::string_view typed = event.text.view();
        if (typed.empty() || !text_can_show(typed) || typed.size() > most_bytes_ - text_.size()) return false;
        text_.insert(caret_, typed);
        caret_ += typed.size();
        changed();
        return true;
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper& engine) {
        if (auto shaped = caption_.shape_with(engine); !shaped) return std::unexpected(std::move(shaped.error()));
        const Height row = engine.line_height() + theme::label_pad_y + theme::label_pad_y;
        bounds_ = area.intersect(Rect{area.origin(), Size{area.width(), row}});
        room_ = bounds_.inset(theme::label_pad_x, theme::label_pad_y);
        // Measured with the text before it, so the caret is where the next character will go.
        auto before = engine.shape(std::string_view{text_}.substr(0, caret_));
        if (before) {
            caret_across_ = before->size().width();
        } else if (detail::refuses_the_text(before.error().code)) {
            caret_across_ = Width{};
        } else {
            return std::unexpected(std::move(before.error()));
        }
        // Text longer than the box slides along it, just far enough to keep the caret in view.
        constexpr Width caret_width{1};
        if (caret_across_ < scrolled_) {
            scrolled_ = caret_across_;
        } else if (caret_across_ + caret_width > scrolled_ + room_.width()) {
            scrolled_ = caret_across_ + caret_width - room_.width();
        }
        return Rect{Point{area.left(), top_after(bounds_.bottom())}, Size{area.width(), area.height() - bounds_.height()}};
    }

    constexpr void paint(const TreeKey&, Canvas& canvas) {
        const Canvas box = canvas.clipped(bounds_);
        box.fill_rect(bounds_, theme::background);
        box.stroke_rect(bounds_, theme::frame_edge);
        const Canvas inner = canvas.clipped(room_);
        const Left text_left = (room_.left() + Width{}) - scrolled_;
        if (caption_.run()) inner.draw_text(Point{text_left, room_.top()}, *caption_.run(), theme::text);
        inner.fill_rect(Rect{Point{left_after(text_left + caret_across_), room_.top()}, Size{Width{1}, room_.height()}}, theme::text);
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const QueryDamage&) {
        return damaged_;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearDamage&) {
        damaged_ = false;
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }

    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}

private:
    constexpr bool move_caret_to(std::size_t at) noexcept {
        if (at != caret_) damaged_ = true;
        caret_ = at;
        return true;
    }

    // The text checked as it came in can show, and at most most_bytes_ is under the limit for any caption this holds, so it is not refused here: a refusal leaves the box showing what it did.
    constexpr void changed() {
        if (auto checked = CheckedText::check(text_); checked) caption_.replace(*checked);
        damaged_ = true;
    }

    detail::ShapedCaption caption_;
    std::string text_{};
    std::size_t most_bytes_;
    std::size_t caret_{};
    Rect bounds_{};
    Rect room_{};
    Width caret_across_{};
    Width scrolled_{};
    bool damaged_{};
};

}  // namespace jgui::inline v001
