// File: include/jCppGUI/v001/jtext.hpp
// Purpose: Reads the full profile of a jText file, which is untrusted input. Parsing takes a string_view and is constexpr, so
//          it runs in static_assert. The result owns a copy of the text and holds offsets into it. Anything the format
//          does not allow, or this reader does not do yet, is an Error naming the line, and never a guess. No error
//          message quotes the file. Small helpers in the style of Qlite's get_top_one family read what was parsed, and
//          get_typed_rows fills a program's own Row. This is not in jgui.hpp. read_jtext_file is defined in
//          src/jtext/, and a program that uses it links jgui_jtext. Parsing needs no library.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cell_text.hpp"
#include "error.hpp"
#include "geometry.hpp"
#include "jtext_characters.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

// How much of a file is taken in, each limit its own error. A line holds at most the whole file. A field number stops at 99, as
// the format says, so a section has at most 99 fields.
struct JTextLimits {
    std::size_t file_bytes{std::size_t{64} * 1024 * 1024};  // and never over 4 GiB, because offsets are 32 bits
    std::size_t sections{256};
    std::size_t rows_in_a_section{1'000'000};
    std::size_t cells_in_the_file{16'000'000};  // rows times declared fields, so a short file cannot ask for much memory
    std::size_t name_bytes{256};                // a section's or a field's name
};

inline constexpr std::size_t jtext_most_fields = 99;
inline constexpr std::size_t jtext_header_values = 9;

// Part of the file's text.
struct JTextSpan {
    std::uint32_t offset{};
    std::uint32_t size{};
};

struct JTextSectionData {
    JTextSpan name{};
    std::vector<JTextSpan> fields{};
    std::vector<JTextSpan> cells{};  // by row and then field
};

// One section of a file: a view, which needs the file to stay alive and where it is.
class JTextSection {
public:
    constexpr JTextSection(std::string_view text, const JTextSectionData& data) noexcept : text_{text}, data_{&data} {}

    [[nodiscard]] constexpr std::string_view name() const noexcept { return of(data_->name); }
    [[nodiscard]] constexpr std::size_t field_count() const noexcept { return data_->fields.size(); }
    [[nodiscard]] constexpr std::size_t row_count() const noexcept { return data_->fields.empty() ? 0 : data_->cells.size() / data_->fields.size(); }
    [[nodiscard]] constexpr std::string_view field_name(std::size_t field) const { return of(data_->fields.at(field)); }
    // By position, and the caller has checked both. Out of range aborts, in every build.
    [[nodiscard]] constexpr std::string_view cell(std::size_t row, std::size_t field) const { return of(data_->cells.at(row * field_count() + field)); }

private:
    [[nodiscard]] constexpr std::string_view of(JTextSpan span) const noexcept { return text_.substr(span.offset, span.size); }

    std::string_view text_;
    const JTextSectionData* data_;
};

namespace detail {

// --- The line grammar, one small piece at a time ---

[[nodiscard]] constexpr bool is_jtext_blank(char c) noexcept { return c == ' ' || c == '\t'; }

// Without the spaces and tabs around it, which are all that counts as whitespace in this format.
[[nodiscard]] constexpr std::string_view trimmed_jtext(std::string_view text) noexcept {
    while (!text.empty() && is_jtext_blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_jtext_blank(text.back())) text.remove_suffix(1);
    return text;
}

// A control character other than a tab, in text already known to be UTF-8: C0, DEL or C1. No value can hold one, so no text from
// a file can break a line or a display. This also refuses U+001F, the null mark of files older than version 0.8.
[[nodiscard]] constexpr bool holds_jtext_control(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(text[i]);
        if ((byte < 0x20 && text[i] != '\t') || byte == 0x7F) return true;
        if (byte == 0xC2 && i + 1 < text.size() && static_cast<std::uint8_t>(text[i + 1]) < 0xA0) return true;
    }
    return false;
}

struct DecodedCharacter {
    char32_t code_point{};
    std::size_t bytes{};
};

// The first character of text already known to be UTF-8, which must not be empty.
[[nodiscard]] constexpr DecodedCharacter first_character(std::string_view text) noexcept {
    const auto byte = [&](std::size_t at) { return char32_t{static_cast<std::uint8_t>(text[at])}; };
    const char32_t lead = byte(0);
    if (lead < 0x80) return {lead, 1};
    if (lead < 0xE0) return {((lead & 0x1FU) << 6U) | (byte(1) & 0x3FU), 2};
    if (lead < 0xF0) return {((lead & 0x0FU) << 12U) | ((byte(1) & 0x3FU) << 6U) | (byte(2) & 0x3FU), 3};
    return {((lead & 0x07U) << 18U) | ((byte(1) & 0x3FU) << 12U) | ((byte(2) & 0x3FU) << 6U) | (byte(3) & 0x3FU), 4};
}

struct JTextFormatter {
    std::string_view splice{};
    std::string_view comment{};
};

// A marker's last two characters, because the format's older, longer markers are read by keeping those. Each of its characters
// must be punctuation or a symbol. An Error's text says what is wrong with it. The marker is UTF-8 already.
[[nodiscard]] constexpr std::expected<JTextFormatter, std::string_view> formatter_from_marker(std::string_view marker) {
    std::size_t last_at = 0;
    std::size_t before_last_at = 0;
    std::size_t characters = 0;
    for (std::size_t at = 0; at < marker.size();) {
        const DecodedCharacter character = first_character(marker.substr(at));
        if (!is_punctuation_or_symbol(character.code_point)) return std::unexpected(std::string_view{"a formatter holds a character that is not punctuation or a symbol"});
        before_last_at = last_at;
        last_at = at;
        at += character.bytes;
        ++characters;
    }
    if (characters < 2) return std::unexpected(std::string_view{"a formatter needs two characters"});
    const JTextFormatter formatter{marker.substr(before_last_at, last_at - before_last_at), marker.substr(last_at)};
    if (formatter.splice == "[" && formatter.comment == "]") return std::unexpected(std::string_view{"the raw form [] is not supported yet"});
    if (formatter.splice == "[" && formatter.comment == "*") return std::unexpected(std::string_view{"the multiline form [* is not supported yet"});
    if (formatter.splice == "[") return std::unexpected(std::string_view{"[ is reserved and cannot split a line"});
    if (formatter.splice == formatter.comment) return std::unexpected(std::string_view{"a formatter's two characters must differ"});
    return formatter;
}

// Where a comment starts in what follows the formatter: its marker at the very start, followed by a space or the end of the
// line, or later with a space on each side. Empty when there is none.
[[nodiscard]] constexpr std::optional<std::size_t> comment_start(std::string_view rest, std::string_view comment) noexcept {
    if (rest.starts_with(comment) && (rest.size() == comment.size() || rest[comment.size()] == ' ')) return 0;
    for (std::size_t at = rest.find(comment); at != std::string_view::npos; at = rest.find(comment, at + comment.size())) {
        const std::size_t after = at + comment.size();
        if (at > 0 && rest[at - 1] == ' ' && after < rest.size() && rest[after] == ' ') return at;
    }
    return std::nullopt;
}

// What a numbered line says, as views of that line.
struct JTextLine {
    std::size_t number{};
    std::string_view splice{};
    std::string_view data{};  // without the comment, and trimmed
};

// One line of the shape <padding><number>.<whitespace><formatter><whitespace><data>[ <comment>], which is UTF-8 already.
[[nodiscard]] constexpr std::expected<JTextLine, std::string_view> parsed_jtext_line(std::string_view line) {
    if (line.starts_with(' ')) line.remove_prefix(1);
    std::size_t digits = 0;
    while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
    if (digits == 0 || digits > 2) return std::unexpected(std::string_view{"a line must start with a number from 1 to 99"});
    const std::size_t number = digits == 1 ? static_cast<std::size_t>(line[0] - '0') : static_cast<std::size_t>(line[0] - '0') * 10 + static_cast<std::size_t>(line[1] - '0');
    if (number == 0) return std::unexpected(std::string_view{"a line must start with a number from 1 to 99"});
    if (digits == line.size() || line[digits] != '.') return std::unexpected(std::string_view{"a line's number must be followed by a period"});
    std::string_view rest = line.substr(digits + 1);
    if (rest.empty() || !is_jtext_blank(rest.front())) return std::unexpected(std::string_view{"a line's period must be followed by a space or a tab"});
    while (!rest.empty() && is_jtext_blank(rest.front())) rest.remove_prefix(1);
    std::size_t marker_size = 0;
    while (marker_size < rest.size() && !is_jtext_blank(rest[marker_size])) ++marker_size;
    const auto formatter = formatter_from_marker(rest.substr(0, marker_size));
    if (!formatter) return std::unexpected(formatter.error());
    rest.remove_prefix(marker_size);
    while (!rest.empty() && is_jtext_blank(rest.front())) rest.remove_prefix(1);
    if (const auto comment = comment_start(rest, formatter->comment); comment) rest = rest.substr(0, *comment);
    return JTextLine{number, formatter->splice, trimmed_jtext(rest)};
}

// The name between "=== Section:" and "===", trimmed, from a line already trimmed. Empty when the line is not that banner.
[[nodiscard]] constexpr std::string_view section_banner_name(std::string_view line) noexcept {
    constexpr std::string_view opening = "=== Section:";
    constexpr std::string_view closing = "===";
    if (!line.starts_with(opening) || !line.ends_with(closing) || line.size() < opening.size() + closing.size()) return {};
    return trimmed_jtext(line.substr(opening.size(), line.size() - opening.size() - closing.size()));
}

// A line that starts with one of these, in its first column as the format says. jText's own parser reads " # x" in a block as a broken line.
[[nodiscard]] constexpr bool is_jtext_comment_line(std::string_view line) noexcept { return line.starts_with('#') || line.starts_with("//"); }

// Levels of one value, in a field declaration or a header line: one splice at either end is decoration, and every level between
// must hold something. Hands each level to the visitor in turn, and the answer is how many there were, or empty on an empty one.
template <class Visitor>
[[nodiscard]] constexpr std::optional<std::size_t> for_each_level(std::string_view data, std::string_view splice, Visitor visit) {
    if (data.starts_with(splice)) data.remove_prefix(splice.size());
    if (data.ends_with(splice)) data.remove_suffix(splice.size());
    std::size_t count = 0;
    while (true) {
        const std::size_t cut = data.find(splice);
        const std::string_view level = trimmed_jtext(data.substr(0, cut));
        if (level.empty()) return std::nullopt;
        visit(level);
        ++count;
        if (cut == std::string_view::npos) return count;
        data.remove_prefix(cut + splice.size());
    }
}

// --- The file's structure ---

struct JTextStructure {
    std::array<JTextSpan, jtext_header_values> header{};
    std::vector<JTextSectionData> sections{};
};

// Walks a file line by line. Every path through it either moves on to the next line or ends with an Error.
class JTextParser {
public:
    constexpr JTextParser(std::string_view text, const JTextLimits& limits) noexcept : text_{text}, limits_{limits} {}

    [[nodiscard]] constexpr std::expected<JTextStructure, Error> run() {
        if (text_.size() > limits_.file_bytes) return make_error(ErrorCode::SizeLimit, "a jText file is over the limit of " + decimal_text(limits_.file_bytes, false) + " bytes");
        if (std::cmp_greater(text_.size(), std::numeric_limits<std::uint32_t>::max())) return make_error(ErrorCode::SizeLimit, "a jText file is over the limit of 4 GiB");
        std::size_t at = text_.starts_with("\xEF\xBB\xBF") ? 3 : 0;  // a byte-order mark is skipped, as the format says
        while (at < text_.size()) {
            const std::size_t end = text_.find('\n', at);
            std::string_view line = text_.substr(at, end == std::string_view::npos ? end : end - at);
            at = end == std::string_view::npos ? text_.size() : end + 1;
            ++line_number_;
            if (line.ends_with('\r')) line.remove_suffix(1);
            if (auto taken = take_line(line); !taken) return std::unexpected(std::move(taken.error()));
        }
        if (state_ != State::Ended) return refuse_end();
        return std::move(result_);
    }

private:
    enum class State : std::uint8_t { Start, Header, Section, Fields, FieldsEnded, Data, DataEnded, Ended };

    [[nodiscard]] constexpr std::unexpected<Error> refuse(std::string_view why, ErrorCode code = ErrorCode::FileFormat) const {
        return make_error(code, "jText line " + decimal_text(line_number_, false) + ": " + std::string{why});
    }

    [[nodiscard]] constexpr std::unexpected<Error> refuse_end() const {
        switch (state_) {
            case State::Start: return make_error(ErrorCode::FileFormat, "this is not a jText file: it has no line saying so");
            case State::Header: return refuse("the file ends with its header open: no closing === End File ===");
            case State::Section:
            case State::FieldsEnded:
            case State::DataEnded: return refuse("the file ends with a section open: no closing === End Section ===");
            case State::Fields: return refuse("the file ends with a Fields block open: no closing === End Fields ===");
            case State::Data: return refuse("the file ends with a Data block open: no closing === End Data ===");
            case State::Ended:
            default: return refuse("the file is not closed");
        }
    }

    [[nodiscard]] constexpr JTextSpan span_of(std::string_view part) const noexcept {
        // Every part is a view of the text, except that an empty one has no place and needs none.
        if (part.empty()) return JTextSpan{};
        return JTextSpan{static_cast<std::uint32_t>(part.data() - text_.data()), static_cast<std::uint32_t>(part.size())};
    }

    [[nodiscard]] constexpr std::string_view text_of(JTextSpan span) const noexcept { return text_.substr(span.offset, span.size); }

    [[nodiscard]] constexpr std::expected<void, Error> take_line(std::string_view line) {
        if (!valid_utf8(line)) return refuse("this line is not valid UTF-8", ErrorCode::InvalidUtf8);
        if (holds_jtext_control(line)) return refuse("this line holds a control character");
        const std::string_view trimmed = trimmed_jtext(line);
        if (trimmed.empty()) {
            record_open_ = false;
            return {};
        }
        if (state_ == State::Start) return take_first_line(line, trimmed);
        if (is_jtext_comment_line(line)) return {};
        if (trimmed.starts_with("===")) return take_marker(trimmed);
        if (state_ == State::Header) return take_header_line(line);
        if (state_ == State::Fields) return take_field_line(line);
        if (state_ == State::Data) return take_data_line(line);
        return refuse("a line stands outside any block");
    }

    // What the first lines say decides whether this reader will go on at all.
    [[nodiscard]] constexpr std::expected<void, Error> take_first_line(std::string_view line, std::string_view trimmed) {
        if (trimmed.starts_with("# JText File") || (trimmed.starts_with("-- ") && trimmed.ends_with(" --"))) return refuse("this is a light-profile jText file, which is not supported yet");
        if (is_jtext_comment_line(line)) return {};
        if (trimmed == "=== jText File ===") {
            state_ = State::Header;
            return {};
        }
        if (trimmed.starts_with("=== Section:")) return refuse("a file with no === jText File === header is not supported");
        return refuse("this is not a jText file: it does not open with === jText File ===");
    }

    [[nodiscard]] constexpr std::expected<void, Error> take_marker(std::string_view marker) {
        if (marker == "=== Fields ===") return open_fields();
        if (marker == "=== End Fields ===") return close_block(State::Fields, State::FieldsEnded, "=== End Fields === with no Fields block open");
        if (marker == "=== Data ===") return open_data();
        if (marker == "=== End Data ===") return close_block(State::Data, State::DataEnded, "=== End Data === with no Data block open");
        if (marker == "=== End Section ===") return close_section();
        if (marker == "=== End File ===") return close_file();
        if (marker.starts_with("=== Section:")) return open_section(marker);
        if (marker.starts_with("=== <#include#>")) return refuse("an include is not supported yet, so this reader never opens another file");
        if (marker.starts_with("=== Template:")) return refuse("a template is not supported yet");
        if (marker == "=== jText File ===") return refuse("a second === jText File ===");
        return refuse("this is not a marker the format has");
    }

    [[nodiscard]] constexpr std::expected<void, Error> open_section(std::string_view marker) {
        if (state_ == State::Fields || state_ == State::Data || state_ == State::Section || state_ == State::FieldsEnded || state_ == State::DataEnded) return refuse("a section opens inside another that is not closed");
        if (state_ != State::Header) return refuse("a section opens after === End File ===");
        const std::string_view name = section_banner_name(marker);
        if (name.empty()) return refuse("a section needs a name, as === Section: name ===");
        if (name.size() > limits_.name_bytes) return refuse("a section's name is too long", ErrorCode::SizeLimit);
        if (result_.sections.size() >= limits_.sections) return refuse("too many sections", ErrorCode::SizeLimit);
        for (const JTextSectionData& section : result_.sections)
            if (text_of(section.name) == name) return refuse("two sections have the same name");
        result_.sections.push_back(JTextSectionData{.name = span_of(name)});
        state_ = State::Section;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> open_fields() {
        if (state_ != State::Section) return refuse("a Fields block must come first in a section, and only once");
        state_ = State::Fields;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> open_data() {
        if (state_ == State::Section) return refuse("a Data block needs a Fields block before it");
        if (state_ != State::FieldsEnded) return refuse("a Data block must follow a closed Fields block, and only once");
        state_ = State::Data;
        record_open_ = false;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> close_block(State open, State closed, std::string_view why) {
        if (state_ != open) return refuse(why);
        state_ = closed;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> close_section() {
        if (state_ == State::Fields) return refuse("a section closes with its Fields block open");
        if (state_ == State::Data) return refuse("a section closes with its Data block open");
        if (state_ != State::Section && state_ != State::FieldsEnded && state_ != State::DataEnded) return refuse("=== End Section === with no section open");
        state_ = State::Header;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> close_file() {
        if (state_ == State::Header) {
            state_ = State::Ended;
            return {};
        }
        if (state_ == State::Ended) return refuse("a second === End File ===");
        return refuse("the file closes with a section open");
    }

    // A header line's number names which value it holds. Numbers past the nine the format has are allowed and ignored.
    [[nodiscard]] constexpr std::expected<void, Error> take_header_line(std::string_view text) {
        if (!result_.sections.empty()) return refuse("a line stands between sections");
        const auto line = parsed_jtext_line(text);
        if (!line) return refuse(line.error());
        if (!line->data.empty() && !for_each_level(line->data, line->splice, [](std::string_view) {})) return refuse("a header line has an empty level");
        if (line->number <= jtext_header_values) {
            const std::size_t bit = std::size_t{1} << line->number;
            if ((header_seen_ & bit) != 0) return refuse("a header line's number is repeated");
            header_seen_ |= bit;
            result_.header[line->number - 1] = span_of(line->data);
        }
        return {};
    }

    // A field's number must be the next one, and its name and at least one more level must be there. Past the name, what the
    // levels say is left unread: typed rows are what decide how a value is read.
    [[nodiscard]] constexpr std::expected<void, Error> take_field_line(std::string_view text) {
        const auto line = parsed_jtext_line(text);
        if (!line) return refuse(line.error());
        JTextSectionData& section = result_.sections.back();
        if (line->number != section.fields.size() + 1) return refuse("a field's number must be the next one in order");
        std::string_view name{};
        const auto levels = for_each_level(line->data, line->splice, [&](std::string_view level) {
            if (name.empty()) name = level;
        });
        if (!levels) return refuse("a field declaration has an empty level");
        if (*levels < 2) return refuse("a field declaration needs a name and a type");
        if (name.size() > limits_.name_bytes) return refuse("a field's name is too long", ErrorCode::SizeLimit);
        for (const JTextSpan& field : section.fields)
            if (text_of(field) == name) return refuse("two fields of a section have the same name");
        section.fields.push_back(span_of(name));
        return {};
    }

    // Field 1 starts a record, and it holds one record's worth of empty values from then on, so a field left out of it is the
    // empty string, as the format says. A later line must be past what is already filled.
    [[nodiscard]] constexpr std::expected<void, Error> take_data_line(std::string_view text) {
        const auto line = parsed_jtext_line(text);
        if (!line) return refuse(line.error());
        JTextSectionData& section = result_.sections.back();
        const std::size_t fields = section.fields.size();
        if (line->number == 1) {
            if (auto started = start_record(section); !started) return std::unexpected(std::move(started.error()));
        } else if (!record_open_) {
            return refuse("a record must start at field 1");
        } else if (line->number <= fields_filled_) {
            return refuse("a line's number must be past the fields its record has filled");
        }
        std::size_t field = line->number - 1;
        std::string_view rest = line->data;
        const std::size_t row_start = section.cells.size() - fields;
        while (true) {
            if (field >= fields) return refuse("a line has more values than its section has fields");
            const std::size_t cut = rest.find(line->splice);
            section.cells[row_start + field] = span_of(trimmed_jtext(rest.substr(0, cut)));
            ++field;
            if (cut == std::string_view::npos) break;
            rest.remove_prefix(cut + line->splice.size());
        }
        fields_filled_ = field;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, Error> start_record(JTextSectionData& section) {
        const std::size_t fields = section.fields.size();
        if (fields == 0) return refuse("a line has more values than its section has fields");
        const std::size_t cells_after = cells_ + fields;
        if (cells_after > limits_.cells_in_the_file) return refuse("too many values in the file", ErrorCode::SizeLimit);
        if (section.cells.size() / fields >= limits_.rows_in_a_section) return refuse("too many rows in a section", ErrorCode::SizeLimit);
        cells_ = cells_after;
        section.cells.resize(section.cells.size() + fields);
        record_open_ = true;
        fields_filled_ = 0;
        return {};
    }

    std::string_view text_;
    JTextLimits limits_;
    JTextStructure result_{};
    State state_{State::Start};
    std::size_t line_number_{};
    std::size_t header_seen_{};
    std::size_t cells_{};
    std::size_t fields_filled_{};
    bool record_open_{};
};

// A text as a whole number of this type, and only if that is all it is: an optional sign and digits, with no space and no other
// character, and in range. An empty text is not a number. A minus sign is refused for an unsigned type, even on a zero.
template <std::integral T>
[[nodiscard]] constexpr std::optional<T> strict_whole_number(std::string_view text) noexcept {
    constexpr std::uint64_t most_ever = std::numeric_limits<std::uint64_t>::max();
    constexpr auto most = static_cast<std::uint64_t>(std::numeric_limits<T>::max());
    const bool negative = text.starts_with('-');
    if (negative || text.starts_with('+')) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    std::uint64_t magnitude = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (magnitude > (most_ever - digit) / 10) return std::nullopt;
        magnitude = magnitude * 10 + digit;
    }
    if (!negative) {
        if (magnitude > most) return std::nullopt;
        return static_cast<T>(magnitude);
    }
    if constexpr (std::signed_integral<T>) {
        if (magnitude > most + 1) return std::nullopt;
        // Negated as unsigned, so the lowest value of the type is safe.
        return static_cast<T>(std::uint64_t{0} - magnitude);
    } else {
        return std::nullopt;
    }
}

[[nodiscard]] constexpr bool same_ignoring_case(std::string_view text, std::string_view lower) noexcept {
    return std::ranges::equal(text, lower, {}, [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
}

// Every way the format writes a true or a false, in any case.
[[nodiscard]] constexpr std::optional<bool> strict_bool(std::string_view text) noexcept {
    if (same_ignoring_case(text, "yes") || same_ignoring_case(text, "true") || same_ignoring_case(text, "y") || same_ignoring_case(text, "t") || text == "1") return true;
    if (same_ignoring_case(text, "no") || same_ignoring_case(text, "false") || same_ignoring_case(text, "n") || same_ignoring_case(text, "f") || text == "0") return false;
    return std::nullopt;
}

}  // namespace detail

// A parsed file. It owns its text, and moving it keeps what it holds right.
class JTextFile {
public:
    [[nodiscard]] constexpr std::size_t section_count() const noexcept { return structure_.sections.size(); }
    [[nodiscard]] constexpr JTextSection section_at(std::size_t index) const { return JTextSection{text_, structure_.sections.at(index)}; }
    // A header line's value by its number, 1 to 9. Empty when the file has none.
    [[nodiscard]] constexpr std::string_view header_value(std::size_t number) const {
        const JTextSpan span = structure_.header.at(number - 1);
        return std::string_view{text_}.substr(span.offset, span.size);
    }

private:
    friend constexpr std::expected<JTextFile, Error> parse_owned_jtext(std::string text, const JTextLimits& limits);
    constexpr JTextFile(std::string text, detail::JTextStructure structure) noexcept : text_{std::move(text)}, structure_{std::move(structure)} {}

    std::string text_;
    detail::JTextStructure structure_;
};

// Takes the text it is given, so a whole file need not be copied.
[[nodiscard]] constexpr std::expected<JTextFile, Error> parse_owned_jtext(std::string text, const JTextLimits& limits = JTextLimits{}) {
    auto structure = detail::JTextParser{text, limits}.run();
    if (!structure) return std::unexpected(std::move(structure.error()));
    return JTextFile{std::move(text), std::move(*structure)};
}

// The parser itself takes a view. This keeps a copy of the text, so what comes back depends on nothing given.
[[nodiscard]] constexpr std::expected<JTextFile, Error> parse_jtext(std::string_view text, const JTextLimits& limits = JTextLimits{}) {
    if (text.size() > limits.file_bytes) return make_error(ErrorCode::SizeLimit, "a jText file is over the limit of " + decimal_text(limits.file_bytes, false) + " bytes");
    return parse_owned_jtext(std::string{text}, limits);
}

// Reads a whole file through read_at and parses it. What stands at the path itself must be a regular file: a symbolic link, a pipe, a
// directory or anything else is refused, as is a file over the limit, before any of it is read. Links in the folders above it are
// followed. Defined in src/jtext/, and linking jgui_jtext gives it.
[[nodiscard]] std::expected<JTextFile, Error> read_jtext_file(const std::string& path, const JTextLimits& limits = JTextLimits{});

// --- Helpers, each answering or saying why it cannot, and never guessing ---

[[nodiscard]] constexpr std::expected<JTextSection, Error> get_section(const JTextFile& file, std::string_view name) {
    for (std::size_t i = 0; i < file.section_count(); ++i)
        if (file.section_at(i).name() == name) return file.section_at(i);
    return make_error(ErrorCode::FileFormat, "the file has no section named " + std::string{name});
}

[[nodiscard]] constexpr std::expected<std::size_t, Error> get_field_index(const JTextSection& section, std::string_view field) {
    for (std::size_t i = 0; i < section.field_count(); ++i)
        if (section.field_name(i) == field) return i;
    return make_error(ErrorCode::FileFormat, "the section " + std::string{section.name()} + " has no field named " + detail::text_copied_by_character(field));
}

[[nodiscard]] constexpr std::size_t get_row_count(const JTextSection& section) noexcept { return section.row_count(); }

// A value's text, as a view of the file, which must stay alive and where it is.
[[nodiscard]] constexpr std::expected<std::string_view, Error> get_cell_string(const JTextSection& section, std::size_t row, std::string_view field) {
    const auto index = get_field_index(section, field);
    if (!index) return std::unexpected(index.error());
    if (row >= section.row_count()) return make_error(ErrorCode::FileFormat, "the section " + std::string{section.name()} + " has no row " + decimal_text(row, false));
    return section.cell(row, *index);
}

namespace detail {
// One more helper's way of reading a value: what is there, or an Error naming where.
template <class T, class Convert>
[[nodiscard]] constexpr std::expected<T, Error> converted_cell(const JTextSection& section, std::size_t row, std::string_view field, Convert convert, std::string_view wanted) {
    const auto text = get_cell_string(section, row, field);
    if (!text) return std::unexpected(text.error());
    const std::optional<T> value = convert(*text);
    if (!value) return make_error(ErrorCode::FileFormat, "the value of " + std::string{field} + " in row " + decimal_text(row, false) + " of " + std::string{section.name()} + " is not " + std::string{wanted});
    return *value;
}
}  // namespace detail

[[nodiscard]] constexpr std::expected<std::int64_t, Error> get_cell_int(const JTextSection& section, std::size_t row, std::string_view field) {
    return detail::converted_cell<std::int64_t>(section, row, field, detail::strict_whole_number<std::int64_t>, "a 64-bit whole number");
}

[[nodiscard]] constexpr std::expected<bool, Error> get_cell_bool(const JTextSection& section, std::size_t row, std::string_view field) {
    return detail::converted_cell<bool>(section, row, field, detail::strict_bool, "true or false");
}

// The first row's value: most of what a one-row section is for. No row is an Error, and not a made-up value.
[[nodiscard]] constexpr std::expected<std::string_view, Error> get_top_one_string(const JTextSection& section, std::string_view field) { return get_cell_string(section, 0, field); }
[[nodiscard]] constexpr std::expected<std::int64_t, Error> get_top_one_int(const JTextSection& section, std::string_view field) { return get_cell_int(section, 0, field); }

// --- Typed rows ---

// How the text of a value becomes a value of one type, chosen at compile time, and a program can add its own. Empty when it does not.
template <class T>
struct CellParse;

template <class T>
concept HasCellParse = requires(std::string_view text) {
    { CellParse<T>::from(text) } -> std::same_as<std::optional<T>>;
};

template <detail::CellNumber T>
struct CellParse<T> {
    [[nodiscard]] static constexpr std::optional<T> from(std::string_view text) noexcept { return detail::strict_whole_number<T>(text); }
};

template <>
struct CellParse<bool> {
    [[nodiscard]] static constexpr std::optional<bool> from(std::string_view text) noexcept { return detail::strict_bool(text); }
};

template <>
struct CellParse<std::string> {
    [[nodiscard]] static constexpr std::optional<std::string> from(std::string_view text) { return std::string{text}; }
};

template <class Tag>
struct CellParse<Edge<Tag>> {
    [[nodiscard]] static constexpr std::optional<Edge<Tag>> from(std::string_view text) noexcept {
        const std::optional<std::int32_t> number = detail::strict_whole_number<std::int32_t>(text);
        if (!number) return std::nullopt;
        return Edge<Tag>{*number};
    }
};

// A length is never negative, and a negative one in a file is a mistake rather than a zero.
template <class Tag>
struct CellParse<Extent<Tag>> {
    [[nodiscard]] static constexpr std::optional<Extent<Tag>> from(std::string_view text) noexcept {
        const std::optional<std::int32_t> number = detail::strict_whole_number<std::int32_t>(text);
        if (!number || *number < 0) return std::nullopt;
        return Extent<Tag>{*number};
    }
};

// What a section's rows came to: those that converted, and how many did not and so were left out.
template <class Row>
struct TypedRows {
    std::vector<Row> rows{};
    std::size_t left_out{};
};

// Fills one Row from each row of a section. Each column is a Column, the same type a DataGrid takes, and its name must be a field
// in the section, so what the program declares and what the file declares are matched here, by name. A column the file lacks is
// an Error. A field the program does not name is ignored. A row with a value that does not convert is left out and counted.
template <class Row, class... Columns>
[[nodiscard]] constexpr std::expected<TypedRows<Row>, Error> get_typed_rows(const JTextSection& section) {
    static_assert((std::same_as<typename Columns::Row, Row> && ...), "a column's member belongs to another row type");
    static_assert((HasCellParse<typename Columns::Value> && ...), "a column's member has no way to be read from text");
    std::array<std::size_t, sizeof...(Columns)> where{};
    std::size_t next = 0;
    std::optional<Error> missing;
    const auto find_one = [&](std::string_view name) {
        auto index = get_field_index(section, name);
        if (index) {
            where[next] = *index;
        } else if (!missing) {
            missing = std::move(index.error());
        }
        ++next;
    };
    (find_one(Columns::name()), ...);
    if (missing) return std::unexpected(std::move(*missing));

    TypedRows<Row> result;
    const std::size_t rows = section.row_count();
    result.rows.reserve(rows);
    for (std::size_t r = 0; r < rows; ++r) {
        Row row{};
        std::size_t column = 0;
        const auto fill_one = [&]<class C>(std::type_identity<C>) {
            auto value = CellParse<typename C::Value>::from(section.cell(r, where[column++]));
            if (!value) return false;
            C::member_of(row) = std::move(*value);
            return true;
        };
        if ((fill_one(std::type_identity<Columns>{}) && ...)) {
            result.rows.push_back(std::move(row));
        } else {
            ++result.left_out;
        }
    }
    return result;
}

namespace detail {
template <class Row, class Columns>
struct TypedRowsOf;
template <class Row, class... Columns>
struct TypedRowsOf<Row, std::tuple<Columns...>> {
    [[nodiscard]] static constexpr std::expected<TypedRows<Row>, Error> get(const JTextSection& section) { return get_typed_rows<Row, Columns...>(section); }
};
}  // namespace detail

// The same, for the row type and the columns of a DataGrid, so a grid's declaration is all a program writes.
template <class Grid>
[[nodiscard]] constexpr std::expected<TypedRows<typename Grid::row_type>, Error> get_rows_for_grid(const JTextSection& section) {
    return detail::TypedRowsOf<typename Grid::row_type, typename Grid::column_types>::get(section);
}

}  // namespace jgui::inline v001
