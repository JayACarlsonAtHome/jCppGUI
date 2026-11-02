// File: include/jCppGUI/v001/frame.hpp
// Purpose: The minimal window frame, because GNOME draws no title bars for Wayland clients: a title
//          strip with no text, a close box, and resize edges and corners, all as plain hit regions.
//          It is a widget in the tree, sizes come from its Metrics type, and it holds the window's content.
#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <tuple>
#include <type_traits>
#include <utility>

#include "canvas.hpp"
#include "events.hpp"
#include "geometry.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

enum class FrameRegion : std::uint8_t { Outside, Client, Title, Close, Edge };

struct FrameHit {
    FrameRegion region{FrameRegion::Outside};
    ResizeEdge edge{ResizeEdge::Top};  // meaningful only for FrameRegion::Edge
};

// The frame's sizes, as compile-time values of one type. They must be positive, the close box must
// fit in the title strip, and a corner must be at least as long as the edge is wide.
template <class M>
concept FrameMetrics = requires {
    requires std::same_as<decltype(M::edge_width), const std::int32_t>;
    requires std::same_as<decltype(M::corner_length), const std::int32_t>;
    requires std::same_as<decltype(M::title_height), const std::int32_t>;
    requires std::same_as<decltype(M::close_side), const std::int32_t>;
    typename std::integral_constant<std::int32_t, M::edge_width>;
    typename std::integral_constant<std::int32_t, M::corner_length>;
    typename std::integral_constant<std::int32_t, M::title_height>;
    typename std::integral_constant<std::int32_t, M::close_side>;
    requires M::edge_width > 0 && M::corner_length > 0 && M::title_height > 0 && M::close_side > 0;
    requires M::close_side <= M::title_height && M::edge_width <= M::corner_length;
};

struct DefaultFrameMetrics {
    static constexpr std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;
};

// The tag of a window's frame. A second frame in one window is a duplicate tag.
struct WindowFrameTag;

// Takes the whole window and holds the content in the client area. A frame belongs directly under the
// Window, whose origin is the window's corner, and what it asks of the window system goes through
// the Window's requests. It does not hold the pointer while the close box is armed, so widgets keep
// seeing ordinary motion during that press.
template <FrameMetrics Metrics, Widget... Children>
class WindowFrame {
public:
    using tag = WindowFrameTag;
    using children_type = std::tuple<Children...>;

    static constexpr std::int32_t edge_width = Metrics::edge_width;
    static constexpr std::int32_t corner_length = Metrics::corner_length;
    static constexpr std::int32_t title_height = Metrics::title_height;
    static constexpr std::int32_t close_side = Metrics::close_side;

    constexpr explicit WindowFrame(Children... children) : children_{std::move(children)...} {}

    [[nodiscard]] static constexpr Rect client_rect(Size window) noexcept {
        return Rect{edge_width, title_height, sat_sub(window.width(), sat_mul(edge_width, 2)),
                    sat_sub(window.height(), sat_add(title_height, edge_width))};
    }

    [[nodiscard]] static constexpr Rect close_box(Size window) noexcept {
        const std::int32_t inset = (title_height - close_side) / 2;
        return Rect{sat_sub(window.width(), sat_add(close_side, sat_add(inset, edge_width))), inset, close_side,
                    close_side};
    }

    [[nodiscard]] static constexpr FrameHit hit(Point p, Size window) noexcept {
        if (!Rect{Point{}, window}.contains(p)) return FrameHit{};
        const std::int32_t w = window.width();
        const std::int32_t h = window.height();
        const bool left = p.x < edge_width;
        const bool right = p.x >= sat_sub(w, edge_width);
        const bool top = p.y < edge_width;
        const bool bottom = p.y >= sat_sub(h, edge_width);
        const bool near_left = p.x < corner_length;
        const bool near_right = p.x >= sat_sub(w, corner_length);
        const bool near_top = p.y < corner_length;
        const bool near_bottom = p.y >= sat_sub(h, corner_length);
        if ((top && near_left) || (left && near_top)) return edge_hit(ResizeEdge::TopLeft);
        if ((top && near_right) || (right && near_top)) return edge_hit(ResizeEdge::TopRight);
        if ((bottom && near_left) || (left && near_bottom)) return edge_hit(ResizeEdge::BottomLeft);
        if ((bottom && near_right) || (right && near_bottom)) return edge_hit(ResizeEdge::BottomRight);
        if (top) return edge_hit(ResizeEdge::Top);
        if (bottom) return edge_hit(ResizeEdge::Bottom);
        if (left) return edge_hit(ResizeEdge::Left);
        if (right) return edge_hit(ResizeEdge::Right);
        if (close_box(window).contains(p)) return FrameHit{FrameRegion::Close};
        if (p.y < title_height) return FrameHit{FrameRegion::Title};
        return FrameHit{FrameRegion::Client};
    }

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool close_hovered() const noexcept { return close_hover_; }
    [[nodiscard]] constexpr bool close_armed() const noexcept { return close_armed_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(Rect area, const Shaper& text) {
        bounds_ = area;
        auto used = detail::layout_in_order(children_, client_rect(area.size()), text);
        if (!used) return used;
        return Rect{area.x(), area.bottom(), area.width(), 0};
    }

    // The content first and clipped to the client area, so that the frame is always on top.
    constexpr void paint(Canvas& canvas) {
        Canvas inside = canvas.clipped(client_rect(bounds_.size()));
        detail::paint_in_order(children_, inside);
        paint_frame(canvas, bounds_.size());
    }

    template <class U>
    constexpr bool handle(U& ui, const PointerPress& event) {
        const FrameHit where = hit(event.position, ui.size());
        if (where.region == FrameRegion::Client || event.button != MouseButton::Left) {
            static_cast<void>(detail::broadcast(children_, ui, ClearFocus{}));
            return detail::deliver(children_, ui, event);
        }
        switch (where.region) {
            case FrameRegion::Close:
                close_armed_ = true;
                damaged_ = true;
                return true;
            case FrameRegion::Title:
                ui.request_move(event.serial);
                return true;
            case FrameRegion::Edge:
                ui.request_resize(event.serial, where.edge);
                return true;
            case FrameRegion::Outside:
            case FrameRegion::Client:
                break;
        }
        return false;
    }

    template <class U>
    constexpr bool handle(U& ui, const PointerRelease& event) {
        if (close_armed_) {
            damaged_ = true;
            if (hit(event.position, ui.size()).region == FrameRegion::Close) ui.request_close();
        }
        close_armed_ = false;
        return detail::deliver(children_, ui, event);
    }

    template <class U>
    constexpr bool handle(U& ui, const PointerMove& event) {
        set_close_hover(hit(event.position, ui.size()).region == FrameRegion::Close);
        if (event.captured || client_rect(ui.size()).contains(event.position))
            return detail::deliver(children_, ui, event);
        static_cast<void>(detail::broadcast(children_, ui, PointerLeave{}));
        return false;
    }

    template <class U>
    constexpr bool handle(U& ui, const PointerLeave& event) {
        if (close_armed_) damaged_ = true;
        close_armed_ = false;
        set_close_hover(false);
        return detail::deliver(children_, ui, event);
    }

    template <class U>
    constexpr bool handle(U& ui, const QueryDamage& event) {
        return detail::deliver(children_, ui, event) || damaged_;
    }

    template <class U>
    constexpr bool handle(U& ui, const ClearDamage& event) {
        damaged_ = false;
        return detail::deliver(children_, ui, event);
    }

    template <class U, class E>
    constexpr bool handle(U& ui, const E& event) {
        return detail::deliver(children_, ui, event);
    }

private:
    [[nodiscard]] static constexpr FrameHit edge_hit(ResizeEdge edge) noexcept {
        return FrameHit{FrameRegion::Edge, edge};
    }

    constexpr void set_close_hover(bool hover) noexcept {
        if (hover != close_hover_) damaged_ = true;
        close_hover_ = hover;
    }

    constexpr void paint_frame(const Canvas& canvas, Size window) const {
        const Rect whole{Point{}, window};
        canvas.fill_rect(Rect{0, 0, window.width(), title_height}, theme::frame_strip);
        Color fill = theme::close_normal;
        if (close_hover_) fill = close_armed_ ? theme::close_pressed : theme::close_hover;
        const Rect box = close_box(window);
        canvas.fill_rect(box, fill);
        canvas.stroke_rect(box, theme::frame_edge);
        for (std::int32_t i = 4; i < close_side - 4; ++i) {
            canvas.fill_rect(Rect{sat_add(box.x(), i), sat_add(box.y(), i), 1, 1}, theme::close_mark);
            canvas.fill_rect(Rect{sat_add(box.x(), close_side - 1 - i), sat_add(box.y(), i), 1, 1}, theme::close_mark);
        }
        canvas.fill_rect(Rect{0, title_height, edge_width, window.height()}, theme::frame_strip);
        canvas.fill_rect(Rect{sat_sub(window.width(), edge_width), title_height, edge_width, window.height()},
                         theme::frame_strip);
        canvas.fill_rect(Rect{0, sat_sub(window.height(), edge_width), window.width(), edge_width}, theme::frame_strip);
        canvas.stroke_rect(whole, theme::frame_edge);
    }

    children_type children_;
    Rect bounds_{};
    bool close_hover_{};
    bool close_armed_{};
    bool damaged_{};
};

// The metrics are given or left at the default; the content's types are deduced.
template <FrameMetrics Metrics = DefaultFrameMetrics, class... Children>
[[nodiscard]] constexpr WindowFrame<Metrics, Children...> window_frame(Children... children) {
    return WindowFrame<Metrics, Children...>{std::move(children)...};
}

}  // namespace jgui::inline v001
