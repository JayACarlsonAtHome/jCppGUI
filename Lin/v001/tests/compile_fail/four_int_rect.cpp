// File: tests/compile_fail/four_int_rect.cpp
// Purpose: A rectangle made from four loose integers must not compile: only a Point and a Size make one.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    const jgui::Rect area{1, 2, 3, 4};
#else
    const jgui::Rect area{jgui::Point{1, 2}, jgui::Size{3, 4}};
#endif
    static_cast<void>(area);
}
