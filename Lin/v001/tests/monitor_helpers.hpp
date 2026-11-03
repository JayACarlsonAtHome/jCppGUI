// File: tests/monitor_helpers.hpp
// Purpose: What the redraw monitor's tests share: a clock that says what it is told, a sink that keeps the text and can be
//          told to fail, a compile-time shaper, a reader for the lines of the file, to see what a reader would, a
//          temporary directory, and a way to start a program from a working directory of its own.
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "jCppGUI/v001/redraw_monitor.hpp"

namespace jgui_test {

// Each reading moves on by the next step queued, or stays where it is when none is left. So a test queues one step
// for each reading a frame makes, and knows every number the monitor will record.
struct TestClock {
    std::vector<std::int64_t> steps{};
    std::size_t next{};
    std::int64_t now{};

    constexpr void queue(std::initializer_list<std::int64_t> more) { steps.insert(steps.end(), more); }
    [[nodiscard]] constexpr std::int64_t now_ns() {
        if (next < steps.size()) now += steps[next++];
        return now;
    }
    [[nodiscard]] constexpr std::chrono::year_month_day today() const noexcept {
        return std::chrono::year{2026} / std::chrono::month{11} / std::chrono::day{2};
    }
};

struct MemorySink {
    std::string saved{};
    int saves{};
    int attempts{};
    bool fail{};

    [[nodiscard]] constexpr std::expected<void, jgui::Error> save(std::string_view text) {
        ++attempts;
        if (fail) return jgui::make_error(jgui::ErrorCode::System, "this sink was told to fail");
        saved = text;
        ++saves;
        return {};
    }
    [[nodiscard]] constexpr std::string_view file_name() const noexcept { return "test.jtext"; }
};

// One line of text per caption byte, four rows high, solid.
struct BlockShaper {
    [[nodiscard]] constexpr std::expected<jgui::TextRun, jgui::Error> shape(std::string_view text) const {
        const jgui::Size size{jgui::Width{jgui::saturate_to<std::int32_t>(text.size() * 2)}, jgui::Height{4}};
        return jgui::TextRun{size, std::vector<std::uint8_t>(text.size() * 2 * 4, 255)};
    }
    [[nodiscard]] constexpr jgui::Height line_height() const noexcept { return jgui::Height{4}; }
};

// How long a UTF-8 character is, from its first byte.
[[nodiscard]] constexpr std::size_t utf8_length(char first) noexcept {
    const auto byte = static_cast<std::uint8_t>(first);
    if (byte < 0x80) return 1;
    if (byte < 0xE0) return 2;
    return byte < 0xF0 ? 3 : 4;
}

// A compact row's values, as the spec reads them: the number, the formatter's two characters, then split on the first.
[[nodiscard]] constexpr std::vector<std::string> values_of_line(std::string_view line) {
    std::vector<std::string> values;
    std::size_t at = line.find(". ");
    if (at == std::string_view::npos) return values;
    at += 2;
    const std::size_t splice_size = utf8_length(line[at]);
    const std::string_view splice = line.substr(at, splice_size);
    at += splice_size;
    at += utf8_length(line[at]);
    if (at < line.size()) ++at;
    std::string_view rest = line.substr(at);
    while (true) {
        const std::size_t cut = rest.find(splice);
        values.emplace_back(rest.substr(0, cut));
        if (cut == std::string_view::npos) return values;
        rest.remove_prefix(cut + splice.size());
    }
}

// Each line of a section's Data block, read into its values. Empty when there is no such section.
[[nodiscard]] constexpr std::vector<std::vector<std::string>> rows_of_section(std::string_view text, std::string_view section) {
    std::vector<std::vector<std::string>> rows;
    const std::string banner = "=== Section: " + std::string{section} + " ===\n";
    std::size_t at = text.find(banner);
    if (at == std::string_view::npos) return rows;
    at = text.find("=== Data ===\n", at);
    if (at == std::string_view::npos) return rows;
    at += std::string_view{"=== Data ===\n"}.size();
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string_view line = text.substr(at, end - at);
        if (line == "=== End Data ===" || end == std::string_view::npos) return rows;
        rows.push_back(values_of_line(line));
        at = end + 1;
    }
    return rows;
}

// A directory for one test, gone at the end of it.
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = (std::filesystem::temp_directory_path() / "jcppgui-monitor-XXXXXX").string();
        if (::mkdtemp(path_.data()) == nullptr) path_.clear();
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::permissions(path_, std::filesystem::perms::owner_all, ignored);
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path file() const { return std::filesystem::path{path_} / jgui::default_redraw_file_name; }
    [[nodiscard]] std::filesystem::path temporary() const { return std::filesystem::path{path_} / "redraw_monitor.jtext.tmp"; }

private:
    std::string path_{};
};

// Starts a program from a working directory and waits for it, for its exit status, or -1 if it died or could not start.
// It is told a name of the caller's choosing as its argv[0]. Its error stream goes to a file when one is named.
[[nodiscard]] inline int run_program(const std::filesystem::path& program, std::string name, const std::filesystem::path& working_directory,
                                     std::vector<std::string> arguments, const std::filesystem::path& error_file = {}) {
    std::vector<char*> pointers{name.data()};
    for (std::string& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    std::cout.flush();
    std::cerr.flush();
    const pid_t child = ::fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (::chdir(working_directory.c_str()) != 0) std::_Exit(126);
        if (!error_file.empty()) {
            const int file = ::open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
            if (file < 0 || ::dup2(file, STDERR_FILENO) < 0) std::_Exit(126);
        }
        ::execv(program.c_str(), pointers.data());
        std::_Exit(127);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

// Every block opened is closed, in order, and the last line closes the file. A file cut short fails this.
[[nodiscard]] constexpr bool blocks_all_close(std::string_view text) {
    int sections = 0;
    int fields = 0;
    int data = 0;
    int files = 0;
    std::string_view last;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) return false;
        last = text.substr(at, end - at);
        at = end + 1;
        if (last == "=== jText File ===") ++files;
        if (last == "=== End File ===") --files;
        if (last.starts_with("=== Section: ")) ++sections;
        if (last == "=== End Section ===") --sections;
        if (last == "=== Fields ===") ++fields;
        if (last == "=== End Fields ===") --fields;
        if (last == "=== Data ===") ++data;
        if (last == "=== End Data ===") --data;
        if (sections < 0 || sections > 1 || fields < 0 || fields > 1 || data < 0 || data > 1) return false;
    }
    return sections == 0 && fields == 0 && data == 0 && files == 0 && last == "=== End File ===";
}

}  // namespace jgui_test
