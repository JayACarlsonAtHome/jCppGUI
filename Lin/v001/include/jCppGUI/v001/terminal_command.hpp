// File: include/jCppGUI/v001/terminal_command.hpp
// Purpose: What to run in a terminal to show or edit files, and how each terminal is told what to run. All of it is lists of separate arguments and constexpr: a file's name is only ever one argument, and no
//          shell ever reads it as part of a command, so nothing in a name can run. What starts the terminal is in local_files.hpp.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"

namespace jgui::inline v001 {

enum class TerminalTool : std::uint8_t { Cat, Edit };

inline constexpr std::string_view fallback_editor = "vim";

// A terminal's program and what comes before the command it is to run. A terminal closes when its command ends, so Cat waits for Return.
struct TerminalStyle {
    std::string_view program{};
    std::array<std::string_view, 2> before{};
    std::size_t before_count{};
};

// Tried in this order, after one the person named in TERMINAL, which is told -e as most of these understand.
inline constexpr std::array<TerminalStyle, 10> known_terminals{{
    {"xdg-terminal-exec", {}, 0},
    {"ptyxis", {"--"}, 1},
    {"kgx", {"--"}, 1},
    {"gnome-terminal", {"--"}, 1},
    {"konsole", {"-e"}, 1},
    {"xfce4-terminal", {"-x"}, 1},
    {"foot", {}, 0},
    {"kitty", {}, 0},
    {"alacritty", {"-e"}, 1},
    {"xterm", {"-e"}, 1},
}};

// The "$@" is what takes the names, as separate words, from the arguments after this script. No name is ever part of the script's text.
inline constexpr std::string_view cat_and_wait_script = "cat -- \"$@\"; printf '\\n[Press Return to close] '; read -r _";

// The words of EDITOR, such as "emacs -nw", split on spaces and tabs, with vim when it is empty. No shell reads it, so a quote or a backslash would not mean what whoever wrote it meant, and is refused rather than guessed at.
[[nodiscard]] constexpr std::expected<std::vector<std::string>, Error> editor_words(std::string_view editor) {
    if (editor.find_first_of("'\"\\") != std::string_view::npos) return make_error(ErrorCode::System, "EDITOR holds a quote or a backslash, which this does not read. Set it to a program and plain options");
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < editor.size()) {
        const std::size_t start = editor.find_first_not_of(" \t", at);
        if (start == std::string_view::npos) break;
        const std::size_t end = std::min(editor.find_first_of(" \t", start), editor.size());
        words.emplace_back(editor.substr(start, end - start));
        at = end;
    }
    if (words.empty()) words.emplace_back(fallback_editor);
    return words;
}

// The folders PATH names that are absolute, in its order. An empty or relative one means wherever the program happens to be running, which could be a folder someone else filled, so it is not looked in.
[[nodiscard]] constexpr std::vector<std::string> absolute_path_folders(std::string_view path) {
    std::vector<std::string> folders;
    while (true) {
        const std::size_t colon = path.find(':');
        const std::string_view folder = path.substr(0, colon);
        if (folder.starts_with('/')) folders.emplace_back(folder);
        if (colon == std::string_view::npos) return folders;
        path.remove_prefix(colon + 1);
    }
}

// What to run. editor is only used for Edit. "--" tells it where options end, so a name cannot be one.
[[nodiscard]] constexpr std::expected<std::vector<std::string>, Error> terminal_tool_command(TerminalTool tool, std::span<const std::string> paths, std::string_view editor) {
    std::vector<std::string> command;
    if (tool == TerminalTool::Cat) {
        command = {"sh", "-c", std::string{cat_and_wait_script}, "sh"};
    } else {
        auto words = editor_words(editor);
        if (!words) return std::unexpected(std::move(words.error()));
        command = std::move(*words);
        command.emplace_back("--");
    }
    command.insert(command.end(), paths.begin(), paths.end());
    return command;
}

[[nodiscard]] constexpr std::vector<std::string> terminal_arguments(const TerminalStyle& style, std::span<const std::string> command) {
    std::vector<std::string> arguments{std::string{style.program}};
    for (std::size_t i = 0; i < style.before_count; ++i) arguments.emplace_back(style.before[i]);
    arguments.insert(arguments.end(), command.begin(), command.end());
    return arguments;
}

}  // namespace jgui::inline v001
