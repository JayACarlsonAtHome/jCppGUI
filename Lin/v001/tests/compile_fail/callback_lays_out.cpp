// File: tests/compile_fail/callback_lays_out.cpp
// Purpose: A callback holds the Window, which must not let it run layout.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        static_cast<void>(u.layout(jgui::Rect{}, jgui::detail::ProbeShaper{}));
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
