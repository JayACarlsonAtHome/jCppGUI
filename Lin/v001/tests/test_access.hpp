// File: tests/test_access.hpp
// Purpose: What only tests may reach: an application's backend, and the window's size and requests. The
//          toolkit declares detail::TestAccess and makes it a friend. It is defined only here, so no program
//          or callback can use it, and tests/check_source_rules.sh holds that.
#pragma once

#include <cstdint>

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
    template <class B, class Root>
    [[nodiscard]] static constexpr B& backend(ApplicationCore<B, Root>& core) noexcept {
        return core.backend_;
    }
    template <class B, class Root>
    [[nodiscard]] static B& backend(Application<B, Root>& application) {
        application.check_thread();
        return backend(application.core_);
    }
    template <class... C>
    static constexpr void set_size(Window<C...>& window, Size size) noexcept {
        window.set_size(size);
    }
    template <class... C>
    [[nodiscard]] static constexpr WindowRequests take_requests(Window<C...>& window) noexcept {
        return window.take_requests();
    }
};

}  // namespace jgui::inline v001::detail

namespace jgui_test {

// A test's own loops and measurements read the number. Toolkit code cannot.
template <class Value>
[[nodiscard]] constexpr std::int32_t number(Value value) noexcept {
    return jgui::detail::TestAccess::number(value);
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
