// File: tests/compile_fail/keysym_as_modifier.cpp
// Purpose: A keysym passed where a modifier mask belongs must not compile.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    const jgui::KeyEvent key{jgui::Key::Return, jgui::Key::Space, true};
#else
    const jgui::KeyEvent key{jgui::Key::Return, jgui::Modifier::Shift, true};
#endif
    static_cast<void>(key);
}
