// File: include/jCppGUI/v001/scroll_bar.hpp
// Purpose: Scrolling along one axis. The arithmetic is a set of small functions over one kind of length, and ScrollBar<Axis>
//          holds one offset, keeps it clamped and draws its track and thumb. It is a part a widget holds and not a widget:
//          the widget decides which events reach it and when to paint. Everything here costs the same for any content size.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

#include "canvas.hpp"
#include "events.hpp"
#include "geometry.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

// The offset, kept to what the content and the view allow.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> clamped_scroll_offset(Extent<Tag> offset, Extent<Tag> content, Extent<Tag> view) noexcept {
    return std::min(offset, content - view);
}

// How far an amount of scrolling goes, with a step this long, in its own direction.
template <class Tag, class AmountTag>
[[nodiscard]] constexpr Extent<Tag> scroll_distance(ScrollAmount<AmountTag> amount, Extent<Tag> step) noexcept {
    const std::int32_t steps = amount.steps();
    const std::int32_t pixels = amount.pixels();
    return repeated_extent(step, static_cast<std::size_t>(std::max(steps, -steps))) + Extent<Tag>{std::max(pixels, -pixels)};
}

// The offset after an amount of scrolling. Not clamped.
template <class Tag, class AmountTag>
[[nodiscard]] constexpr Extent<Tag> offset_after_scroll(Extent<Tag> offset, ScrollAmount<AmountTag> amount, Extent<Tag> step) noexcept {
    const Extent<Tag> distance = scroll_distance(amount, step);
    return amount.steps() < 0 || amount.pixels() < 0 ? offset - distance : offset + distance;
}

// The nearest offset that shows a piece of the content, which starts this far in and is this long. A piece longer than
// the view shows its start. Not clamped.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> offset_to_reveal(Extent<Tag> offset, Extent<Tag> start, Extent<Tag> length, Extent<Tag> view) noexcept {
    if (start < offset || length >= view) return start;
    const Extent<Tag> least_offset = (start + length) - view;
    return std::max(offset, least_offset);
}

// A thumb's length on its track: its share of the track, but at least the least, and never more than the track. None
// when there is nothing to scroll.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> scroll_thumb_length(Extent<Tag> content, Extent<Tag> view, Extent<Tag> track, Extent<Tag> least) noexcept {
    if (content <= view) return Extent<Tag>{};
    return std::min(std::max(rescaled_extent(view, content, track), least), track);
}

// How far along the track a thumb starts, for an offset.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> scroll_thumb_start(Extent<Tag> offset, Extent<Tag> content, Extent<Tag> view, Extent<Tag> track, Extent<Tag> thumb) noexcept {
    return rescaled_extent(offset, content - view, track - thumb);
}

// The offset for a thumb that starts this far along its track. Clamped: a thumb past the end of the track means the last offset.
template <class Tag>
[[nodiscard]] constexpr Extent<Tag> offset_for_thumb_start(Extent<Tag> start, Extent<Tag> content, Extent<Tag> view, Extent<Tag> track, Extent<Tag> thumb) noexcept {
    const Extent<Tag> room = track - thumb;
    return rescaled_extent(std::min(start, room), room, content - view);
}

struct ScrollBarsNeeded {
    bool down{};
    bool across{};
    friend constexpr bool operator==(const ScrollBarsNeeded&, const ScrollBarsNeeded&) = default;
};

// Which bars some content needs in the room given. A bar takes room from the other axis, which can make the other bar
// needed, so each is looked at again with the other in place.
[[nodiscard]] constexpr ScrollBarsNeeded scroll_bars_needed(Size content, Size room, Width bar_across, Height bar_down) noexcept {
    bool down = content.height() > room.height();
    const bool across = content.width() > (down ? room.width() - bar_across : room.width());
    down = content.height() > (across ? room.height() - bar_down : room.height());
    return ScrollBarsNeeded{down, across || content.width() > (down ? room.width() - bar_across : room.width())};
}

// A scroll bar that runs from top to bottom.
struct DownAxis {
    using Length = Height;
    using Amount = ScrollDown;
    [[nodiscard]] static constexpr Height length_of(const Rect& area) noexcept { return area.height(); }
    [[nodiscard]] static constexpr Height distance_into(const Rect& area, Point at) noexcept { return (at.top() + Height{}) - area.top(); }
    [[nodiscard]] static constexpr Rect piece_of(const Rect& area, Height start, Height length) noexcept {
        return Rect{Point{area.left(), top_after(area.top() + start)}, Size{area.width(), length}};
    }
};

// A scroll bar that runs from left to right.
struct AcrossAxis {
    using Length = Width;
    using Amount = ScrollAcross;
    [[nodiscard]] static constexpr Width length_of(const Rect& area) noexcept { return area.width(); }
    [[nodiscard]] static constexpr Width distance_into(const Rect& area, Point at) noexcept { return (at.left() + Width{}) - area.left(); }
    [[nodiscard]] static constexpr Rect piece_of(const Rect& area, Width start, Width length) noexcept {
        return Rect{Point{left_after(area.left() + start), area.top()}, Size{length, area.height()}};
    }
};

// What a press on a bar did.
enum class ScrollBarPress : std::uint8_t { Missed, Grabbed, Paged, PagedNowhere };

inline constexpr std::int32_t scroll_thumb_least = 16;

// One offset and the bar that shows it. Whatever changes the size or the offset keeps the offset clamped. Each call that
// moves the offset answers true when it really moved, so the caller knows whether there is anything to draw.
template <class Axis>
class ScrollBar {
public:
    using Length = typename Axis::Length;

    // The track is where the bar is. An empty one shows nothing and takes no presses, and the offset still works.
    constexpr void place(const Rect& track, Length content, Length view) noexcept {
        track_ = track;
        content_ = content;
        view_ = view;
        offset_ = clamped_scroll_offset(offset_, content_, view_);
    }

    [[nodiscard]] constexpr Length offset() const noexcept { return offset_; }
    [[nodiscard]] constexpr Length view() const noexcept { return view_; }
    [[nodiscard]] constexpr bool grabbed() const noexcept { return grab_.has_value(); }

    [[nodiscard]] constexpr Rect thumb() const noexcept {
        const Length track = Axis::length_of(track_);
        const Length length = thumb_length();
        return Axis::piece_of(track_, scroll_thumb_start(offset_, content_, view_, track, length), length);
    }

    constexpr bool scroll_to(Length offset) noexcept {
        const Length clamped = clamped_scroll_offset(offset, content_, view_);
        const bool moved = clamped != offset_;
        offset_ = clamped;
        return moved;
    }

    constexpr bool scroll_by(typename Axis::Amount amount, Length step) noexcept { return scroll_to(offset_after_scroll(offset_, amount, step)); }

    constexpr bool reveal(Length start, Length length) noexcept { return scroll_to(offset_to_reveal(offset_, start, length, view_)); }

    // On the thumb, this holds it, from where it was taken. In the rest of the track, the offset moves one view that way.
    constexpr ScrollBarPress press(Point at) noexcept {
        if (track_.empty() || !track_.contains(at)) return ScrollBarPress::Missed;
        const Rect held = thumb();
        if (held.contains(at)) {
            grab_ = Axis::distance_into(held, at);
            return ScrollBarPress::Grabbed;
        }
        const bool back = Axis::distance_into(track_, at) < Axis::distance_into(track_, held.origin());
        return scroll_to(back ? offset_ - view_ : offset_ + view_) ? ScrollBarPress::Paged : ScrollBarPress::PagedNowhere;
    }

    constexpr bool drag_to(Point at) noexcept {
        if (!grab_) return false;
        const Length start = Axis::distance_into(track_, at) - *grab_;
        return scroll_to(offset_for_thumb_start(start, content_, view_, Axis::length_of(track_), thumb_length()));
    }

    constexpr void release() noexcept { grab_.reset(); }

    constexpr void paint(const Canvas& canvas) const {
        if (track_.empty() || thumb_length() == Length{}) return;
        const Canvas local = canvas.clipped(track_);
        local.fill_rect(track_, theme::bar);
        local.fill_rect(thumb(), theme::button_edge);
    }

private:
    [[nodiscard]] constexpr Length thumb_length() const noexcept {
        return scroll_thumb_length(content_, view_, Axis::length_of(track_), Length{scroll_thumb_least});
    }

    Rect track_{};
    Length content_{};
    Length view_{};
    Length offset_{};
    std::optional<Length> grab_{};
};

}  // namespace jgui::inline v001
