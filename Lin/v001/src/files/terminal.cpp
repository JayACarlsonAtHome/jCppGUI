// File: src/files/terminal.cpp
// Purpose: Starts a terminal running a command, and does not wait for it. Two forks, so it is nobody's child to collect and goes on by itself. What the program holds open does not go with it. If nothing can be
//          started, the last child says why through a pipe that closes by itself on a successful exec, so a missing terminal is an error here and not nothing happening.
#include <fcntl.h>
#include <linux/close_range.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "file_io.hpp"
#include "folder_names.hpp"
#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/terminal_command.hpp"

namespace jgui::inline v001 {

namespace {

using detail::UniqueFd;

// Only what is safe after a fork happens from here on: no allocation, and no return to the caller.
[[noreturn]] void become_a_terminal(const std::string& folder, std::span<std::vector<char*>> candidates, int report) noexcept {
    static_cast<void>(::setsid());
    const pid_t last = ::fork();
    if (last != 0) ::_exit(last < 0 ? 1 : 0);
    static_cast<void>(::close_range(3, UINT_MAX, CLOSE_RANGE_CLOEXEC));
    std::array<int, 1> why{EIO};
    if (::chdir(folder.c_str()) != 0) {
        why[0] = errno;
    } else {
        why[0] = ENOENT;
        for (std::vector<char*>& candidate : candidates) {
            ::execvp(candidate.front(), candidate.data());
            // One that is missing is not a failure while others are left. Anything else is the more useful thing to say.
            if (errno != ENOENT) why[0] = errno;
        }
    }
    static_cast<void>(detail::write_stream<int>(report, std::span<const int>{why}));
    ::_exit(127);
}

}  // namespace

std::string local_editor() {
    const char* named = ::secure_getenv("EDITOR");
    return named == nullptr ? std::string{} : std::string{named};
}

std::expected<void, Error> run_in_local_terminal(const std::string& folder, std::span<const std::string> command) {
    if (!file_path_can_show(folder)) return make_error(ErrorCode::System, "That folder cannot be used");
    std::vector<std::vector<std::string>> words;
    const char* named = ::secure_getenv("TERMINAL");
    if (named != nullptr && named[0] != '\0') words.push_back(terminal_arguments(TerminalStyle{named, {"-e"}, 1}, command));
    for (const TerminalStyle& style : known_terminals) words.push_back(terminal_arguments(style, command));
    std::vector<std::vector<char*>> candidates;
    candidates.reserve(words.size());
    for (std::vector<std::string>& each : words) {
        std::vector<char*>& pointers = candidates.emplace_back();
        pointers.reserve(each.size() + 1);
        for (std::string& word : each) pointers.push_back(word.data());
        pointers.push_back(nullptr);
    }
    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return make_error(ErrorCode::System, "cannot start a terminal: " + detail::reason_of(errno));
    const UniqueFd read_end{ends[0]};
    UniqueFd write_end{ends[1]};
    const pid_t first = ::fork();
    if (first < 0) return make_error(ErrorCode::System, "cannot start a terminal: " + detail::reason_of(errno));
    if (first == 0) become_a_terminal(folder, candidates, write_end.get());
    write_end = UniqueFd{};
    int status = 0;
    while (::waitpid(first, &status, 0) < 0) {
        if (errno != EINTR) return make_error(ErrorCode::System, "cannot start a terminal: " + detail::reason_of(errno));
    }
    std::array<int, 1> why{};
    const auto said = detail::read_stream<int>(read_end.get(), std::span<int>{why});
    if (said && *said == sizeof(int)) {
        if (why[0] == ENOENT) return make_error(ErrorCode::System, "No terminal was found. Set TERMINAL to one, or install one such as ptyxis or xterm");
        return make_error(ErrorCode::System, "cannot start a terminal: " + detail::reason_of(why[0]));
    }
    return {};
}

}  // namespace jgui::inline v001
