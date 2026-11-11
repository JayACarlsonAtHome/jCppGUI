// File: include/jCppGUI/v001/one_shown.hpp
// Purpose: OneShown<Tag, Children...>: a node that holds several widgets and shows at most one at a time, in all the room it is
//          offered. The one shown gets the layout, the paint and the events, so the others cost nothing and keep what they hold, such
//          as their rows and where they are scrolled to. Each keeps its own type.
#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <tuple>
#include <utility>

#include "panels.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

template <class Tag, Widget... Children>
class OneShown {
public:
    using tag = Tag;
    using children_type = std::tuple<Children...>;

    constexpr explicit OneShown(Children... children) : children_{std::move(children)...} {}
    OneShown(const OneShown&) = delete;
    OneShown& operator=(const OneShown&) = delete;
    constexpr OneShown(OneShown&&) = default;
    OneShown& operator=(OneShown&&) = delete;
    constexpr ~OneShown() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr std::optional<std::size_t> shown() const noexcept { return shown_; }
    [[nodiscard]] constexpr bool own_damage() const noexcept { return damaged_; }

    // Shows the child at this position, or nothing at all, which a position past the last also means. A callback can call this. It holds
    // no key to tell the one going away, so that is told as soon as this delivery ends, and until then the old one gets no events.
    constexpr void show(std::optional<std::size_t> position) noexcept {
        if (position && *position >= sizeof...(Children)) position = {};
        if (position == shown_) return;
        if (shown_ && !leaving_) leaving_ = shown_;
        shown_ = position;
        damaged_ = true;
    }

    // The one shown takes all it is offered and says what it left. With none shown, everything is left.
    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        bounds_ = area;
        std::expected<Rect, Error> remaining{area};
        with_shown([&](auto& child) { remaining = child.layout(key, full_of(area), text); });
        return remaining;
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        with_shown([&](auto& child) { child.paint(key, canvas); });
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const QueryDamage& event) {
        return ask_shown(key, ui, event) || damaged_;
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey& key, U& ui, const E& event) {
        return ask_shown(key, ui, event);
    }

    // Tells the one that went away that the pointer and the focus went with it, as soon as this can.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const CollectDead& event) {
        if (const std::optional<std::size_t> gone = std::exchange(leaving_, {}); gone && gone != shown_) {
            with_child(*gone, [&](auto& child) {
                child.notify(key, ui, PointerLeave{});
                child.notify(key, ui, ClearFocus{});
            });
        }
        detail::notify_all(children_, key, ui, event);
    }

    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const ClearDamage& event) {
        damaged_ = false;
        detail::notify_all(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey& key, U& ui, const E& event) {
        detail::notify_all(children_, key, ui, event);
    }

private:
    template <class Visit>
    constexpr void with_child(std::size_t position, Visit&& visit) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((I == position ? visit(std::get<I>(children_)) : void()), ...);
        }(std::index_sequence_for<Children...>{});
    }

    template <class Visit>
    constexpr void with_shown(Visit&& visit) {
        if (shown_) with_child(*shown_, visit);
    }

    template <class U, class E>
    constexpr bool ask_shown(const TreeKey& key, U& ui, const E& event) {
        bool answer = false;
        with_shown([&](auto& child) { answer = child.handle(key, ui, event); });
        return answer;
    }

    children_type children_;
    // Emptied with = {} and not reset(): clang cannot copy, in a constant expression, an optional of a trivially copyable type
    // that reset() emptied under libstdc++ 15, and one assigned empty copies.
    std::optional<std::size_t> shown_{};
    std::optional<std::size_t> leaving_{};
    Rect bounds_{};
    bool damaged_{};
};

template <class Tag, class... Children>
[[nodiscard]] constexpr OneShown<Tag, Children...> one_shown(Children... children) {
    return OneShown<Tag, Children...>{std::move(children)...};
}

}  // namespace jgui::inline v001
