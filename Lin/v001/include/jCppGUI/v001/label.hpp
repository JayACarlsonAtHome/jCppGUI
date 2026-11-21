// File: include/jCppGUI/v001/label.hpp
// Purpose: Label<Tag>: one line of text. It takes a full-width row and leaves what is below it.
#pragma once

#include <expected>
#include <optional>
#include <string_view>
#include <utility>

#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag>
class Label {
public:
    using tag = Tag;

    constexpr explicit Label(CheckedText text) : caption_{text} {}
    Label(const Label&) = delete;
    Label& operator=(const Label&) = delete;
    constexpr Label(Label&&) = default;
    Label& operator=(Label&&) = delete;
    constexpr ~Label() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return caption_.text(); }
    // Why this text cannot be drawn, when a shaper refused it. Empty otherwise.
    [[nodiscard]] constexpr const std::optional<ErrorCode>& text_error() const noexcept { return caption_.refusal(); }

    // The new text is checked here. A refused one leaves the old text in place, with nothing to redraw.
    [[nodiscard]] constexpr std::expected<void, Error> set_text(std::string_view text) {
        auto checked = CheckedText::check(text);
        if (!checked) return std::unexpected(std::move(checked.error()));
        caption_.replace(*checked);
        damaged_ = true;
        return {};
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper& engine) {
        if (auto shaped = caption_.shape_with(engine); !shaped) return std::unexpected(std::move(shaped.error()));
        const Height row = caption_.size_with(engine).height() + theme::label_pad_y + theme::label_pad_y;
        bounds_ = area.intersect(Rect{area.origin(), Size{area.width(), row}});
        return Rect{Point{area.left(), top_after(bounds_.bottom())}, Size{area.width(), area.height() - bounds_.height()}};
    }

    constexpr void paint(const TreeKey&, Canvas& canvas) {
        if (!caption_.run()) return;
        const Canvas local = canvas.clipped(bounds_);
        const Point at{left_after(bounds_.left() + theme::label_pad_x), top_after(bounds_.top() + theme::label_pad_y)};
        local.draw_text(at, *caption_.run(), theme::text);
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
    detail::ShapedCaption caption_;
    Rect bounds_{};
    bool damaged_{};
};

template <class Tag>
[[nodiscard]] constexpr Label<Tag> label(CheckedText text) {
    return Label<Tag>{text};
}

}  // namespace jgui::inline v001
