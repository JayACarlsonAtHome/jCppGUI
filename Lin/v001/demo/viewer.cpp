// File: demo/viewer.cpp
// Purpose: jcppgui-viewer: shows a redraw monitor's file in grids, one section at a time. The file is redraw_monitor.jtext in the
//          application's base directory, or the path given as the one argument. A file that is missing or refused does not end the
//          program: the window opens and says why, where the grid would be. It reads the file once, when it starts.
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <utility>

#include "jCppGUI/v001/redraw_monitor.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"
#include "viewer_ui.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

int report(const jgui::Error& error) {
    std::cerr << "jcppgui-viewer: " << error.message << '\n';
    return exit_failure;
}

[[nodiscard]] std::expected<std::string, jgui::Error> default_path() {
    auto base = jgui::application_base_directory();
    if (!base) return std::unexpected(jgui::Error{base.error().code, "the viewer has no base directory: " + base.error().message});
    return viewer::checked_path(*base + "/" + std::string{jgui::default_redraw_file_name});
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) return report(arguments.error());
    if (arguments->size() > 2) {
        std::cerr << "usage: jcppgui-viewer [jtext-file]\n";
        return exit_usage;
    }
    const auto path = arguments->size() == 2 ? viewer::checked_path(arguments->at(1)) : default_path();
    // What the file holds becomes typed rows here, and its text goes when this statement ends.
    auto loaded = viewer::load_file(path ? jgui::read_jtext_file(*path) : std::expected<jgui::JTextFile, jgui::Error>{std::unexpected(path.error())});

    auto backend = jgui::WaylandBackend::create(jgui::WaylandOptions{.title = viewer::window_title_for(path), .app_id = "jcppgui.viewer", .size = jgui::Size{jgui::Width{1000}, jgui::Height{600}}});
    if (!backend) return report(backend.error());
    auto app = jgui::make_application(std::move(*backend), viewer::make_ui(std::move(loaded)));
    if (!app) return report(app.error());
    const auto result = app->run();
    return result ? EXIT_SUCCESS : report(result.error());
}
