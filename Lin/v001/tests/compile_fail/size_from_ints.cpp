// File: tests/compile_fail/size_from_ints.cpp
// Purpose: A size made from two loose integers must not compile.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    const jgui::Size size{640, 480};
#else
    const jgui::Size size{jgui::Width{640}, jgui::Height{480}};
#endif
    static_cast<void>(size);
}
