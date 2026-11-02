// File: tests/compile_fail/frame_copied.cpp
// Purpose: A frame cannot be copied, so there is only ever one that can be presented.
#include <utility>

#include "common.hpp"
#include "jCppGUI/v001/headless_backend.hpp"

int main() {
    jgui::HeadlessBackend backend{jgui::Size{jgui::Width{1}, jgui::Height{1}}};
    auto frame = backend.acquire_frame(jgui::Size{jgui::Width{1}, jgui::Height{1}});
    if (!frame) return 1;
#ifdef JGUI_BREAK
    auto kept = *frame;
#else
    auto kept = std::move(*frame);
#endif
    static_cast<void>(backend.present(std::move(kept), jgui::Rect{}));
}
