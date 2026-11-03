// File: tests/compile_fail/two_item_recorders.cpp
// Purpose: Two recorders in the one slot would each number the same items, so that does not compile.
#include "common.hpp"
#include "jCppGUI/v001/settings_recorder.hpp"

#ifdef JGUI_BREAK
using Both = jgui::BothMonitors<jgui::SettingsRecorder<>, jgui::SettingsRecorder<>>;
#else
using Both = jgui::BothMonitors<jgui::NoMonitor, jgui::SettingsRecorder<>>;
#endif

int main() {
    static_cast<void>(sizeof(Both));
}
