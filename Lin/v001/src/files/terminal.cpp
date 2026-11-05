// File: src/files/terminal.cpp
// Purpose: Starts other programs. A terminal running a command, or a program by itself, is started and not waited for: two forks, so it is nobody's child to collect and goes on by itself. What this program holds open does not go with it. If nothing can be
//          started, the last child says why through a pipe that closes by itself on a successful exec, so a missing program is an error here and not nothing happening. A quick question of a system tool is waited for, within a time, and its answer read.
#include <fcntl.h>
#include <linux/close_range.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <expected>
#include <optional>
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

[[nodiscard]] bool runs_as_a_program(const std::string& path) noexcept {
    struct stat found {};
    return ::stat(path.c_str(), &found) == 0 && S_ISREG(found.st_mode) && ::faccessat(AT_FDCWD, path.c_str(), X_OK, AT_EACCESS) == 0;
}

// What execvp takes. It points into the words, so those must outlive it.
[[nodiscard]] std::vector<char*> pointers_into(std::vector<std::string>& words) {
    std::vector<char*> pointers;
    pointers.reserve(words.size() + 1);
    for (std::string& word : words) pointers.push_back(word.data());
    pointers.push_back(nullptr);
    return pointers;
}

// Only what is safe after a fork happens from here on: no allocation, and no return to the caller.
[[noreturn]] void become_the_first_that_runs(const std::string& folder, std::span<std::vector<char*>> candidates, int report) noexcept {
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

// The first of these that can be started, going on by itself. What is wrong comes back as the system's own number, for the caller to word.
[[nodiscard]] std::expected<void, int> start_first_that_runs(const std::string& folder, std::vector<std::vector<std::string>>& words) {
    std::vector<std::vector<char*>> candidates;
    candidates.reserve(words.size());
    for (std::vector<std::string>& each : words) candidates.push_back(pointers_into(each));
    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return std::unexpected(errno);
    const UniqueFd read_end{ends[0]};
    UniqueFd write_end{ends[1]};
    const pid_t first = ::fork();
    if (first < 0) return std::unexpected(errno);
    if (first == 0) become_the_first_that_runs(folder, candidates, write_end.get());
    write_end = UniqueFd{};
    int status = 0;
    while (::waitpid(first, &status, 0) < 0) {
        if (errno != EINTR) return std::unexpected(errno);
    }
    std::array<int, 1> why{};
    const auto said = detail::read_stream<int>(read_end.get(), std::span<int>{why});
    if (said && *said == sizeof(int)) return std::unexpected(why[0]);
    return {};
}

// Whether the child ended well, by the deadline. One still going then is killed, so this always ends.
[[nodiscard]] bool ended_well_by(pid_t child, std::chrono::steady_clock::time_point deadline) noexcept {
    constexpr int look_again_ms = 10;
    int status = 0;
    while (true) {
        const pid_t found = ::waitpid(child, &status, WNOHANG);
        if (found == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (found < 0 && errno != EINTR) return false;
        if (std::chrono::steady_clock::now() >= deadline) {
            static_cast<void>(::kill(child, SIGKILL));
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            return false;
        }
        static_cast<void>(::poll(nullptr, 0, look_again_ms));
    }
}

}  // namespace

bool local_program_is_installed(const std::string& program) {
    if (program.contains('/')) return runs_as_a_program(program);
    const char* path = ::secure_getenv("PATH");
    if (path == nullptr) return false;
    for (const std::string& folder : absolute_path_folders(path)) {
        if (runs_as_a_program(child_of_path(folder, program))) return true;
    }
    return false;
}

std::string local_editor() {
    const char* named = ::secure_getenv("EDITOR");
    if (named == nullptr) return {};
    // One that cannot be read is left for terminal_tool_command to say why, because that is the person's mistake to see.
    const auto words = editor_words(named);
    if (words && !local_program_is_installed(words->front())) return {};
    return std::string{named};
}

std::expected<void, Error> run_in_local_terminal(const std::string& folder, std::span<const std::string> command) {
    if (!file_path_can_show(folder)) return make_error(ErrorCode::System, "That folder cannot be used");
    std::vector<std::vector<std::string>> words;
    const char* named = ::secure_getenv("TERMINAL");
    if (named != nullptr && named[0] != '\0') words.push_back(terminal_arguments(TerminalStyle{named, {"-e"}, 1}, command));
    for (const TerminalStyle& style : known_terminals) words.push_back(terminal_arguments(style, command));
    const auto started = start_first_that_runs(folder, words);
    if (started) return {};
    if (started.error() == ENOENT) return make_error(ErrorCode::System, "No terminal was found. Set TERMINAL to one, or install one such as ptyxis or xterm");
    return make_error(ErrorCode::System, "cannot start a terminal: " + detail::reason_of(started.error()));
}

std::expected<void, Error> start_local_program(const std::string& folder, std::span<const std::string> command) {
    if (!file_path_can_show(folder)) return make_error(ErrorCode::System, "That folder cannot be used");
    if (command.empty()) return make_error(ErrorCode::System, "There is no program to start");
    std::vector<std::vector<std::string>> words{std::vector<std::string>{command.begin(), command.end()}};
    const auto started = start_first_that_runs(folder, words);
    if (started) return {};
    return make_error(ErrorCode::System, "cannot start " + command.front() + ": " + detail::reason_of(started.error()));
}

std::optional<std::string> read_program_output(std::span<const std::string> command, std::chrono::milliseconds most_time) {
    if (command.empty()) return std::nullopt;
    std::vector<std::string> words{command.begin(), command.end()};
    std::vector<char*> pointers = pointers_into(words);
    std::array<int, 2> ends{-1, -1};
    if (::pipe2(ends.data(), O_CLOEXEC) != 0) return std::nullopt;
    const UniqueFd read_end{ends[0]};
    UniqueFd write_end{ends[1]};
    const auto deadline = std::chrono::steady_clock::now() + most_time;
    const pid_t child = ::fork();
    if (child < 0) return std::nullopt;
    if (child == 0) {
        // Only what is safe after a fork. An error message goes the same way, and an error's exit status means the text is not used.
        static_cast<void>(::dup2(write_end.get(), STDOUT_FILENO));
        static_cast<void>(::dup2(write_end.get(), STDERR_FILENO));
        static_cast<void>(::close_range(3, UINT_MAX, CLOSE_RANGE_CLOEXEC));
        ::execvp(pointers.front(), pointers.data());
        ::_exit(127);
    }
    write_end = UniqueFd{};
    std::string out;
    std::array<char, 1024> chunk{};
    bool whole = false;
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) break;
        pollfd watch{read_end.get(), POLLIN, 0};
        const int ready = ::poll(&watch, 1, saturate_to<int>(left));
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        const auto got = detail::read_stream<char>(read_end.get(), std::span<char>{chunk});
        if (!got) break;
        if (*got == 0) {
            whole = true;
            break;
        }
        if (*got > most_program_output_bytes - out.size()) break;
        out.append(chunk.data(), *got);
    }
    // Told to end now rather than at the deadline, when this has already stopped listening.
    const bool ended = ended_well_by(child, whole ? deadline : std::chrono::steady_clock::now());
    if (!whole || !ended) return std::nullopt;
    return out;
}

}  // namespace jgui::inline v001
