// File: tests/compile_fail/listener_wrong_method.cpp
// Purpose: A listener table holds one owner's functions, so another type's method in it must not compile.
#include "wayland/trampoline.hpp"

struct Owner {
    void on_release(wl_buffer*) noexcept {}
};
struct Other {
    void on_release(wl_buffer*) noexcept {}
};

using Listener = jgui::detail::OwnedListener<Owner, wl_buffer_listener>;

#ifdef JGUI_BREAK
constexpr Listener listener{{.release = Listener::on<&Other::on_release>}};
#else
constexpr Listener listener{{.release = Listener::on<&Owner::on_release>}};
#endif

int main() {
    Other other;
    static_cast<void>(other);
    static_cast<void>(listener.table.release);
}
