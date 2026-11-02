// File: tests/test_access.hpp
// Purpose: What only tests may reach: an application's backend, and the window's size and requests. The
//          toolkit declares detail::TestAccess and makes it a friend. It is defined only here, so no program
//          or callback can use it, and tests/check_source_rules.sh holds that.
#pragma once

#include "jCppGUI/v001/application.hpp"
#include "jCppGUI/v001/tree.hpp"

namespace jgui::inline v001::detail {

struct TestAccess {
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

template <class App>
[[nodiscard]] constexpr auto& backend(App& app) {
    return jgui::detail::TestAccess::backend(app);
}

}  // namespace jgui_test
