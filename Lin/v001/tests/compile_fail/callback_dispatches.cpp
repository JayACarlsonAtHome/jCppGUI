// File: tests/compile_fail/callback_dispatches.cpp
// Purpose: A callback holds the Window, which must not let it send an event into the tree.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        static_cast<void>(u.dispatch(jgui::PointerLeave{}));
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
