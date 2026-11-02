// File: tests/compile_fail/callback_forges_serial.cpp
// Purpose: A serial proves that a press happened. A callback must not be able to make one to ask for a move.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.request_move(jgui::Serial{0});
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
