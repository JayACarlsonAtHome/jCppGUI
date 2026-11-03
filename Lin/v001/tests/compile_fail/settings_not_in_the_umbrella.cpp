// File: tests/compile_fail/settings_not_in_the_umbrella.cpp
// Purpose: The settings recorder is asked for by name, so jgui.hpp does not bring it in.
#include "common.hpp"
#ifndef JGUI_BREAK
#include "jCppGUI/v001/settings_recorder.hpp"
#endif

using Recorder = jgui::SettingsRecorder<>;

int main() {
    static_cast<void>(sizeof(Recorder));
}
