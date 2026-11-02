// File: include/jCppGUI/v001/frame.hpp
// Purpose: The minimal window frame, because GNOME draws no title bars for Wayland clients: a title
//          strip with no text, a close box, and resize edges and corners, all as plain hit regions.
#pragma once

#include <cstdint>

#include "canvas.hpp"
#include "events.hpp"
#include "geometry.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

enum class ResizeEdge : std::uint8_t { Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight };

enum class FrameRegion : std::uint8_t { Outside, Client, Title, Close, Edge };

struct FrameHit {
    FrameRegion region{FrameRegion::Outside};
    ResizeEdge edge{ResizeEdge::Top};  // meaningful only for FrameRegion::Edge
};

class WindowFrame {
public:
    static constexpr std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
    static constexpr std::int32_t close_side = 16;

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

    // Each of these answers true when the frame must be painted again.
    constexpr bool pointer_moved(Point p, Size window) noexcept {
        return set_hover(hit(p, window).region == FrameRegion::Close);
    }
    constexpr bool pointer_left() noexcept {
        const bool disarmed = close_armed_;
        close_armed_ = false;
        return set_hover(false) || disarmed;
    }
    constexpr void arm_close() noexcept { close_armed_ = true; }
    [[nodiscard]] constexpr bool close_armed() const noexcept { return close_armed_; }

    // True when this release completes a click on the close box.
    constexpr bool release(Point p, Size window) noexcept {
        const bool armed = close_armed_;
        close_armed_ = false;
        return armed && hit(p, window).region == FrameRegion::Close;
    }

    constexpr void paint(const Canvas& canvas, Size window) const {
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

private:
    [[nodiscard]] static constexpr FrameHit edge_hit(ResizeEdge edge) noexcept {
        return FrameHit{FrameRegion::Edge, edge};
    }

    constexpr bool set_hover(bool hover) noexcept {
        const bool changed = hover != close_hover_;
        close_hover_ = hover;
        return changed;
    }

    bool close_hover_{};
    bool close_armed_{};
};

}  // namespace jgui::inline v001
