// File: tests/compile_fail/unknown_tag.cpp
// Purpose: get<Tag>() with a tag not in the tree must not compile.
#include "common.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
#ifdef JGUI_BREAK
    static_cast<void>(ui.get<Second>());
#else
    static_cast<void>(ui.get<First>());
#endif
}
