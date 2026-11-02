// File: tests/compile_fail/duplicate_tag.cpp
// Purpose: Two widgets with the same tag in one tree must not compile, at any depth.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    auto ui = jgui::window(jgui::menu_bar(Plain<First>{}), Plain<First>{});
#else
    auto ui = jgui::window(jgui::menu_bar(Plain<First>{}), Plain<Second>{});
#endif
    static_cast<void>(ui.get<First>());
}
