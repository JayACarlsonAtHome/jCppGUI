// File: include/jCppGUI/v001/menu_bar.hpp
// Purpose: MenuBar<Children...>: a full-width strip that lays out its children from the left, sized
//          from their own measured text, and leaves what is below it.
#pragma once

#include <expected>
#include <tuple>
#include <utility>

#include "fit.hpp"
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
    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;
    constexpr MenuBar(MenuBar&&) = default;
    MenuBar& operator=(MenuBar&&) = delete;
    constexpr ~MenuBar() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        const Height strip = text.line_height() + theme::button_pad_y + theme::button_pad_y + theme::bar_pad_y + theme::bar_pad_y;
        bounds_ = area.intersect(Rect{area.origin(), Size{area.width(), strip}});
        const Width end_x = padding_of(text).bar_x;
        if (auto used = detail::layout_in_order(children_, key, bounds_.inset(end_x, theme::bar_pad_y), text); !used)
            return std::unexpected(std::move(used.error()));
        std::apply([&](const auto&... child) { report_row(text, bounds_.width() - end_x - end_x, child...); }, children_);
        return Rect{Point{area.left(), top_after(bounds_.bottom())}, Size{area.width(), area.height() - bounds_.height()}};
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        Canvas local = canvas.clipped(bounds_);
        local.fill_rect(bounds_, theme::bar);
        constexpr Height one_down{1};
        local.fill_rect(Rect{Point{bounds_.left(), bounds_.bottom() - one_down}, Size{bounds_.width(), one_down}}, theme::bar_edge);
        detail::paint_in_order(children_, key, local);
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey& key, U& ui, const E& event) {
        return detail::deliver(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey& key, U& ui, const E& event) {
        detail::notify_all(children_, key, ui, event);
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
