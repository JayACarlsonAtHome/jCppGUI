// File: include/jCppGUI/v001/menu_bar.hpp
// Purpose: MenuBar<Children...>: a full-width strip that lays out its children from the left, sized
//          from their own measured text, and leaves what is below it.
#pragma once

#include <expected>
#include <tuple>
#include <utility>

#include "widget.hpp"

namespace jgui::inline v001 {

// The tag of a window's menu bar. A second bar in one window is a duplicate tag.
struct MenuBarTag;

template <Widget... Children>
class MenuBar {
public:
    using tag = MenuBarTag;
    using children_type = std::tuple<Children...>;

    constexpr explicit MenuBar(Children... children) : children_{std::move(children)...} {}

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper& text) {
        const std::int32_t inner = sat_add(text.line_height(), sat_mul(theme::button_pad_y, 2));
        const std::int32_t strip = sat_add(inner, sat_mul(theme::bar_pad, 2));
        bounds_ = area.intersect(Rect{area.origin(), Size{area.width(), strip}});
        if (auto used = detail::layout_in_order(children_, bounds_.inset(theme::bar_pad), text); !used)
            return std::unexpected(std::move(used.error()));
        return Rect{area.x(), bounds_.bottom(), area.width(), sat_sub(area.height(), bounds_.height())};
    }

    constexpr void paint(Canvas& canvas) {
        const Canvas local = canvas.clipped(bounds_);
        local.fill_rect(bounds_, theme::bar);
        local.fill_rect(Rect{bounds_.x(), sat_sub(bounds_.bottom(), 1), bounds_.width(), 1}, theme::bar_edge);
        Canvas inner{local};
        detail::paint_in_order(children_, inner);
    }

    template <class U, class E>
    constexpr bool handle(U& ui, const E& event) {
        return detail::deliver(children_, ui, event);
    }

    template <class U, class E>
    constexpr void notify(U& ui, const E& event) {
        detail::notify_all(children_, ui, event);
    }

private:
    children_type children_;
    Rect bounds_{};
};

template <class... Children>
[[nodiscard]] constexpr MenuBar<Children...> menu_bar(Children... children) {
    return MenuBar<Children...>{std::move(children)...};
}

}  // namespace jgui::inline v001
