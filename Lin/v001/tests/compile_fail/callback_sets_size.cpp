// File: tests/compile_fail/callback_sets_size.cpp
// Purpose: A callback receives the Window, which must not let it change the size behind the validation.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    const auto on_click = [](auto& u) -> void {
#ifdef JGUI_BREAK
        u.set_size(jgui::Size{jgui::Width{1}, jgui::Height{1}});
#else
        u.request_close();
#endif
    };
    on_click(ui);
}
