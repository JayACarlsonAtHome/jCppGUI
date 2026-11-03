// File: tests/compile_fail/set_text_result_ignored.cpp
// Purpose: Text set at run time can be refused, and a caller must look at the answer.
#include "common.hpp"

int main() {
    auto ui = jgui::label<First>("Help");
#ifdef JGUI_BREAK
    ui.set_text("next");
#else
    if (!ui.set_text("next")) return 1;
#endif
    return 0;
}
