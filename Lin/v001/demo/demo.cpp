// File: demo/demo.cpp
// Purpose: Opens a 640x480 window with a menu bar holding a Help button. --smoke waits for the first
//          configure, presents one frame and exits 0, for testing against a live compositor.
#include <cstdlib>
#include <iostream>
#include <utility>

#include "help_ui.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;
constexpr int exit_ctest_skip = 77;

int report(const jgui::Error& error) {
    std::cerr << "jcppgui-demo: " << error.message << '\n';
    return exit_failure;
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) return report(arguments.error());
    bool smoke = false;
    if (arguments->size() == 2 && arguments->at(1) == "--smoke") {
        smoke = true;
    } else if (arguments->size() != 1) {
        std::cerr << "usage: jcppgui-demo [--smoke]\n";
        return exit_usage;
    }
    if (smoke && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        std::cerr << "jcppgui-demo: WAYLAND_DISPLAY is not set; skipping\n";
        return exit_ctest_skip;
    }

    auto backend = jgui::WaylandBackend::create(
        jgui::WaylandOptions{.title = "jCppGUI", .app_id = "jcppgui.demo", .size = jgui::Size{jgui::Width{640}, jgui::Height{480}}});
    if (!backend) return report(backend.error());

    auto app = jgui::make_application(std::move(*backend), demo::make_ui());
    if (!app) return report(app.error());

    const auto result = smoke ? app->present_first_frame() : app->run();
    if (!result) return report(result.error());
    return EXIT_SUCCESS;
}
