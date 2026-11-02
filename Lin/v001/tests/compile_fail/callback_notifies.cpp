// File: tests/compile_fail/callback_notifies.cpp
// Purpose: A callback holds the Window, which must not let it send a notice into the tree.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.notify(jgui::ClearFocus{});
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
