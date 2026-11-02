// File: tests/compile_fail/size_swapped.cpp
// Purpose: A height where the width belongs must not compile.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    const jgui::Size size{jgui::Height{480}, jgui::Width{640}};
#else
    const jgui::Size size{jgui::Width{640}, jgui::Height{480}};
#endif
    static_cast<void>(size);
}
