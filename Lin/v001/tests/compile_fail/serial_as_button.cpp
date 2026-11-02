// File: tests/compile_fail/serial_as_button.cpp
// Purpose: A serial passed where a pointer button belongs must not compile.
#include "common.hpp"

// A serial cannot be made here, so this takes one from an event.
[[maybe_unused]] static jgui::PointerPress again(const jgui::PointerPress& event) {
    const jgui::Serial serial = event.serial;
#ifdef JGUI_BREAK
    return jgui::PointerPress{jgui::Point{}, serial, serial};
#else
    return jgui::PointerPress{jgui::Point{}, jgui::MouseButton::Left, serial};
#endif
}

int main() {}
