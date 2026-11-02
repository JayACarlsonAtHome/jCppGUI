// File: tests/compile_fail/frame_presented_without_move.cpp
// Purpose: Presenting takes a frame by value, so an lvalue must be moved, and cannot be used after that.
#include <utility>

#include "common.hpp"
#include "jCppGUI/v001/headless_backend.hpp"

int main() {
    jgui::HeadlessBackend backend{jgui::Size{jgui::Width{1}, jgui::Height{1}}};
    auto frame = backend.acquire_frame(jgui::Size{jgui::Width{1}, jgui::Height{1}});
    if (!frame) return 1;
#ifdef JGUI_BREAK
    static_cast<void>(backend.present(*frame, jgui::Rect{}));
#else
    static_cast<void>(backend.present(std::move(*frame), jgui::Rect{}));
#endif
}
