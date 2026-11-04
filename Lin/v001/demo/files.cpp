// File: demo/files.cpp
// Purpose: jcppgui-files: a file manager with two panes, each going its own way. It starts in the folders given as its two optional arguments, or else where it
//          was started. A folder it cannot read does not end it: that pane says why. It can copy things, and never moves, replaces or deletes anything.
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "files_ui.hpp"
#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

namespace {

constexpr int exit_failure = 1;
constexpr int exit_usage = 2;

int report(const jgui::Error& error) {
    std::cerr << "jcppgui-files: " << error.message << '\n';
    return exit_failure;
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) return report(arguments.error());
    if (arguments->size() > 3) {
        std::cerr << "usage: jcppgui-files [left-folder [right-folder]]\n";
        return exit_usage;
    }
    const files::StartFolder left = jgui::starting_folder(arguments->size() >= 2 ? arguments->at(1) : std::string{"."});
    const files::StartFolder right = jgui::starting_folder(arguments->size() == 3 ? arguments->at(2) : std::string{"."});

    auto backend = jgui::WaylandBackend::create(jgui::WaylandOptions{.title = "jCppGUI files", .app_id = "jcppgui.files", .size = jgui::Size{jgui::Width{1400}, jgui::Height{800}}});
    if (!backend) return report(backend.error());
    auto app = jgui::make_application(std::move(*backend), files::make_ui(jgui::LocalFiles{}, left, right));
    if (!app) return report(app.error());
    const auto result = app->run();
    return result ? EXIT_SUCCESS : report(result.error());
}
