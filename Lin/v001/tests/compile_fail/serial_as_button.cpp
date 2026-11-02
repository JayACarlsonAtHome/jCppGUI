// File: tests/compile_fail/serial_as_button.cpp
// Purpose: A serial passed where a pointer button belongs must not compile.
#include "common.hpp"

int main() {
    const jgui::Serial serial{1};
#ifdef JGUI_BREAK
    const jgui::PointerPress press{jgui::Point{}, serial, serial};
#else
    const jgui::PointerPress press{jgui::Point{}, jgui::MouseButton::Left, serial};
#endif
    static_cast<void>(press);
}
