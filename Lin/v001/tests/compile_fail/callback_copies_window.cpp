// File: tests/compile_fail/callback_copies_window.cpp
// Purpose: A callback must not be able to copy the Window, or assign over it.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        auto copy = u; static_cast<void>(copy);
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
