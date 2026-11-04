// File: include/jCppGUI/v001/popup_layer.hpp
// Purpose: PopupLayer<Tag, Popup, Content...>. The toolkit has one window and no xdg_popup, so a popup is drawn inside the window, over everything else, and takes the pointer and the keys until it is closed.
//          A layer can hold another as its content, for a window with more than one popup. jgui.hpp does not include it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <tuple>
#include <utility>

#include "panels.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

// The content is placed as it would be with no popup, and the popup is offered all the room. Being last, the popup is painted last and is asked first about a press, motion and scrolling. The keys are
// sent to everything by default, so here they stop at an open popup.
template <class Tag, Widget Popup, Widget... Content>
class PopupLayer {
public:
    using tag = Tag;
    using children_type = std::tuple<Content..., Popup>;

    constexpr explicit PopupLayer(Content... content, Popup popup) : children_{std::move(content)..., std::move(popup)} {}
    PopupLayer(const PopupLayer&) = delete;
    PopupLayer& operator=(const PopupLayer&) = delete;
    constexpr PopupLayer(PopupLayer&&) = default;
    PopupLayer& operator=(PopupLayer&&) = delete;
    constexpr ~PopupLayer() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        bounds_ = area;
        std::expected<Rect, Error> remaining{area};
        const auto place = [&](auto& child) {
            if (remaining) remaining = child.layout(key, *remaining, text);
        };
        [&]<std::size_t... I>(std::index_sequence<I...>) { (place(std::get<I>(children_)), ...); }(std::index_sequence_for<Content...>{});
        if (!remaining) return std::unexpected(std::move(remaining.error()));
        return popup().layout(key, area, text);
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) { detail::paint_in_order(children_, key, canvas); }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const KeyEvent& event) {
        return popup().is_open() ? popup().handle(key, ui, event) : detail::deliver(children_, key, ui, event);
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
    [[nodiscard]] constexpr Popup& popup() noexcept { return std::get<Popup>(children_); }

    children_type children_;
    Rect bounds_{};
};

template <class Tag, class Popup, class... Content>
[[nodiscard]] constexpr PopupLayer<Tag, Popup, Content...> popup_layer(Popup popup, Content... content) {
    return PopupLayer<Tag, Popup, Content...>{std::move(content)..., std::move(popup)};
}

// A box of about this size in the middle of the room, smaller if the room is.
[[nodiscard]] constexpr Rect popup_box_in(Rect area, Width across, Height down) noexcept {
    const Size size{std::min(across, area.width()), std::min(down, area.height())};
    return Rect{Point{left_after(area.left() + smaller_half_of(area.width() - size.width())), top_after(area.top() + smaller_half_of(area.height() - size.height()))}, size};
}

}  // namespace jgui::inline v001
