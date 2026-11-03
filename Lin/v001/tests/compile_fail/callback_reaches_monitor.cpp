// File: tests/compile_fail/callback_reaches_monitor.cpp
// Purpose: A callback holds the Window, and nothing on it leads to the application's monitor.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.monitor().finish();
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
