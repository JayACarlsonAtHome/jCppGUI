// File: tests/compile_fail/forges_clipboard_text.cpp
// Purpose: A clipboard takes only text the sanitizer has been through. Nothing else may be able to make any.
#include "common.hpp"

#include "jCppGUI/v001/clipboard_text.hpp"

int main() {
#ifdef JGUI_BREAK
    const jgui::SafeClipboardText made{std::string{"anything at all"}};
#else
    const jgui::SafeClipboardText made = jgui::inspect_clipboard_text("anything at all").text;
#endif
    return made.view().empty() ? 1 : 0;
}
