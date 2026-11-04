// File: include/jCppGUI/v001/two_panes.hpp
// Purpose: TwoPanes<Tag, LeftPane, RightPane>: two widgets side by side, each with half of what it is offered, and the left one with any extra pixel. They share
//          nothing: each has its own type, its own state and its own events, and this only gives them their room.
#pragma once

#include <expected>
#include <tuple>
#include <utility>

#include "panels.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag, Widget LeftPane, Widget RightPane>
class TwoPanes {
public:
    using tag = Tag;
    using children_type = std::tuple<LeftPane, RightPane>;

    constexpr TwoPanes(LeftPane left, RightPane right) : children_{std::move(left), std::move(right)} {}
    TwoPanes(const TwoPanes&) = delete;
    TwoPanes& operator=(const TwoPanes&) = delete;
    constexpr TwoPanes(TwoPanes&&) = default;
    TwoPanes& operator=(TwoPanes&&) = delete;
    constexpr ~TwoPanes() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }

    // Takes all that is offered and leaves nothing below.
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        bounds_ = area;
        if (auto placed = std::get<0>(children_).layout(key, left_half_of(area), text); !placed) return std::unexpected(std::move(placed.error()));
        if (auto placed = std::get<1>(children_).layout(key, right_half_of(area), text); !placed) return std::unexpected(std::move(placed.error()));
        return Rect{Point{area.left(), top_after(area.bottom())}, Size{area.width(), Height{}}};
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) { detail::paint_in_order(children_, key, canvas); }

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

template <class Tag, class LeftPane, class RightPane>
[[nodiscard]] constexpr TwoPanes<Tag, LeftPane, RightPane> two_panes(LeftPane left, RightPane right) {
    return TwoPanes<Tag, LeftPane, RightPane>{std::move(left), std::move(right)};
}

}  // namespace jgui::inline v001
