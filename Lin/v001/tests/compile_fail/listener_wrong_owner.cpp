// File: tests/compile_fail/listener_wrong_owner.cpp
// Purpose: Registering a listener with an object of another type would cast the untyped pointer back to the wrong
//          type. It must not compile.
#include <cstdint>

#include "wayland/trampoline.hpp"

struct Right {
    void on_release(wl_buffer*) noexcept {}
};
struct Wrong {
    std::uint32_t other{};
};

using Listener = jgui::detail::OwnedListener<Right, wl_buffer_listener>;
constexpr Listener listener{{.release = Listener::on<&Right::on_release>}};

int main() {
    wl_buffer* proxy = nullptr;
    Right right;
    Wrong wrong;
#ifdef JGUI_BREAK
    const bool added = jgui::detail::listen<wl_buffer_add_listener>(proxy, listener, wrong);
#else
    const bool added = jgui::detail::listen<wl_buffer_add_listener>(proxy, listener, right);
#endif
    static_cast<void>(wrong);
    static_cast<void>(right);
    return added ? 0 : 1;
}
