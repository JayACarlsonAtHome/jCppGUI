// File: include/jCppGUI/v001/open_programs.hpp
// Purpose: What Open starts: which common programs suit a file, and the words of a command from a desktop entry, all constexpr. A file's path is only ever one argument, and no shell reads any of it, so nothing in a name can run. An Exec line that
//          would need a shell to read is refused rather than guessed at. What asks the system is in local_files.hpp.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jgui::inline v001 {

// A command, each word one argument, and whether it needs a terminal to show in.
struct ProgramCommand {
    std::vector<std::string> words{};
    bool in_terminal{};

    friend constexpr bool operator==(const ProgramCommand&, const ProgramCommand&) = default;
};

// OBS, told to start recording. It is not told to hide in the tray: not every desktop shows a tray, and OBS has no option to stop from outside, so its window has to stay where the person can stop it.
inline constexpr std::string_view recording_program = "obs";
[[nodiscard]] constexpr ProgramCommand recording_command() { return ProgramCommand{{std::string{recording_program}, "--startrecording"}, false}; }

// Meld, on one thing from each side, the left first. A pane's paths are absolute, so each starts with a slash and Meld cannot read one as an option, whatever the name is.
inline constexpr std::string_view visual_diff_program = "meld";
[[nodiscard]] constexpr ProgramCommand visual_diff_command(std::string_view left, std::string_view right) { return ProgramCommand{{std::string{visual_diff_program}, std::string{left}, std::string{right}}, false}; }

enum class FileFamily : std::uint8_t { Office, Image, Text, Other };

inline constexpr std::array<std::string_view, 19> office_extensions{"odt", "ods", "odp", "odg", "ott", "ots", "otp", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "rtf", "csv", "tsv", "fodt", "fods", "fodp"};
inline constexpr std::array<std::string_view, 11> image_extensions{"png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "tif", "tiff", "ico", "avif"};
inline constexpr std::array<std::string_view, 12> text_extensions{"txt", "md", "log", "conf", "ini", "json", "xml", "yaml", "yml", "cpp", "hpp", "sh"};

// By the name's last extension, in either case. What it does not know is Other, which is offered everything.
[[nodiscard]] constexpr FileFamily family_of_name(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return FileFamily::Other;
    std::string extension{name.substr(dot + 1)};
    for (char& letter : extension) {
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
    }
    if (std::ranges::contains(office_extensions, extension)) return FileFamily::Office;
    if (std::ranges::contains(image_extensions, extension)) return FileFamily::Image;
    if (std::ranges::contains(text_extensions, extension)) return FileFamily::Text;
    return FileFamily::Other;
}

struct CommonProgram {
    std::string_view title{};
    std::string_view program{};
    FileFamily family{FileFamily::Other};
    bool in_terminal{};

    friend constexpr bool operator==(const CommonProgram&, const CommonProgram&) = default;
};

inline constexpr std::array<CommonProgram, 17> common_programs{{
    {"LibreOffice", "libreoffice", FileFamily::Office, false},
    {"OnlyOffice", "onlyoffice-desktopeditors", FileFamily::Office, false},
    {"AbiWord", "abiword", FileFamily::Office, false},
    {"Gnumeric", "gnumeric", FileFamily::Office, false},
    {"Loupe", "loupe", FileFamily::Image, false},
    {"Eye of GNOME", "eog", FileFamily::Image, false},
    {"Gwenview", "gwenview", FileFamily::Image, false},
    {"GIMP", "gimp", FileFamily::Image, false},
    {"Krita", "krita", FileFamily::Image, false},
    {"Inkscape", "inkscape", FileFamily::Image, false},
    {"GNOME Text Editor", "gnome-text-editor", FileFamily::Text, false},
    {"gedit", "gedit", FileFamily::Text, false},
    {"Kate", "kate", FileFamily::Text, false},
    {"Mousepad", "mousepad", FileFamily::Text, false},
    {"VS Code", "code", FileFamily::Text, false},
    {"Vim", "vim", FileFamily::Text, true},
    {"Nano", "nano", FileFamily::Text, true},
}};

// What goes with a file of this family, of those the question answers yes for. A family it does not know is offered all of them.
template <class Installed>
[[nodiscard]] constexpr std::vector<CommonProgram> programs_for(FileFamily family, std::size_t most, Installed installed) {
    std::vector<CommonProgram> chosen;
    for (const CommonProgram& each : common_programs) {
        if (chosen.size() == most) break;
        if ((family == FileFamily::Other || each.family == family) && installed(each.program)) chosen.push_back(each);
    }
    return chosen;
}

namespace detail {

// One word of an Exec line with its field codes filled in. A code the file's path goes in sets took_path. A code that only names the entry is dropped, as the specification allows. One it does not know is refused.
[[nodiscard]] constexpr std::optional<std::string> exec_word_filled(std::string_view word, std::string_view path, bool& took_path) {
    std::string out;
    for (std::size_t at = 0; at < word.size(); ++at) {
        if (word[at] != '%') {
            out.push_back(word[at]);
            continue;
        }
        if (++at == word.size()) return std::nullopt;
        const char code = word[at];
        if (code == '%') {
            out.push_back('%');
        } else if (code == 'f' || code == 'F' || code == 'u' || code == 'U') {
            out.append(path);
            took_path = true;
        } else if (code != 'i' && code != 'c' && code != 'k' && code != 'd' && code != 'D' && code != 'n' && code != 'N' && code != 'v' && code != 'm') {
            return std::nullopt;
        }
    }
    return out;
}

}  // namespace detail

// The command to open this path with, from the text of a desktop entry. Nothing when it has no usable Exec line: one with anything a shell would have read is refused, because there is no shell here. With no field code for the file, the path goes last,
// because the person asked for this file to be opened.
[[nodiscard]] constexpr std::optional<ProgramCommand> desktop_command_for(std::string_view entry, std::string_view path) {
    ProgramCommand command;
    std::string_view exec;
    bool in_main_group = false;
    while (!entry.empty()) {
        const std::size_t end = entry.find('\n');
        const std::string_view line = entry.substr(0, end);
        entry = end == std::string_view::npos ? std::string_view{} : entry.substr(end + 1);
        if (line.starts_with('[')) in_main_group = line == "[Desktop Entry]";
        if (!in_main_group) continue;
        if (line.starts_with("Exec=")) exec = line.substr(5);
        if (line == "Terminal=true") command.in_terminal = true;
    }
    if (exec.find_first_of("\"'\\`$;|&<>()") != std::string_view::npos) return std::nullopt;
    bool took_path = false;
    std::size_t at = 0;
    while (at < exec.size()) {
        const std::size_t start = exec.find_first_not_of(" \t", at);
        if (start == std::string_view::npos) break;
        const std::size_t stop = std::min(exec.find_first_of(" \t", start), exec.size());
        auto filled = detail::exec_word_filled(exec.substr(start, stop - start), path, took_path);
        if (!filled) return std::nullopt;
        if (!filled->empty()) command.words.push_back(std::move(*filled));
        at = stop;
    }
    if (command.words.empty()) return std::nullopt;
    if (!took_path) command.words.emplace_back(path);
    return command;
}

}  // namespace jgui::inline v001
