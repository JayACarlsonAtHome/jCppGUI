// File: tests/compile_fail/left_plus_left.cpp
// Purpose: Adding two positions means nothing and must not compile. A position plus a length does.
#include "common.hpp"

int main() {
    const jgui::Left left{1};
#ifdef JGUI_BREAK
    const auto far = left + left;
#else
    const auto far = left + jgui::Width{1};
#endif
    static_cast<void>(far);
}
