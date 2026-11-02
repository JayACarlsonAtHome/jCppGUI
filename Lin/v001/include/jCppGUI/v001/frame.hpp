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
    ResizeEdge edge{ResizeEdge::TopSide};  // meaningful only for FrameRegion::Edge
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
    WindowFrame(const WindowFrame&) = delete;
    WindowFrame& operator=(const WindowFrame&) = delete;
    constexpr WindowFrame(WindowFrame&&) = default;
    WindowFrame& operator=(WindowFrame&&) = delete;
    constexpr ~WindowFrame() = default;

    [[nodiscard]] static constexpr Rect client_rect(Size window) noexcept {
        return Rect{Point{Left{edge_width}, Top{title_height}},
                    Size{window.width() - edge_across - edge_across, window.height() - title_down - edge_down}};
    }

    [[nodiscard]] static constexpr Rect close_box(Size window) noexcept {
        constexpr std::int32_t inset = (title_height - close_side) / 2;
        const Rect whole{Point{}, window};
        return Rect{Point{whole.right() - Width{close_side + inset + edge_width}, Top{inset}}, Size{close_across, close_down}};
    }

    [[nodiscard]] static constexpr FrameHit hit(Point p, Size window) noexcept {
        const Rect whole{Point{}, window};
        if (!whole.contains(p)) return FrameHit{};
        const bool left = p.left() < Left{edge_width};
        const bool right = p.left() >= whole.right() - edge_across;
        const bool top = p.top() < Top{edge_width};
        const bool bottom = p.top() >= whole.bottom() - edge_down;
        const bool near_left = p.left() < Left{corner_length};
        const bool near_right = p.left() >= whole.right() - Width{corner_length};
        const bool near_top = p.top() < Top{corner_length};
        const bool near_bottom = p.top() >= whole.bottom() - Height{corner_length};
        if ((top && near_left) || (left && near_top)) return edge_hit(ResizeEdge::TopLeft);
        if ((top && near_right) || (right && near_top)) return edge_hit(ResizeEdge::TopRight);
        if ((bottom && near_left) || (left && near_bottom)) return edge_hit(ResizeEdge::BottomLeft);
        if ((bottom && near_right) || (right && near_bottom)) return edge_hit(ResizeEdge::BottomRight);
        if (top) return edge_hit(ResizeEdge::TopSide);
        if (bottom) return edge_hit(ResizeEdge::BottomSide);
        if (left) return edge_hit(ResizeEdge::LeftSide);
        if (right) return edge_hit(ResizeEdge::RightSide);
        if (close_box(window).contains(p)) return FrameHit{FrameRegion::Close};
        if (p.top() < Top{title_height}) return FrameHit{FrameRegion::Title};
        return FrameHit{FrameRegion::Client};
    }

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr bool close_hovered() const noexcept { return close_hover_; }
    [[nodiscard]] constexpr bool close_armed() const noexcept { return close_armed_; }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        bounds_ = area;
        if (auto used = detail::layout_in_order(children_, key, client_rect(area.size()), text); !used)
            return std::unexpected(std::move(used.error()));
        return Rect{Point{area.left(), top_after(area.bottom())}, Size{area.width(), Height{}}};
    }

    // The content first and clipped to the client area, so that the frame is always on top.
    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        Canvas inside = canvas.clipped(client_rect(bounds_.size()));
        detail::paint_in_order(children_, key, inside);
        paint_frame(canvas, bounds_.size());
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerPress& event) {
        const FrameHit where = hit(event.position, ui.size());
        if (where.region == FrameRegion::Client || event.button != MouseButton::Left) {
            detail::notify_all(children_, key, ui, ClearFocus{});
            return detail::deliver(children_, key, ui, event);
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
            default:
                return false;
        }
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerRelease& event) {
        if (close_armed_) {
            damaged_ = true;
            if (hit(event.position, ui.size()).region == FrameRegion::Close) ui.request_close();
        }
        close_armed_ = false;
        return detail::deliver(children_, key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        set_close_hover(hit(event.position, ui.size()).region == FrameRegion::Close);
        if (event.captured || client_rect(ui.size()).contains(event.position))
            return detail::deliver(children_, key, ui, event);
        detail::notify_all(children_, key, ui, PointerLeave{});
        return false;
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const PointerLeave& event) {
        if (close_armed_) damaged_ = true;
        close_armed_ = false;
        set_close_hover(false);
        detail::notify_all(children_, key, ui, event);
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return detail::deliver(children_, key, ui, event) || damaged_;
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        damaged_ = false;
        detail::notify_all(children_, key, ui, event);
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
    // The metrics are plain lengths, because one number can serve both axes. Each use here names its axis.
    static constexpr Width edge_across{Metrics::edge_width};
    static constexpr Height edge_down{Metrics::edge_width};
    static constexpr Height title_down{Metrics::title_height};
    static constexpr Width close_across{Metrics::close_side};
    static constexpr Height close_down{Metrics::close_side};

    [[nodiscard]] static constexpr FrameHit edge_hit(ResizeEdge edge) noexcept {
        return FrameHit{FrameRegion::Edge, edge};
    }

    constexpr void set_close_hover(bool hover) noexcept {
        if (hover != close_hover_) damaged_ = true;
        close_hover_ = hover;
    }

    constexpr void paint_frame(const Canvas& canvas, Size window) const {
        const Rect whole{Point{}, window};
        canvas.fill_rect(Rect{Point{}, Size{window.width(), title_down}}, theme::frame_strip);
        Color fill = theme::close_normal;
        if (close_hover_) fill = close_armed_ ? theme::close_pressed : theme::close_hover;
        const Rect box = close_box(window);
        canvas.fill_rect(box, fill);
        canvas.stroke_rect(box, theme::frame_edge);
        const Size one_pixel{Width{1}, Height{1}};
        for (std::int32_t i = 4; i < close_side - 4; ++i) {
            const Top row = top_after(box.top() + Height{i});
            canvas.fill_rect(Rect{Point{left_after(box.left() + Width{i}), row}, one_pixel}, theme::close_mark);
            canvas.fill_rect(Rect{Point{box.right() - Width{i + 1}, row}, one_pixel}, theme::close_mark);
        }
        canvas.fill_rect(Rect{Point{Left{}, Top{title_height}}, Size{edge_across, window.height()}}, theme::frame_strip);
        canvas.fill_rect(Rect{Point{whole.right() - edge_across, Top{title_height}}, Size{edge_across, window.height()}}, theme::frame_strip);
        canvas.fill_rect(Rect{Point{Left{}, whole.bottom() - edge_down}, Size{window.width(), edge_down}}, theme::frame_strip);
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
