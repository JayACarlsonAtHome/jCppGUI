// File: tests/monitor_helpers.hpp
// Purpose: What the redraw monitor's tests share: a clock that says what it is told, a sink that keeps the text and can be
//          told to fail, a compile-time shaper, and a reader for the lines of the file, to see what a reader would.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
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
