// File: src/files/default_program.cpp
// Purpose: Finds the command the desktop would open a file with, in the same order the specification looks: the system's xdg-mime says the file's type and the entry for it, and the entry is read from the applications folders. Nothing here starts anything.
#include <stdlib.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/terminal_command.hpp"

namespace jgui::inline v001 {

namespace {

inline constexpr std::chrono::milliseconds most_time_to_ask{5000};

// The one line xdg-mime answers with, if it can be used as a word.
[[nodiscard]] std::optional<std::string> asked_of_xdg_mime(std::string_view question, const std::string& about) {
    const std::vector<std::string> command{"xdg-mime", "query", std::string{question}, about};
    auto said = read_program_output(command, most_time_to_ask);
    if (!said) return std::nullopt;
    std::string word = std::move(*said);
    while (!word.empty() && (word.back() == '\n' || word.back() == ' ')) word.pop_back();
    if (word.empty() || word.contains(' ') || word.contains('\n') || !text_can_show(word)) return std::nullopt;
    return word;
}

// Where desktop entries are, most personal first.
[[nodiscard]] std::vector<std::string> folders_holding_entries() {
    std::vector<std::string> folders;
    const char* data = ::secure_getenv("XDG_DATA_HOME");
    const char* home = ::secure_getenv("HOME");
    if (data != nullptr && data[0] == '/') {
        folders.emplace_back(data);
    } else if (home != nullptr && home[0] == '/') {
        folders.push_back(std::string{home} + "/.local/share");
    }
    if (const char* dirs = ::secure_getenv("XDG_DATA_DIRS"); dirs != nullptr) {
        for (std::string& folder : absolute_path_folders(dirs)) folders.push_back(std::move(folder));
    }
    return folders;
}

}  // namespace

std::optional<ProgramCommand> local_default_program(const std::string& path) {
    if (!file_path_can_show(path)) return std::nullopt;
    const auto type = asked_of_xdg_mime("filetype", path);
    if (!type) return std::nullopt;
    const auto entry = asked_of_xdg_mime("default", *type);
    if (!entry || !entry->ends_with(".desktop") || !file_name_can_show(*entry)) return std::nullopt;
    for (const std::string& folder : folders_holding_entries()) {
        const auto text = read_text_file(child_of_path(child_of_path(folder, "applications"), *entry));
        if (text) return desktop_command_for(*text, path);
    }
    return std::nullopt;
}

}  // namespace jgui::inline v001
