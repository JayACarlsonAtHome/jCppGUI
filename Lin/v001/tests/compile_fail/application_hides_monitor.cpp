// File: tests/compile_fail/application_hides_monitor.cpp
// Purpose: A callback that reached its application through a pointer still finds no monitor: it is private there.
#include "common.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"

template <class Core>
void from_a_callback(Core& core) {
#ifdef JGUI_BREAK
    core.monitor_.finish();
#else
    static_cast<void>(core.frames());
#endif
}

int main() {
    const jgui::Size size{jgui::Width{10}, jgui::Height{10}};
    jgui::ApplicationCore core{jgui::HeadlessBackend{size}, jgui::window(Plain<First>{}), jgui::FrameMonitor<>{}};
    from_a_callback(core);
}
