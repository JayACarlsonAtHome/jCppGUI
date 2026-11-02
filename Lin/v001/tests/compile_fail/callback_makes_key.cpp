// File: tests/compile_fail/callback_makes_key.cpp
// Purpose: A callback that reaches a widget must not be able to make the key that runs one.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.template get<First>().notify(jgui::TreeKey{}, u, jgui::ClearDamage{});
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
