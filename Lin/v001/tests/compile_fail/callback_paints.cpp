// File: tests/compile_fail/callback_paints.cpp
// Purpose: A callback holds the Window, which must not let it paint.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.paint(*static_cast<jgui::Canvas*>(nullptr));
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
