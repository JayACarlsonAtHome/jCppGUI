// File: tests/compile_fail/callback_records_items.cpp
// Purpose: A callback holds the Window, and cannot run the walk that writes the tree down: only the application can.
#include "common.hpp"
#include "jCppGUI/v001/settings_recorder.hpp"

int main() {
    auto ui = jgui::window(Plain<First>{});
    jgui::NoMonitor monitor{};
    const auto on_click = [&monitor](auto& u) -> void {
#ifdef JGUI_BREAK
        u.record_items(monitor);
#else
        static_cast<void>(monitor);
        u.request_close();
#endif
    };
    on_click(ui);
}
