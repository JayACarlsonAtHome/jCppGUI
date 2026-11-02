// File: include/jCppGUI/v001/widget.hpp
// Purpose: The Widget concept, which is the only thing widgets have in common. There is no base
//          class. Also the traversal helpers that deliver an event over a tuple of widgets.
#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <string_view>
#include <tuple>
#include <utility>

#include "canvas.hpp"
#include "error.hpp"
#include "events.hpp"
#include "geometry.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

namespace theme {
inline constexpr Color background{0xFFFFFFFFU};
inline constexpr Color text{0xFF101010U};
inline constexpr Color bar{0xFFF0F0F0U};
inline constexpr Color bar_edge{0xFFB0B0B0U};
inline constexpr Color button_normal{0xFFE4E4E4U};
inline constexpr Color button_hover{0xFFCCD8EEU};
inline constexpr Color button_pressed{0xFF9FB4D8U};
inline constexpr Color button_edge{0xFF808080U};
inline constexpr Color focus_edge{0xFF2050A0U};
inline constexpr Color frame_strip{0xFFD8D8D8U};
inline constexpr Color frame_edge{0xFF707070U};
inline constexpr Color close_normal{0xFFC8C8C8U};
inline constexpr Color close_hover{0xFFE08080U};
inline constexpr Color close_pressed{0xFFC04040U};
inline constexpr Color close_mark{0xFF202020U};
inline constexpr std::int32_t button_pad_x = 10;
inline constexpr std::int32_t button_pad_y = 3;
inline constexpr std::int32_t bar_pad = 2;
inline constexpr std::int32_t label_pad_x = 8;
inline constexpr std::int32_t label_pad_y = 6;
}  // namespace theme

namespace detail {
// The tree type a concept check passes to a widget. Handlers declare their return type, so the check
// instantiates no handler body and no callback, and the real tree can still be incomplete.
struct ProbeUi {};
struct ProbeShaper {
    [[nodiscard]] constexpr std::expected<TextRun, Error> shape(std::string_view) const { return TextRun{}; }
    [[nodiscard]] constexpr std::int32_t line_height() const noexcept { return 0; }
};
}  // namespace detail

// tag names the widget in the tree. layout places it inside the rectangle offered and returns what
// is left over for whatever comes next. handle takes each event that has an answer; a widget with no
// interest in one answers false. notify takes each notice, which has no answer and so nothing to discard.
template <class W>
concept Widget = requires(W& widget, const W& constant, Canvas& canvas, Rect area, const detail::ProbeShaper& text,
                          detail::ProbeUi& ui) {
    typename W::tag;
    { constant.bounds() } -> std::same_as<Rect>;
    { widget.paint(canvas) } -> std::same_as<void>;
    { widget.layout(area, text) } -> std::same_as<std::expected<Rect, Error>>;
    { widget.handle(ui, PointerPress{}) } -> std::same_as<bool>;
    { widget.handle(ui, PointerRelease{}) } -> std::same_as<bool>;
    { widget.handle(ui, PointerMove{}) } -> std::same_as<bool>;
    { widget.handle(ui, KeyEvent{}) } -> std::same_as<bool>;
    { widget.handle(ui, HoldsCapture{}) } -> std::same_as<bool>;
    { widget.handle(ui, QueryDamage{}) } -> std::same_as<bool>;
    { widget.notify(ui, PointerLeave{}) } -> std::same_as<void>;
    { widget.notify(ui, ClearFocus{}) } -> std::same_as<void>;
    { widget.notify(ui, ClearDamage{}) } -> std::same_as<void>;
    { widget.notify(ui, CollectDead{}) } -> std::same_as<void>;
};

namespace detail {

// Every widget sees the event. True when any of them answered true.
template <class Children, class U, class E>
constexpr bool broadcast(Children& children, U& ui, const E& event) {
    bool any = false;
    std::apply([&](auto&... child) { ((any = child.handle(ui, event) || any), ...); }, children);
    return any;
}

// Every widget gets the notice.
template <class Children, class U, class E>
constexpr void notify_all(Children& children, U& ui, const E& event) {
    std::apply([&](auto&... child) { (child.notify(ui, event), ...); }, children);
}

// Default delivery: a broadcast.
template <class Children, class U, class E>
constexpr bool deliver(Children& children, U& ui, const E& event) {
    return broadcast(children, ui, event);
}

// A press goes to the topmost widget under it: last painted first, stopping at the first that takes it.
template <class Children, class U>
constexpr bool deliver(Children& children, U& ui, const PointerPress& event) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        constexpr std::size_t count = sizeof...(I);
        return (std::get<count - 1 - I>(children).handle(ui, event) || ...);
    }(std::make_index_sequence<std::tuple_size_v<Children>>{});
}

// Motion reaches the topmost widget under the pointer, and those below it see the pointer leave.
// During a capture every widget sees it, so the holder keeps tracking and the others stay idle.
template <class Children, class U>
constexpr bool deliver(Children& children, U& ui, const PointerMove& event) {
    if (event.captured) return broadcast(children, ui, event);
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        constexpr std::size_t count = sizeof...(I);
        bool hit = false;
        const auto visit = [&](auto& child) {
            if (hit) {
                child.notify(ui, PointerLeave{});
            } else {
                hit = child.handle(ui, event);
            }
        };
        (visit(std::get<count - 1 - I>(children)), ...);
        return hit;
    }(std::make_index_sequence<std::tuple_size_v<Children>>{});
}

// Places the widgets in order, each in what the one before it left.
template <class Children, class Shaper>
constexpr std::expected<Rect, Error> layout_in_order(Children& children, Rect area, const Shaper& text) {
    std::expected<Rect, Error> remaining{area};
    const auto place = [&](auto& child) {
        if (remaining) remaining = child.layout(*remaining, text);
    };
    std::apply([&](auto&... child) { (place(child), ...); }, children);
    return remaining;
}

template <class Children>
constexpr void paint_in_order(Children& children, Canvas& canvas) {
    std::apply([&](auto&... child) { (child.paint(canvas), ...); }, children);
}

}  // namespace detail

}  // namespace jgui::inline v001
