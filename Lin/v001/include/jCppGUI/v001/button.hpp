// File: include/jCppGUI/v001/button.hpp
// Purpose: Button<Tag, OnClick>: a text caption with normal, hover and pressed states. It keeps its
//          lambda's real type, and a click fires only if press and release both land on it.
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag, class OnClick>
class Button {
public:
    using tag = Tag;

    constexpr Button(std::string_view caption, OnClick on_click) : caption_{caption}, on_click_{std::move(on_click)} {}
    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;
    constexpr Button(Button&&) = default;
    Button& operator=(Button&&) = delete;
    constexpr ~Button() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr bool hovered() const noexcept { return hovered_; }
    [[nodiscard]] constexpr bool pressed() const noexcept { return pressed_; }
    [[nodiscard]] constexpr bool focused() const noexcept { return focused_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper& text) {
        if (!run_) {
            auto shaped = text.shape(caption_);
            if (!shaped) return std::unexpected(std::move(shaped.error()));
            run_ = std::move(*shaped);
        }
        const Width wanted_width = run_->size().width() + theme::button_pad_x + theme::button_pad_x;
        const Height wanted_height = run_->size().height() + theme::button_pad_y + theme::button_pad_y;
        bounds_ = area.intersect(Rect{area.origin(), Size{wanted_width, wanted_height}});
        return Rect{Point{left_after(bounds_.right()), area.top()}, Size{area.width() - bounds_.width(), area.height()}};
    }

    constexpr void paint(const TreeKey&, Canvas& canvas) {
        const Canvas local = canvas.clipped(bounds_);
        Color fill = theme::button_normal;
        if (pressed_ && hovered_) {
            fill = theme::button_pressed;
        } else if (hovered_) {
            fill = theme::button_hover;
        }
        local.fill_rect(bounds_, fill);
        local.stroke_rect(bounds_, focused_ ? theme::focus_edge : theme::button_edge);
        if (run_) {
            const Point at{left_after(bounds_.left() + theme::button_pad_x), top_after(bounds_.top() + theme::button_pad_y)};
            local.draw_text(at, *run_, theme::text);
        }
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerPress& event) {
        if (event.button != MouseButton::Left || !bounds_.contains(event.position)) return false;
        pressed_ = true;
        hovered_ = true;
        focused_ = true;
        damaged_ = true;
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const PointerRelease& event) {
        if (!pressed_ || event.button != MouseButton::Left) return false;
        pressed_ = false;
        damaged_ = true;
        const bool inside = bounds_.contains(event.position);
        hovered_ = inside;
        if (inside) on_click_(ui);
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const PointerMove& event) {
        const bool inside = bounds_.contains(event.position);
        const bool hover = inside && (pressed_ || !event.captured);
        if (hover != hovered_) {
            hovered_ = hover;
            damaged_ = true;
        }
        return hover;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const PointerLeave&) {
        if (hovered_ || pressed_) damaged_ = true;
        hovered_ = false;
        pressed_ = false;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U& ui, const KeyEvent& event) {
        if (!focused_ || !event.pressed) return false;
        if (event.keysym != Key::Return && event.keysym != Key::Space) return false;
        on_click_(ui);
        return true;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearFocus&) {
        if (focused_) damaged_ = true;
        focused_ = false;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const HoldsCapture&) {
        return pressed_;
    }

    template <class U>
    constexpr bool handle(const TreeKey&, U&, const QueryDamage&) {
        return damaged_;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearDamage&) {
        damaged_ = false;
    }

    template <class U>
    constexpr void notify(const TreeKey&, U&, const CollectDead&) {}

private:
    std::string caption_;
    OnClick on_click_;
    std::optional<TextRun> run_{};
    Rect bounds_{};
    bool hovered_{};
    bool pressed_{};
    bool focused_{};
    bool damaged_{};
};

// The tag is given; the callback's type is deduced and becomes part of the button's type.
template <class Tag, class OnClick>
[[nodiscard]] constexpr Button<Tag, OnClick> button(std::string_view caption, OnClick on_click) {
    return Button<Tag, OnClick>{caption, std::move(on_click)};
}

}  // namespace jgui::inline v001
