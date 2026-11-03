// File: tests/compile_fail/jtext_result_ignored.cpp
// Purpose: A parse whose answer is ignored must not compile, because that answer may be a refusal.
#include "common.hpp"
#include "jCppGUI/v001/jtext.hpp"

int main() {
#ifdef JGUI_BREAK
    jgui::parse_jtext("=== jText File ===\n=== End File ===\n");
#else
    static_cast<void>(jgui::parse_jtext("=== jText File ===\n=== End File ===\n"));
#endif
    return 0;
}
