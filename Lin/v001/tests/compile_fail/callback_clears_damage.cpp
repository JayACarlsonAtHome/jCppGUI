// File: tests/compile_fail/callback_clears_damage.cpp
// Purpose: A callback holds the Window, which must not let it say a frame was painted.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.clear_damage();
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
