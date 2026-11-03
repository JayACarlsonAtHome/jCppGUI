// File: demo/demo.cpp
// Purpose: Opens a 640x480 window with a menu bar holding a Help button. --smoke waits for the first
//          configure, presents one frame and exits 0, for testing against a live compositor. --redraw-monitor
//          runs the same window with the redraw monitor, which records to redraw_monitor.jtext in the application base directory.
//          --record-settings writes down what the window holds, in settings.jtext beside it. The two can be used together.
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include "help_ui.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "jCppGUI/v001/settings_recorder.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;
constexpr int exit_ctest_skip = 77;

int report(const jgui::Error& error) {
    std::cerr << "jcppgui-demo: " << error.message << '\n';
    return exit_failure;
}

// The whole demo, for either kind of monitor: each is its own instantiation of this.
template <class Monitor>
int run_demo(bool smoke, Monitor monitor) {
    auto backend = jgui::WaylandBackend::create(
        jgui::WaylandOptions{.title = "jCppGUI", .app_id = "jcppgui.demo", .size = jgui::Size{jgui::Width{640}, jgui::Height{480}}});
    if (!backend) return report(backend.error());

    auto app = jgui::make_application(std::move(*backend), demo::make_ui(), std::move(monitor));
    if (!app) return report(app.error());

    const auto result = smoke ? app->present_first_frame() : app->run();
    // A monitor that could not write stopped quietly and the program went on, so this is where it is told.
    const auto monitor_error = app->monitor_error();
    if (monitor_error) static_cast<void>(report(*monitor_error));
    if (!result) return report(result.error());
    return monitor_error ? exit_failure : EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) return report(arguments.error());
    bool smoke = false;
    bool redraw_monitor = false;
    bool record_settings = false;
    for (std::size_t i = 1; i < arguments->size(); ++i) {
        const std::string_view argument = arguments->at(i);
        if (argument == "--smoke") {
            smoke = true;
        } else if (argument == "--redraw-monitor") {
            redraw_monitor = true;
        } else if (argument == "--record-settings") {
            record_settings = true;
        } else {
            std::cerr << "usage: jcppgui-demo [--smoke] [--redraw-monitor] [--record-settings]\n";
            return exit_usage;
        }
    }
    if (smoke && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        std::cerr << "jcppgui-demo: WAYLAND_DISPLAY is not set; skipping\n";
        return exit_ctest_skip;
    }

    if (redraw_monitor && record_settings) return run_demo(smoke, jgui::BothMonitors{jgui::FrameMonitor<>{}, jgui::make_settings_recorder()});
    if (record_settings) return run_demo(smoke, jgui::make_settings_recorder());
    return redraw_monitor ? run_demo(smoke, jgui::FrameMonitor<>{}) : run_demo(smoke, jgui::NoMonitor{});
}
