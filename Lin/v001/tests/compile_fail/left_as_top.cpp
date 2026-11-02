// File: tests/compile_fail/left_as_top.cpp
// Purpose: A Left passed as a Top must not compile.
#include "common.hpp"

int main() {
    const jgui::Left left{1};
#ifdef JGUI_BREAK
    const jgui::Point point{jgui::Left{left}, jgui::Top{left}};
#else
    const jgui::Point point{left, jgui::Top{1}};
#endif
    static_cast<void>(point);
}
