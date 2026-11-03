// File: demo/viewer.cpp
// Purpose: jcppgui-viewer: shows a redraw monitor's file and a settings record in grids, one section at a time. The files are
//          redraw_monitor.jtext and settings.jtext in the application's base directory, or the paths given as the two optional arguments,
//          in that order. A file that is missing or refused does not end the program, and does not touch the other file: the window opens
//          and says why, where that file's grid would be. It reads each file once, when it starts.
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "jCppGUI/v001/jtext_writer.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"
#include "viewer_ui.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

int report(const jgui::Error& error) {
    std::cerr << "jcppgui-viewer: " << error.message << '\n';
    return exit_failure;
}

using Path = std::expected<std::string, jgui::Error>;

[[nodiscard]] Path default_path(std::string_view file_name) {
    auto base = jgui::application_base_directory();
    if (!base) return std::unexpected(jgui::Error{base.error().code, "the viewer has no base directory: " + base.error().message});
    return viewer::checked_path(*base + "/" + std::string{file_name});
}

[[nodiscard]] std::expected<jgui::JTextFile, jgui::Error> read_if_a_path(const Path& path) {
    if (!path) return std::unexpected(path.error());
    return jgui::read_jtext_file(*path);
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) return report(arguments.error());
    if (arguments->size() > 3) {
        std::cerr << "usage: jcppgui-viewer [redraw-monitor-file [settings-file]]\n";
        return exit_usage;
    }
    const Path path = arguments->size() >= 2 ? viewer::checked_path(arguments->at(1)) : default_path(jgui::default_redraw_file_name);
    const Path settings_path = arguments->size() == 3 ? viewer::checked_path(arguments->at(2)) : default_path(jgui::default_settings_file_name);
    // What each file holds becomes typed rows here, and its text goes when the statement ends.
    auto loaded = viewer::load_file(read_if_a_path(path));
    auto settings = viewer::load_settings_file(read_if_a_path(settings_path));

    auto backend = jgui::WaylandBackend::create(jgui::WaylandOptions{.title = viewer::window_title_for(path), .app_id = "jcppgui.viewer", .size = jgui::Size{jgui::Width{1000}, jgui::Height{600}}});
    if (!backend) return report(backend.error());
    auto app = jgui::make_application(std::move(*backend), viewer::make_ui(std::move(loaded), std::move(settings)));
    if (!app) return report(app.error());
    const auto result = app->run();
    return result ? EXIT_SUCCESS : report(result.error());
}
