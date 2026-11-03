// File: tests/compile_fail/monitor_not_in_the_umbrella.cpp
// Purpose: The redraw monitor is a development tool, so jgui.hpp does not bring it in: a program asks for it by name.
#include "common.hpp"
#ifndef JGUI_BREAK
#include "jCppGUI/v001/redraw_monitor.hpp"
#endif

int main() {
    const jgui::FrameMonitor<> monitor{};
    static_cast<void>(monitor);
}
