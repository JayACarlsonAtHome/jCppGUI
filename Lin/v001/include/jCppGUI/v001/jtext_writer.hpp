// File: include/jCppGUI/v001/jtext_writer.hpp
// Purpose: What writes a jText file, for the redraw monitor and the settings recorder alike: the clock and the sink they are given as
//          template parameters, FileSink, which saves a whole file by writing a new one and renaming it over the old, and a small
//          writer of lines and sections, with no jText library. It is not in jgui.hpp. All but FileSink and SteadyClock is
//          constexpr, so what is written runs in static_assert with a test clock and no file.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "error.hpp"
#include "geometry.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

// now_ns only ever goes up, and today is the date for the file's header.
template <class C>
concept MonitorClock = requires(C& clock) {
    { clock.now_ns() } -> std::same_as<std::int64_t>;
    { clock.today() } -> std::same_as<std::chrono::year_month_day>;
};

// save replaces what was saved before with the whole of the text, all or nothing, and file_name is the name it goes by.
template <class S>
concept MonitorSink = requires(S& sink, const S& constant, std::string_view text) {
    { sink.save(text) } -> std::same_as<std::expected<void, Error>>;
    { constant.file_name() } -> std::same_as<std::string_view>;
};

struct SteadyClock {
    [[nodiscard]] std::int64_t now_ns() const noexcept {
        const auto since = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(since).count();
    }
    [[nodiscard]] std::chrono::year_month_day today() const noexcept {
        return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    }
};

inline constexpr std::string_view default_redraw_file_name = "redraw_monitor.jtext";
inline constexpr std::string_view default_settings_file_name = "settings.jtext";

namespace detail {
// A file's name and nothing more, so it stays in the directory it was given for, and fit to stand in a jText line.
[[nodiscard]] constexpr bool plain_file_name_fits(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == ".." || !valid_utf8(name)) return false;
    return std::ranges::none_of(name, [](char c) { return c == '/' || static_cast<std::uint8_t>(c) < 0x20 || c == 0x7F; });
}
}  // namespace detail

// Saves to a file named in a directory, by writing a temporary file beside it and renaming that over it, so a reader
// sees a whole file or the one before. The directory is opened once, here, and held, so the file's place cannot change
// later. Anything wrong, including no base directory to be found, is an Error from save: this never ends the program.
// Defined in src/monitor/, and a program that uses it links jgui_monitor.
class FileSink {
public:
    // In the application's base directory, under the redraw monitor's name: see base_directory.hpp.
    FileSink();
    // In the application's base directory, under the name given.
    [[nodiscard]] static FileSink in_base_directory(std::string_view file_name);
    // In a directory the caller names, such as a test's own.
    explicit FileSink(std::string_view directory, std::string_view file_name = default_redraw_file_name);
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    FileSink(FileSink&& other) noexcept;
    FileSink& operator=(FileSink&&) = delete;
    ~FileSink();

    [[nodiscard]] std::expected<void, Error> save(std::string_view text);
    [[nodiscard]] std::string_view file_name() const noexcept { return name_; }

private:
    struct InBaseDirectory {};
    FileSink(InBaseDirectory, std::string_view file_name);
    void open_directory(const std::string& directory);

    std::string name_;
    int directory_{-1};
    std::optional<Error> refused_{};
};

namespace detail {

// The one reader of the number in a geometry value for these files: only to write it.
struct FileNumbers {
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Edge<Tag> value) noexcept {
        return value.value_;
    }
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Extent<Tag> value) noexcept {
        return value.value_;
    }
};

[[nodiscard]] constexpr std::string_view yes_no(bool value) noexcept { return value ? "Yes" : "No"; }

// A number as text, held right here, so a row needs no allocation for each.
class DecimalText {
public:
    template <std::integral I>
    constexpr explicit DecimalText(I value) {
        const bool negative = std::cmp_less(value, 0);
        // Negated as unsigned, so the lowest value of a signed type is safe.
        auto left = static_cast<std::uint64_t>(value);
        if (negative) left = std::uint64_t{0} - left;
        do {
            digits_[--start_] = static_cast<char>('0' + left % 10U);
            left /= 10U;
        } while (left != 0);
        if (negative) digits_[--start_] = '-';
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return std::string_view{digits_}.substr(start_); }

private:
    std::array<char, 21> digits_{};
    std::size_t start_{digits_.size()};
};

inline constexpr std::string_view default_splice = "\xE2\x96\xAE";   // U+25AE
inline constexpr std::string_view default_comment = "\xE2\x96\x8C";  // U+258C
// What the spec's reference writer falls back on, in its order. '[' is not here: as a splice it is reserved.
inline constexpr std::string_view fallback_marks = "#^%~!@$&*+=:;?\"'(),./<>\\_`{}|-";

// A line's formatter, which is the two characters that say how its data is split.
struct LineFormatter {
    std::string_view splice;
    std::string_view comment;
};

// Whether one value can stand in a line and read back as itself: UTF-8, no control character of any kind, so no line
// break, and no space around it, which a reader trims. The format has no escapes, so anything else is refused.
[[nodiscard]] constexpr bool value_fits_a_line(std::string_view value) noexcept {
    if (!valid_utf8(value)) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(value[i]);
        if (byte < 0x20 || byte == 0x7F) return false;
        if (byte == 0xC2 && i + 1 < value.size() && static_cast<std::uint8_t>(value[i + 1]) < 0xA0) return false;
    }
    return value.empty() || (value.front() != ' ' && value.back() != ' ');
}

[[nodiscard]] constexpr bool any_value_holds(std::span<const std::string_view> values, std::string_view text) noexcept {
    return std::ranges::any_of(values, [&](std::string_view value) { return value.find(text) != std::string_view::npos; });
}

// The next mark not in the data and not already taken. Empty when every one of them is in the data.
[[nodiscard]] constexpr std::string_view fallback_mark(std::span<const std::string_view> values, std::string_view taken) noexcept {
    for (std::size_t i = 0; i < fallback_marks.size(); ++i) {
        const std::string_view mark = fallback_marks.substr(i, 1);
        if (mark != taken && !any_value_holds(values, mark)) return mark;
    }
    return {};
}

// The spec's writer rule: the default, unless the line's data holds that character. Empty when no mark is left.
[[nodiscard]] constexpr std::optional<LineFormatter> choose_formatter(std::span<const std::string_view> values) noexcept {
    LineFormatter chosen{default_splice, default_comment};
    if (any_value_holds(values, default_comment)) chosen.comment = fallback_mark(values, {});
    if (any_value_holds(values, default_splice)) chosen.splice = fallback_mark(values, chosen.comment);
    if (chosen.splice.empty() || chosen.comment.empty()) return std::nullopt;
    return chosen;
}

// Appends one line of values, starting at that field position. False, with nothing appended, when the line cannot be
// written so that it reads back as these values.
[[nodiscard]] constexpr bool append_line(std::string& out, std::size_t position, std::span<const std::string_view> values) {
    if (!std::ranges::all_of(values, value_fits_a_line)) return false;
    const std::optional<LineFormatter> formatter = choose_formatter(values);
    if (!formatter) return false;
    if (position < 10) out += ' ';
    out += DecimalText{position}.view();
    out += ". ";
    out += formatter->splice;
    out += formatter->comment;
    out += ' ';
    bool first = true;
    for (const std::string_view value : values) {
        if (!first) out += formatter->splice;
        first = false;
        out += value;
    }
    out += '\n';
    return true;
}

// One field of a table, as the Fields block declares it.
struct FieldDeclaration {
    std::string_view name;
    std::string_view type;
};

// A whole section: its banner, its fields and its rows, with every block closed. Field 1 is never empty or null.
[[nodiscard]] constexpr std::string section_text(std::string_view name, std::span<const FieldDeclaration> fields, std::string_view rows) {
    std::string out;
    out.reserve(rows.size() + 1024);
    out += "=== Section: ";
    out += name;
    out += " ===\n=== Fields ===\n";
    std::size_t position = 0;
    for (const FieldDeclaration& field : fields) {
        ++position;
        const std::array<std::string_view, 2> plain{field.name, field.type};
        const std::array<std::string_view, 4> required{field.name, field.type, "Not Null", "Not Empty"};
        const bool written = position == 1 ? append_line(out, position, required) : append_line(out, position, plain);
        require(written, "a jText field declaration cannot be written");
    }
    out += "=== End Fields ===\n=== Data ===\n";
    out += rows;
    out += "=== End Data ===\n=== End Section ===\n";
    return out;
}

[[nodiscard]] constexpr std::string date_text(std::chrono::year_month_day date) {
    const auto two_digits = [](unsigned value, std::string& out) {
        if (value < 10) out += '0';
        out += DecimalText{value}.view();
    };
    std::string out{DecimalText{static_cast<int>(date.year())}.view()};
    out += '-';
    two_digits(static_cast<unsigned>(date.month()), out);
    out += '-';
    two_digits(static_cast<unsigned>(date.day()), out);
    return out;
}

// What a file's header says about the file.
struct FileHeader {
    std::string_view file_name;
    std::string_view date;
    std::string_view related_type;
    std::string_view title;
};

// The comment lines and the header block, in the full profile. A value that cannot stand in a line is left out of it.
constexpr void append_file_header(std::string& out, const FileHeader& header) {
    out += "//File:    ";
    out += header.file_name;
    out += "\n//Date:    ";
    out += header.date;
    out += "\n//Purpose: jText Data File\n//Related: type=";
    out += header.related_type;
    out += "\n//\n=== jText File ===\n";
    const auto one = [&](std::size_t position, std::string_view value) {
        const std::array<std::string_view, 1> values{value};
        static_cast<void>(append_line(out, position, values));
    };
    one(1, header.file_name);
    one(2, header.date);
    one(3, header.title);
    one(4, "0.8");
    one(5, "Sensitive");
    one(8, "true");
}

}  // namespace detail

}  // namespace jgui::inline v001
