// File: tests/test_access.hpp
// Purpose: What only tests may reach: an application's backend, and the window's size and requests. The
//          toolkit declares detail::TestAccess and makes it a friend. It is defined only here, so no program
//          or callback can use it, and tests/check_source_rules.sh holds that.
#pragma once

#include <cstdint>
#include <expected>

#include "jCppGUI/v001/application.hpp"
#include "jCppGUI/v001/geometry.hpp"
#include "jCppGUI/v001/tree.hpp"

namespace jgui::inline v001::detail {

struct TestAccess {
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Edge<Tag> value) noexcept {
        return value.value_;
    }
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Extent<Tag> value) noexcept {
        return value.value_;
    }
    template <class B, class Root, class Monitor>
    [[nodiscard]] static constexpr B& backend(ApplicationCore<B, Root, Monitor>& core) noexcept {
        return core.backend_;
    }
    template <class B, class Root, class Monitor>
    [[nodiscard]] static B& backend(Application<B, Root, Monitor>& application) {
        application.check_thread();
        return backend(application.core_);
    }
    template <class B, class Root, class Monitor>
    [[nodiscard]] static constexpr Monitor& monitor(ApplicationCore<B, Root, Monitor>& core) noexcept {
        return core.monitor_;
    }
    template <class B, class Root, class Monitor>
    [[nodiscard]] static Monitor& monitor(Application<B, Root, Monitor>& application) {
        application.check_thread();
        return monitor(application.core_);
    }
    template <class Value>
    [[nodiscard]] static constexpr Value wire(std::uint32_t value) noexcept {
        return Value{typename Value::FromWire{}, value};
    }
    template <class... C, class E>
    static constexpr bool dispatch(Window<C...>& window, const E& event) {
        return window.dispatch(event);
    }
    template <class... C, class E>
    static constexpr void notify(Window<C...>& window, const E& event) {
        window.notify(event);
    }
    template <class... C>
    [[nodiscard]] static constexpr bool capture_active(Window<C...>& window) {
        return window.capture_active();
    }
    template <class... C>
    [[nodiscard]] static constexpr bool damaged(Window<C...>& window) {
        return window.damaged();
    }
    template <class... C>
    static constexpr void clear_damage(Window<C...>& window) {
        window.clear_damage();
    }
    template <class... C, class Shaper>
    static constexpr std::expected<Rect, Error> layout(Window<C...>& window, Rect area, const Shaper& text) {
        return window.layout(area, text);
    }
    template <class... C>
    static constexpr void paint(Window<C...>& window, Canvas& canvas) {
        window.paint(canvas);
    }
    template <class... C>
    static constexpr void set_size(Window<C...>& window, Size size) noexcept {
        window.set_size(size);
    }
    template <class... C>
    [[nodiscard]] static constexpr WindowRequests take_requests(Window<C...>& window) noexcept {
        return window.take_requests();
    }
    template <class... C>
    static constexpr void keep_picture(Window<C...>& window, WindowPicture picture) {
        window.keep_picture(std::move(picture));
    }
};

}  // namespace jgui::inline v001::detail

namespace jgui_test {

// A test's own loops and measurements read the number. Toolkit code cannot.
template <class Value>
[[nodiscard]] constexpr std::int32_t number(Value value) noexcept {
    return jgui::detail::TestAccess::number(value);
}

// A test runs a tree with no application, which only an application may otherwise do.
template <class W, class E>
constexpr bool dispatch(W& window, const E& event) {
    return jgui::detail::TestAccess::dispatch(window, event);
}
template <class W, class E>
constexpr void notify(W& window, const E& event) {
    jgui::detail::TestAccess::notify(window, event);
}
template <class W>
[[nodiscard]] constexpr bool capture_active(W& window) {
    return jgui::detail::TestAccess::capture_active(window);
}
template <class W>
[[nodiscard]] constexpr bool damaged(W& window) {
    return jgui::detail::TestAccess::damaged(window);
}
template <class W>
constexpr void clear_damage(W& window) {
    jgui::detail::TestAccess::clear_damage(window);
}
template <class W, class Shaper>
constexpr auto layout(W& window, jgui::Rect area, const Shaper& text) {
    return jgui::detail::TestAccess::layout(window, area, text);
}
template <class W>
constexpr void paint(W& window, jgui::Canvas& canvas) {
    jgui::detail::TestAccess::paint(window, canvas);
}

// What an application's monitor recorded. Nothing outside the tests can reach it.
template <class App>
[[nodiscard]] constexpr auto& monitor(App& app) {
    return jgui::detail::TestAccess::monitor(app);
}

// A serial, as a backend would hand one over. Nothing else can make one.
[[nodiscard]] constexpr jgui::Serial serial(std::uint32_t value) noexcept {
    return jgui::detail::TestAccess::wire<jgui::Serial>(value);
}

// A point that many pixels in from a rectangle's corner.
[[nodiscard]] constexpr jgui::Point inside(const jgui::Rect& area, std::int32_t across, std::int32_t down) noexcept {
    return jgui::Point{jgui::left_after(area.left() + jgui::Width{across}), jgui::top_after(area.top() + jgui::Height{down})};
}

template <class App>
[[nodiscard]] constexpr auto& backend(App& app) {
    return jgui::detail::TestAccess::backend(app);
}

}  // namespace jgui_test
