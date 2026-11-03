// File: tests/compile_fail/caption_invalid_at_compile_time.cpp
// Purpose: A caption known at compile time is checked at compile time, so a bad one is never a run-time matter.
#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    auto ui = jgui::label<First>("He\xC3");
#else
    auto ui = jgui::label<First>("Help");
#endif
    static_cast<void>(ui);
}
