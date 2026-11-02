// File: demo/demo.cpp
// Purpose: Opens a 640x480 window with a menu bar holding a Help button. --smoke waits for the first
//          configure, presents one frame and exits 0, for testing against a live compositor.
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "help_ui.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;
constexpr int exit_ctest_skip = 77;

int report(const jgui::Error& error) {
    std::fprintf(stderr, "jcppgui-demo: %s\n", error.message.c_str());
    return exit_failure;
}

}  // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    if (argc == 2 && std::string_view{argv[1]} == "--smoke") {
        smoke = true;
    } else if (argc != 1) {
        std::fputs("usage: jcppgui-demo [--smoke]\n", stderr);
        return exit_usage;
    }
    if (smoke && std::getenv("WAYLAND_DISPLAY") == nullptr) {
        std::fputs("jcppgui-demo: WAYLAND_DISPLAY is not set; skipping\n", stderr);
        return exit_ctest_skip;
    }

    auto backend = jgui::WaylandBackend::create(
        jgui::WaylandOptions{.title = "jCppGUI", .app_id = "jcppgui.demo", .size = jgui::Size{640, 480}});
    if (!backend) return report(backend.error());

    auto app = jgui::make_application(std::move(*backend), demo::make_ui());
    if (!app) return report(app.error());

    const auto result = smoke ? app->present_first_frame() : app->run();
    if (!result) return report(result.error());
    return EXIT_SUCCESS;
}
