// File: tests/compile_fail/not_a_widget.cpp
// Purpose: A type that lacks paint, used as a widget, must fail on the concept.
#ifdef JGUI_BREAK
#define JGUI_PLAIN_LACKS_PAINT
#endif
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    static_cast<void>(ui);
}
