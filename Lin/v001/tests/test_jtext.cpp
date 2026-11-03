// File: tests/test_jtext.cpp
// Purpose: The jText reader. In static_assert: each small helper, the whole grammar, every refusal and limit, typed rows, and
//          everything FrameMonitor writes reading back as the same values. At run time: reading through a real file, and what stands
//          at the path being refused. Each input a fuzz run found to crash is kept here.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "app_helpers.hpp"
#include "check.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/jtext.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "monitor_helpers.hpp"

namespace {

using namespace jgui;
using jgui_test::Checker;

// --- Small helpers ---

static_assert(detail::trimmed_jtext(" \t a b \t ") == "a b");
static_assert(detail::trimmed_jtext("\xC2\xA0x") == "\xC2\xA0x", "a no-break space is not whitespace here");
static_assert(detail::trimmed_jtext("   ").empty());

static_assert(!detail::holds_jtext_control("plain\ttab and \xE2\x96\xAE and \xC2\xA0"));
static_assert(detail::holds_jtext_control("a\x01z") && detail::holds_jtext_control("a\x7Fz") && detail::holds_jtext_control("a\xC2\x85z") && detail::holds_jtext_control("\x1F"));
static_assert(detail::holds_jtext_control(std::string_view{"\0", 1}));

static_assert(detail::first_character("a").code_point == U'a' && detail::first_character("a").bytes == 1);
static_assert(detail::first_character("\xC2\xA1").code_point == U'¡' && detail::first_character("\xC2\xA1").bytes == 2);
static_assert(detail::first_character("\xE2\x96\xAE").code_point == U'▮' && detail::first_character("\xE2\x96\xAE").bytes == 3);
static_assert(detail::first_character("\xF0\x9F\x98\x80").code_point == U'\U0001F600' && detail::first_character("\xF0\x9F\x98\x80").bytes == 4);

static_assert(detail::is_punctuation_or_symbol(U'#') && detail::is_punctuation_or_symbol(U'|') && detail::is_punctuation_or_symbol(U'▮') && detail::is_punctuation_or_symbol(U'▌'));
static_assert(detail::is_punctuation_or_symbol(U'!') && detail::is_punctuation_or_symbol(U'~') && detail::is_punctuation_or_symbol(U'\U0001F600'), "the table's ends and a far range");
static_assert(!detail::is_punctuation_or_symbol(U'a') && !detail::is_punctuation_or_symbol(U'7') && !detail::is_punctuation_or_symbol(U' ') && !detail::is_punctuation_or_symbol(U'‍'));
static_assert(!detail::is_punctuation_or_symbol(U'é') && !detail::is_punctuation_or_symbol(U'') && !detail::is_punctuation_or_symbol(U'\U0010FFFF') && !detail::is_punctuation_or_symbol(char32_t{0}));
static_assert(detail::punctuation_or_symbol_ranges.front().first == U'!');

static_assert(detail::comment_start("a # b", "#") == 2);
static_assert(detail::comment_start("# b", "#") == 0, "first, followed by a space");
static_assert(detail::comment_start("#", "#") == 0, "first, at the end of the line");
static_assert(!detail::comment_start("#b", "#"), "first, followed by text");
static_assert(!detail::comment_start("C#", "#") && !detail::comment_start("a #", "#"), "the end of a line is not a space");
static_assert(!detail::comment_start("a\t# b", "#") && !detail::comment_start("a #\tb", "#"), "a tab is not a space");
static_assert(detail::comment_start("C#x # y # z", "#") == 4, "the first with a space each side");
static_assert(detail::comment_start("x \xE2\x96\x8C y", "\xE2\x96\x8C") == 2);

[[nodiscard]] constexpr std::size_t levels_in(std::string_view data, std::string_view splice) { return detail::for_each_level(data, splice, [](std::string_view) {}).value_or(0); }
static_assert(levels_in("a/b/c", "/") == 3 && levels_in("/a/b/c/", "/") == 3 && levels_in(" a / b ", "/") == 2 && levels_in("a", "/") == 1);
static_assert(levels_in("a//b", "/") == 0 && levels_in("///", "/") == 0 && levels_in("a/ /b", "/") == 0 && levels_in("", "/") == 0 && levels_in("/", "/") == 0);

static_assert(detail::section_banner_name("=== Section: Frames ===") == "Frames");
static_assert(detail::section_banner_name("=== Section:Two words   ===") == "Two words");
static_assert(detail::section_banner_name("=== Section: ===").empty() && detail::section_banner_name("=== Section: x").empty() && detail::section_banner_name("=== Section:===").empty());

// --- Numbers and booleans are strict ---

static_assert(detail::strict_whole_number<std::int64_t>("9223372036854775807") == std::numeric_limits<std::int64_t>::max());
static_assert(detail::strict_whole_number<std::int64_t>("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
static_assert(!detail::strict_whole_number<std::int64_t>("9223372036854775808") && !detail::strict_whole_number<std::int64_t>("-9223372036854775809"));
static_assert(detail::strict_whole_number<std::uint64_t>("18446744073709551615") == std::numeric_limits<std::uint64_t>::max());
static_assert(!detail::strict_whole_number<std::uint64_t>("18446744073709551616") && !detail::strict_whole_number<std::uint64_t>("99999999999999999999999"));
static_assert(!detail::strict_whole_number<std::uint64_t>("-1") && !detail::strict_whole_number<std::uint64_t>("-0"));
static_assert(detail::strict_whole_number<std::int32_t>("+5") == 5 && detail::strict_whole_number<std::int32_t>("-5") == -5 && detail::strict_whole_number<std::int32_t>("007") == 7);
static_assert(detail::strict_whole_number<std::int32_t>("2147483647") == 2147483647 && !detail::strict_whole_number<std::int32_t>("2147483648"));
static_assert(!detail::strict_whole_number<std::int32_t>("") && !detail::strict_whole_number<std::int32_t>("-") && !detail::strict_whole_number<std::int32_t>("+") && !detail::strict_whole_number<std::int32_t>("+-1"));
static_assert(!detail::strict_whole_number<std::int32_t>("1x") && !detail::strict_whole_number<std::int32_t>(" 1") && !detail::strict_whole_number<std::int32_t>("1 ") && !detail::strict_whole_number<std::int32_t>("1.5") && !detail::strict_whole_number<std::int32_t>("{NULL}"));

static_assert(detail::strict_bool("Yes") == true && detail::strict_bool("TRUE") == true && detail::strict_bool("y") == true && detail::strict_bool("T") == true && detail::strict_bool("1") == true);
static_assert(detail::strict_bool("No") == false && detail::strict_bool("false") == false && detail::strict_bool("N") == false && detail::strict_bool("f") == false && detail::strict_bool("0") == false);
static_assert(!detail::strict_bool("") && !detail::strict_bool("yess") && !detail::strict_bool("2") && !detail::strict_bool(" yes"));

static_assert(CellParse<Width>::from("5") == Width{5} && !CellParse<Width>::from("-5"), "a negative length is a mistake, not a zero");
static_assert(CellParse<Left>::from("-5") == Left{-5} && !CellParse<Left>::from("2147483648"));
static_assert(CellParse<std::string>::from("").has_value(), "empty is a string");

// --- The line grammar ---

[[nodiscard]] constexpr bool line_says(std::string_view line, std::size_t number, std::string_view splice, std::string_view data) {
    const auto parsed = detail::parsed_jtext_line(line);
    return parsed && parsed->number == number && parsed->splice == splice && parsed->data == data;
}
[[nodiscard]] constexpr bool line_refused(std::string_view line, std::string_view why) {
    const auto parsed = detail::parsed_jtext_line(line);
    return !parsed && parsed.error().find(why) != std::string_view::npos;
}

static_assert(line_says(" 1. ?# value", 1, "?", "value"));
static_assert(line_says("1. ?# value", 1, "?", "value"), "no padding");
static_assert(line_says("99.\t\t?#  \t value  ", 99, "?", "value"), "whitespace, and the value trimmed");
static_assert(line_says("01. ?# v", 1, "?", "v"), "two digits");
static_assert(line_says(" 1. \xE2\x96\xAE\xE2\x96\x8C v", 1, "\xE2\x96\xAE", "v"), "by code point, not by byte");
static_assert(line_says(" 1. %^ v", 1, "%", "v"), "the monitor's fallback");
static_assert(line_says(" 1. #|# v", 1, "|", "v"), "an older marker keeps its right two");
static_assert(line_says(" 1. !@#$ v", 1, "#", "v"), "a longer run the same");
static_assert(line_says(" 1. ?#", 1, "?", ""), "no data");
static_assert(line_says(" 1. ?#    ", 1, "?", ""));
static_assert(line_says(" 4. ?# # Note", 4, "?", ""), "a comment and no data");
static_assert(line_says(" 4. ?# # # Note", 4, "?", ""));
static_assert(line_says(" 1. ?# C# compiler  # language name", 1, "?", "C# compiler"));
static_assert(line_says(" 1. ?# a #", 1, "?", "a #"), "the end of a line opens no comment");
static_assert(line_says(" 1. ?# a # ", 1, "?", "a"), "a space does");
static_assert(line_says(" 1. |# 1|b ?", 1, "|", "1|b ?"));
static_assert(line_says(" 1. \xE2\x9C\x93\xE2\x9C\x97 a\xE2\x9C\x93" "b", 1, "\xE2\x9C\x93", "a\xE2\x9C\x93" "b"), "any symbol");

static_assert(line_refused("  1. ?# v", "number"), "two spaces of padding");
static_assert(line_refused("\t1. ?# v", "number"));
static_assert(line_refused("0. ?# v", "number") && line_refused("00. ?# v", "number") && line_refused("100. ?# v", "number") && line_refused("x. ?# v", "number") && line_refused(". ?# v", "number"));
static_assert(line_refused("1 ?# v", "period") && line_refused("1", "period"));
static_assert(line_refused("1.?# v", "space or a tab") && line_refused("1.", "space or a tab"));
static_assert(line_refused("1. ", "two characters") && line_refused("1. ? v", "two characters"));
static_assert(line_refused("1. ## v", "differ") && line_refused("1. \xE2\x96\xAE\xE2\x96\xAE v", "differ"));
static_assert(line_refused("1. ?a v", "punctuation") && line_refused("1. a? v", "punctuation") && line_refused("1. ?#data", "punctuation"));
static_assert(line_refused("1. \xC2\xA0# v", "punctuation"), "a no-break space is not punctuation");
static_assert(line_refused("1. [] v", "raw") && line_refused("1. [* v", "multiline") && line_refused("1. [| v", "reserved"));

// --- Whole files ---

constexpr std::string_view small_file =
    "//File:    small.jtext\n//Date:    2026-11-03\n//Purpose: jText Data File\n//\n"
    "=== jText File ===\n 1. ?# small.jtext\n 2. ?# 2026-11-03\n 3. ?# for a test # with a comment\n 4. ?# 0.8\n"
    "=== Section: People ===\n=== Fields ===\n 1. /# id/Integer/Not Null\n 2. /# /name/String/\n 3. |# on|Bool # a comment\n=== End Fields ===\n"
    "=== Data ===\n"
    " 1. \xE2\x96\xAE\xE2\x96\x8C 1\xE2\x96\xAE" "ann\xE2\x96\xAEYes\n"
    " 1. |# 2| bob |no # about bob\n"
    " 1. ?# 3\n 3. ?# Y\n\n"
    " 1. %^ 4\n 2. ?# dee\n"
    "# a comment line, which is no separator\n"
    " 3. ?# 1\n"
    "=== End Data ===\n=== End Section ===\n"
    "=== Section: Empty ===\n=== End Section ===\n"
    "=== Section: No rows ===\n=== Fields ===\n 1. /# id/Integer\n=== End Fields ===\n=== End Section ===\n"
    "=== End File ===\n\n// the end\n# and more\n";

[[nodiscard]] constexpr bool cell_is(const JTextFile& file, std::string_view section, std::size_t row, std::string_view field, std::string_view text) {
    const auto found = get_section(file, section);
    if (!found) return false;
    const auto cell = get_cell_string(*found, row, field);
    return cell && *cell == text;
}

[[nodiscard]] constexpr bool small_file_reads() {
    const auto file = parse_jtext(small_file);
    if (!file || file->section_count() != 3) return false;
    if (file->header_value(1) != "small.jtext" || file->header_value(3) != "for a test" || file->header_value(9) != "") return false;
    const auto people = get_section(*file, "People");
    if (!people || people->field_count() != 3 || get_row_count(*people) != 4) return false;
    if (get_field_index(*people, "name") != 1 || get_field_index(*people, "on") != 2 || get_field_index(*people, "ON")) return false;
    if (!cell_is(*file, "People", 0, "name", "ann") || !cell_is(*file, "People", 1, "name", "bob") || !cell_is(*file, "People", 2, "name", "") || !cell_is(*file, "People", 3, "name", "dee")) return false;
    if (get_cell_int(*people, 3, "id") != 4) return false;
    if (get_cell_bool(*people, 0, "on") != true || get_cell_bool(*people, 1, "on") != false || get_cell_bool(*people, 2, "on") != true || get_cell_bool(*people, 3, "on") != true) return false;
    if (get_top_one_string(*people, "name") != "ann" || get_top_one_int(*people, "id") != 1) return false;
    const auto no_rows = get_section(*file, "No rows");
    const auto empty = get_section(*file, "Empty");
    if (!no_rows || !empty || get_row_count(*no_rows) != 0 || get_row_count(*empty) != 0 || empty->field_count() != 0) return false;
    if (get_top_one_string(*no_rows, "id") || get_top_one_int(*no_rows, "id")) return false;
    if (get_section(*file, "people") || get_section(*file, "")) return false;
    return true;
}
static_assert(small_file_reads());

// Answers that must be refusals are checked one by one.
[[nodiscard]] constexpr bool wrong_kinds_are_errors() {
    const auto file = parse_jtext(small_file);
    if (!file) return false;
    const auto people = get_section(*file, "People");
    if (!people) return false;
    return !get_cell_int(*people, 3, "name") && !get_cell_int(*people, 4, "id") && !get_cell_bool(*people, 3, "name") && !get_cell_int(*people, 0, "nobody") && !get_cell_string(*people, 99, "id");
}
static_assert(wrong_kinds_are_errors());

// A file moved somewhere else still reads, and a parse keeps nothing of what it was given.
[[nodiscard]] constexpr bool a_moved_file_still_reads() {
    std::string text{small_file};
    auto file = parse_jtext(text);
    if (!file) return false;
    text.assign(text.size(), 'x');
    const JTextFile moved = std::move(*file);
    return cell_is(moved, "People", 3, "name", "dee");
}
static_assert(a_moved_file_still_reads());

// A BOM, CRLF line ends, and no newline after the last line are all a whole file.
static_assert(parse_jtext("\xEF\xBB\xBF=== jText File ===\n=== End File ===").has_value());
static_assert(parse_jtext("=== jText File ===\r\n=== End File ===\r\n").has_value());
static_assert(parse_jtext("  === jText File ===  \n\n\t=== End File ===\t\n").has_value(), "whitespace around a marker is ignored");

[[nodiscard]] constexpr std::string one_section(std::string_view fields, std::string_view data) {
    std::string text{"=== jText File ===\n=== Section: S ===\n=== Fields ===\n"};
    text += fields;
    text += "=== End Fields ===\n=== Data ===\n";
    text += data;
    text += "=== End Data ===\n=== End Section ===\n=== End File ===\n";
    return text;
}
constexpr std::string_view two_fields = " 1. /# id/Integer\n 2. /# name/String\n";

// The Error says the line, with its reason. One refusal for each thing a file can get wrong.
[[nodiscard]] constexpr bool refused_on(std::string_view text, std::size_t line, std::string_view why, ErrorCode code = ErrorCode::FileFormat) {
    const auto file = parse_jtext(text);
    if (file || file.error().code != code) return false;
    return file.error().message.starts_with("jText line " + decimal_text(line, false) + ": ") && file.error().message.contains(why);
}
[[nodiscard]] constexpr bool refused_because(std::string_view text, std::string_view why, ErrorCode code = ErrorCode::FileFormat) {
    const auto file = parse_jtext(text);
    return !file && file.error().code == code && file.error().message.contains(why);
}

// --- What opens a file ---
static_assert(refused_because("", "not a jText file"), "nothing at all");
static_assert(refused_because("\n\n", "not a jText file"));
static_assert(refused_because("//File: x\n//\n", "not a jText file"), "a wrapper and nothing else");
static_assert(refused_on("hello\n", 1, "not a jText file"));
static_assert(refused_on("//File: x\n# JText File - created 2026-01-01\n", 2, "light-profile"));
static_assert(refused_on("-- Tools --\n", 1, "light-profile"));
static_assert(refused_on("=== Section: S ===\n", 1, "no === jText File === header"));
static_assert(refused_on("=== jtext file ===\n", 1, "not a jText file"), "markers are case-sensitive");

// --- Nothing unclosed, so a file cut short is never taken as whole ---
static_assert(refused_because("=== jText File ===\n", "header open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n", "section open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n=== Fields ===\n", "Fields block open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n", "section open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n=== Data ===\n", "Data block open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n=== Data ===\n=== End Data ===\n", "section open"));
static_assert(refused_because("=== jText File ===\n=== Section: S ===\n=== End Section ===\n", "header open"));

[[nodiscard]] constexpr bool every_cut_is_refused() {
    // The small file cut anywhere before its last marker ends is not whole.
    const std::size_t whole = small_file.find("=== End File ===") + std::string_view{"=== End File ==="}.size();
    for (std::size_t size = 0; size < whole; ++size)
        if (parse_jtext(small_file.substr(0, size))) return false;
    return parse_jtext(small_file.substr(0, whole)).has_value();
}
static_assert(every_cut_is_refused());

// --- Blocks in the wrong place ---
static_assert(refused_on("=== jText File ===\n=== jText File ===\n", 2, "second"));
static_assert(refused_on("=== jText File ===\n=== Unknown ===\n", 2, "not a marker"));
static_assert(refused_on("=== jText File ===\n=== End Section ===\n", 2, "no section open"));
static_assert(refused_on("=== jText File ===\n=== End Data ===\n", 2, "no Data block"));
static_assert(refused_on("=== jText File ===\n=== End Fields ===\n", 2, "no Fields block"));
static_assert(refused_on("=== jText File ===\n=== Data ===\n", 2, "closed Fields block"));
static_assert(refused_on("=== jText File ===\n=== Fields ===\n", 2, "must come first"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Data ===\n", 3, "needs a Fields block"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Section: T ===\n", 3, "not closed"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== End File ===\n", 3, "section open"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Section ===\n", 4, "Fields block open"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n=== Fields ===\n", 5, "only once"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n=== Data ===\n=== End Section ===\n", 6, "Data block open"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== End Fields ===\n=== Data ===\n=== End Data ===\n=== Data ===\n", 7, "only once"));
static_assert(refused_on("=== jText File ===\n=== Section: ===\n", 2, "needs a name"));
static_assert(refused_on("=== jText File ===\n=== Section: A ===\n=== End Section ===\n=== Section: A ===\n", 4, "same name"));
static_assert(refused_on("=== jText File ===\n=== End File ===\n=== End File ===\n", 3, "second"));
static_assert(refused_on("=== jText File ===\n=== End File ===\n=== Section: S ===\n", 3, "after === End File ==="));
static_assert(refused_on("=== jText File ===\n=== End File ===\n 1. ?# x\n", 3, "outside any block"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n 1. ?# x\n", 3, "outside any block"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== End Section ===\n 1. ?# x\n", 4, "between sections"));
static_assert(refused_on("=== jText File ===\nwords\n", 2, "number"), "a stray line in the header");

// --- What this reader does not do yet, and says so ---
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== <#include#> Fields: f.jtFlds ===\n", 3, "include"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Fields ===\n=== <#include#> Fields: f.jtFlds ===\n", 4, "include"));
static_assert(refused_on("=== jText File ===\n=== Section: S ===\n=== Template: T ===\n", 3, "template"));
static_assert(refused_on(one_section(two_fields, " 1. [] raw\n"), 8, "raw"));
static_assert(refused_on(one_section(two_fields, " 1. [*\n"), 8, "multiline"));

// --- Fields ---
static_assert(refused_on(one_section(" 2. /# id/Integer\n", ""), 4, "next one"), "a gap");
static_assert(refused_on(one_section(" 1. /# id/Integer\n 1. /# id2/Integer\n", ""), 5, "next one"), "a repeat");
static_assert(refused_on(one_section(" 1. /# id\n", ""), 4, "name and a type"));
static_assert(refused_on(one_section(" 1. /#\n", ""), 4, "empty level"));
static_assert(refused_on(one_section(" 1. /# a//b\n", ""), 4, "empty level"));
static_assert(refused_on(one_section(" 1. /# id/Integer\n 2. /# id/Integer\n", ""), 5, "same name"));
static_assert(refused_on(one_section(" 1. ## id#Integer\n", ""), 4, "differ"));
static_assert(refused_on("=== jText File ===\n 1. /# a//b\n", 2, "empty level"), "and a header line");
static_assert(refused_on("=== jText File ===\n 1. ?# a\n 1. ?# b\n", 3, "repeated"));
static_assert(parse_jtext("=== jText File ===\n 3. ?#\n50. ?# ignored\n50. ?# again\n=== End File ===\n").has_value(), "numbers past nine are allowed");

// --- Data ---
static_assert(refused_on(one_section(two_fields, " 2. ?# no id\n"), 8, "start at field 1"));
static_assert(refused_on(one_section(two_fields, " 1. ?# 1\n\n 2. ?# after a blank line\n"), 10, "start at field 1"));
static_assert(refused_on(one_section(two_fields, " 1. |# 1|a|too many\n"), 8, "more values"));
static_assert(refused_on(one_section(two_fields, " 1. |# 1|a\n 2. ?# b\n"), 9, "past the fields"));
static_assert(refused_on(one_section(two_fields, " 1. ?# 1\n 2. |# a|b\n"), 9, "more values"));
static_assert(refused_on(one_section("", " 1. ?# 1\n"), 6, "more values"), "no fields at all");
static_assert(refused_on(one_section(two_fields, " 1. ## 1\n"), 8, "differ"));
static_assert(refused_on(one_section(two_fields, " 1. ?a 1\n"), 8, "punctuation"));

// --- Bytes ---
static_assert(refused_on(one_section(two_fields, " 1. ?# \xFF\n"), 8, "UTF-8", ErrorCode::InvalidUtf8));
static_assert(refused_on(one_section(two_fields, " 1. ?# \xC0\x80\n"), 8, "UTF-8", ErrorCode::InvalidUtf8), "an overlong NUL");
static_assert(refused_on("=== jText File ===\n# a comment \xED\xA0\x80\n", 2, "UTF-8", ErrorCode::InvalidUtf8), "a surrogate, even in a comment");
static_assert(refused_on(one_section(two_fields, " 1. ?# a\x01z\n"), 8, "control"));
static_assert(refused_on(one_section(two_fields, " 1. ?# a\xC2\x85z\n"), 8, "control"), "C1");
static_assert(refused_on(one_section(two_fields, " 1. |# 1|\x1F\n"), 8, "control"), "the old null mark is not guessed at");
static_assert(refused_on(one_section(two_fields, " 1. ?# a\rb\n"), 8, "control"), "a CR that is not a line's end");
static_assert(refused_on(std::string{"=== jText File ===\n"} + std::string(std::size_t{1}, '\0') + "\n", 2, "control"));

// --- Limits ---
[[nodiscard]] constexpr bool refused_with_limits(std::string_view text, const JTextLimits& limits, std::string_view why) {
    const auto file = parse_jtext(text, limits);
    return !file && file.error().code == ErrorCode::SizeLimit && file.error().message.contains(why);
}
[[nodiscard]] constexpr JTextLimits tight_limits() {
    return JTextLimits{.file_bytes = 4096, .sections = 2, .rows_in_a_section = 2, .cells_in_the_file = 8, .name_bytes = 4};
}
[[nodiscard]] constexpr std::string sections_named(std::initializer_list<std::string_view> names) {
    std::string text{"=== jText File ===\n"};
    for (const std::string_view name : names) {
        text += "=== Section: ";
        text += name;
        text += " ===\n=== End Section ===\n";
    }
    return text + "=== End File ===\n";
}
static_assert(JTextLimits{}.file_bytes == 64 * 1024 * 1024 && JTextLimits{}.sections == 256 && JTextLimits{}.rows_in_a_section == 1'000'000 && JTextLimits{}.cells_in_the_file == 16'000'000 && JTextLimits{}.name_bytes == 256);
static_assert(JTextLimits{}.rows_in_a_section <= max_grid_rows, "every row a file can hold fits a grid");
static_assert(refused_with_limits(small_file, JTextLimits{.file_bytes = 10}, "limit"), "the whole file");
static_assert(parse_jtext(small_file, JTextLimits{.file_bytes = small_file.size()}).has_value(), "and exactly at it");
static_assert(!parse_jtext(small_file, JTextLimits{.file_bytes = small_file.size() - 1}).has_value());
static_assert(parse_jtext(sections_named({"a", "b"}), tight_limits()).has_value());
static_assert(refused_with_limits(sections_named({"a", "b", "c"}), tight_limits(), "too many sections"));
static_assert(parse_jtext(sections_named({"abcd"}), tight_limits()).has_value());
static_assert(refused_with_limits(sections_named({"abcde"}), tight_limits(), "name is too long"));
static_assert(parse_jtext(one_section(" 1. /# abcd/Integer\n", ""), tight_limits()).has_value());
static_assert(refused_with_limits(one_section(" 1. /# abcde/Integer\n", ""), tight_limits(), "name is too long"));
static_assert(parse_jtext(one_section(two_fields, " 1. ?# 1\n 1. ?# 2\n"), tight_limits()).has_value());
static_assert(refused_with_limits(one_section(two_fields, " 1. ?# 1\n 1. ?# 2\n 1. ?# 3\n"), tight_limits(), "too many rows"));
static_assert(refused_with_limits(
    "=== jText File ===\n=== Section: A ===\n=== Fields ===\n 1. /# a/I\n 2. /# b/I\n 3. /# c/I\n=== End Fields ===\n=== Data ===\n 1. ?# 1\n 1. ?# 2\n=== End Data ===\n=== End Section ===\n"
    "=== Section: B ===\n=== Fields ===\n 1. /# a/I\n 2. /# b/I\n 3. /# c/I\n=== End Fields ===\n=== Data ===\n 1. ?# 1\n=== End Data ===\n=== End Section ===\n=== End File ===\n",
    tight_limits(), "too many values"), "cells across sections");

[[nodiscard]] constexpr std::string fields_up_to(std::size_t count) {
    std::string text;
    for (std::size_t i = 1; i <= count; ++i) text += decimal_text(i, false) + ". /# f" + decimal_text(i, false) + "/Integer\n";
    return text;
}
static_assert(parse_jtext(one_section(fields_up_to(99), "")).has_value(), "99 fields, the most there can be");
static_assert(refused_on(one_section(fields_up_to(100), ""), 3 + 100, "from 1 to 99"));

// --- Typed rows ---

struct Person {
    std::int32_t id{};
    std::string name{};
    bool on{};
    friend constexpr bool operator==(const Person&, const Person&) = default;
};
using IdColumn = Column<"id", &Person::id>;
using NameColumn = Column<"name", &Person::name>;
using OnColumn = Column<"on", &Person::on>;

[[nodiscard]] constexpr std::expected<TypedRows<Person>, Error> people_from(std::string_view data) {
    const auto file = parse_jtext(one_section(" 1. /# id/Integer\n 2. /# name/String\n 3. /# on/Bool\n 4. /# ignored/String\n", data));
    if (!file) return std::unexpected(file.error());
    const auto section = get_section(*file, "S");
    if (!section) return std::unexpected(section.error());
    return get_typed_rows<Person, OnColumn, IdColumn, NameColumn>(*section);
}

[[nodiscard]] constexpr bool typed_rows_fill_and_count() {
    const auto typed = people_from(" 1. |# 1|ann|Yes|x\n 1. |# bad|bob|Yes\n 1. |# 3|cat|maybe\n 1. |# 99999999999|dee|No\n 1. ?# 5\n 3. ?# N\n 1. |# |eve|No\n 1. |# 7||Y\n");
    if (!typed || typed->left_out != 4 || typed->rows.size() != 3) return false;
    return typed->rows[0] == Person{1, "ann", true} && typed->rows[1] == Person{5, "", false} && typed->rows[2] == Person{7, "", true};
}
static_assert(typed_rows_fill_and_count(), "declared in another order, with an extra field, and bad rows left out and counted");

[[nodiscard]] constexpr bool no_rows_is_no_rows() {
    const auto typed = people_from("");
    return typed && typed->rows.empty() && typed->left_out == 0;
}
static_assert(no_rows_is_no_rows());

[[nodiscard]] constexpr bool a_column_the_file_lacks_is_an_error() {
    const auto file = parse_jtext(one_section(" 1. /# id/Integer\n 2. /# on/Bool\n", " 1. |# 1|Yes\n"));
    if (!file) return false;
    const auto section = get_section(*file, "S");
    if (!section) return false;
    const auto typed = get_typed_rows<Person, IdColumn, NameColumn, OnColumn>(*section);
    return !typed && typed.error().message.contains("no field named name");
}
static_assert(a_column_the_file_lacks_is_an_error());

// --- Everything the monitor writes reads back ---

using WrittenMonitor = FrameMonitor<jgui_test::TestClock, jgui_test::MemorySink>;

[[nodiscard]] constexpr WrittenMonitor monitor_with_rows(std::string_view label) {
    WrittenMonitor monitor{jgui_test::TestClock{}, jgui_test::MemorySink{}, FrameMonitorOptions{.slow_budget_ns = 12, .row_limit = 3, .label = label}};
    std::vector<std::uint32_t> pixels(4, 0);
    const Size size{Width{2}, Height{2}};
    auto buffer = PixelBuffer::create(pixels, size, PixelBuffer::tight_stride(size));
    const Rect area{Point{Left{-3}, Top{7}}, size};
    for (int i = 0; i < 4; ++i) {
        monitor.clock().queue({100, 1, 2, 3, 4, 5});
        monitor.frame_begins(i == 0 ? RedrawReason::FirstFrame : RedrawReason::WidgetDamage, area);
        monitor.widget_damaged(DamagedWidget{"Button", "HelpButton", 3});
        monitor.phases_begin();
        monitor.laid_out();
        monitor.acquired();
        monitor.painted(*buffer);
        monitor.presented();
    }
    monitor.clock().queue({9});
    monitor.deferred(RedrawReason::Resize, Size{Width{640}, Height{480}});
    monitor.finish();
    return monitor;
}

[[nodiscard]] constexpr bool the_monitors_file_reads_back(std::string_view label) {
    const WrittenMonitor monitor = monitor_with_rows(label);
    const auto file = parse_jtext(monitor.sink().saved);
    if (!file || file->section_count() != 4) return false;
    if (file->header_value(1) != "test.jtext" || file->header_value(2) != "2026-11-02" || file->header_value(3) != "jCppGUI redraw monitor" || file->header_value(4) != "0.8" || file->header_value(5) != "Sensitive" || file->header_value(8) != "true") return false;
    const auto run = get_section(*file, "Run");
    const auto frames = get_section(*file, "Frames");
    const auto damage = get_section(*file, "Damage");
    const auto deferred = get_section(*file, "Deferred");
    if (!run || !frames || !damage || !deferred) return false;
    if (get_row_count(*run) != 1 || get_top_one_string(*run, "Label") != label || get_top_one_int(*run, "Slow_Budget_ns") != 12 || get_top_one_int(*run, "Row_Limit") != 3) return false;
    if (get_top_one_int(*run, "Frames_Seen") != 4 || get_top_one_int(*run, "Frames_Dropped") != 1 || get_top_one_int(*run, "Rows_Refused") != 0 || get_cell_bool(*run, 0, "Ended") != true) return false;
    if (get_row_count(*frames) != monitor.frames().size() || get_row_count(*damage) != monitor.damage().size() || get_row_count(*deferred) != monitor.deferred_rows().size()) return false;
    for (std::size_t i = 0; i < monitor.frames().size(); ++i) {
        const FrameRow& row = monitor.frames()[i];
        if (get_cell_int(*frames, i, "Frame_ID") != static_cast<std::int64_t>(row.frame_id) || get_cell_int(*frames, i, "Start_ns") != row.start_ns || get_cell_int(*frames, i, "Total_ns") != row.total_ns) return false;
        if (get_cell_int(*frames, i, "Left") != -3 || get_cell_int(*frames, i, "Top") != 7 || get_cell_int(*frames, i, "Width") != 2 || get_cell_int(*frames, i, "Height") != 2) return false;
        if (get_cell_int(*frames, i, "Layout_ns") != row.layout_ns || get_cell_int(*frames, i, "Paint_ns") != row.paint_ns || get_cell_int(*frames, i, "Hash_ns") != row.hash_ns || get_cell_int(*frames, i, "Present_ns") != row.present_ns) return false;
        if (get_cell_string(*frames, i, "Reason") != detail::reason_text(row.reason) || get_cell_bool(*frames, i, "Slow") != row.slow || get_cell_bool(*frames, i, "SamePixels") != row.same_pixels) return false;
    }
    for (std::size_t i = 0; i < monitor.damage().size(); ++i) {
        const DamageRow& row = monitor.damage()[i];
        if (get_cell_int(*damage, i, "Record_ID") != static_cast<std::int64_t>(row.record_id) || get_cell_int(*damage, i, "Frame_ID") != static_cast<std::int64_t>(row.frame_id)) return false;
        if (get_cell_string(*damage, i, "Class_Name") != row.widget.class_name || get_cell_string(*damage, i, "Tag") != row.widget.tag_name || get_cell_int(*damage, i, "Tree_Position") != 3) return false;
    }
    if (get_cell_string(*deferred, 0, "Reason") != "Resize" || get_cell_int(*deferred, 0, "Width") != 640 || get_cell_int(*deferred, 0, "Height") != 480 || get_cell_int(*deferred, 0, "Next_Frame_ID") != 5) return false;
    return true;
}
static_assert(the_monitors_file_reads_back(""), "an empty label");
static_assert(the_monitors_file_reads_back("plain"));
static_assert(the_monitors_file_reads_back("a|b \xE2\x96\xAE c \xE2\x96\x8C d #x"), "the fallback formatter, on a line of its own");
static_assert(the_monitors_file_reads_back("#^%~!@ \xE2\x96\xAE"), "further down the fallback list");
static_assert(the_monitors_file_reads_back("\xC3\xA9t\xC3\xA9 \xF0\x9F\x98\x80"), "non-ASCII text");

// One value at a time, past what a label can show: each writer line reads back as what went in.
[[nodiscard]] constexpr bool a_written_line_reads_back(std::string_view first, std::string_view second) {
    std::string data;
    const std::array<std::string_view, 3> values{"7", first, second};
    if (!detail::append_line(data, 1, values)) return false;
    const auto file = parse_jtext(one_section(" 1. /# id/Integer\n 2. /# a/String\n 3. /# b/String\n", data));
    if (!file) return false;
    return cell_is(*file, "S", 0, "id", "7") && cell_is(*file, "S", 0, "a", first) && cell_is(*file, "S", 0, "b", second);
}
static_assert(a_written_line_reads_back("", ""));
static_assert(a_written_line_reads_back("\xE2\x96\xAE", "\xE2\x96\x8C"), "both default marks in the data");
static_assert(a_written_line_reads_back("x \xE2\x96\x8C y", "#"), "a comment mark with a space each side");
static_assert(a_written_line_reads_back("# a", "%^~"));
static_assert(a_written_line_reads_back("[]", "[*"), "these are only text here");
static_assert(a_written_line_reads_back("=== End Data ===", "//"), "and so are these");
static_assert(a_written_line_reads_back("\xC2\xA0", "a  b"), "space that is not trimmed, and space inside");

// --- At run time, through real files ---

void write_file(const std::filesystem::path& path, std::string_view text) {
    const jgui::detail::UniqueFd file{::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    if (file.get() >= 0) static_cast<void>(jgui::detail::write_at(file.get(), std::span{text}, 0));
}

void read_real_files(Checker& check) {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top{directory.path()};
    const std::filesystem::path good = top / "good.jtext";
    write_file(good, small_file);

    const auto read = read_jtext_file(good.string());
    check(read.has_value(), "a regular file reads");
    if (read) {
        const JTextFile& file = *read;
        check(cell_is(file, "People", 3, "name", "dee"), "and holds what was written");
    }

    const auto refused_saying = [&](const std::filesystem::path& path, std::string_view why) {
        const auto file = read_jtext_file(path.string());
        return !file && file.error().message.contains(why) && file.error().message.contains(path.string());
    };
    check(refused_saying(top / "missing.jtext", "No such file"), "a missing file");
    check(refused_saying(top, "not a regular file"), "a directory");

    std::filesystem::create_symlink(good, top / "link.jtext");
    check(refused_saying(top / "link.jtext", "not a regular file"), "a symbolic link to a good file");
    std::filesystem::create_symlink(top / "nothing", top / "dangling.jtext");
    check(refused_saying(top / "dangling.jtext", "not a regular file"), "a dangling symbolic link");

    check(::mkfifo((top / "pipe.jtext").c_str(), S_IRUSR | S_IWUSR) == 0, "a pipe was made");
    check(refused_saying(top / "pipe.jtext", "not a regular file"), "a pipe, with no wait for a writer");

    write_file(top / "cut.jtext", small_file.substr(0, small_file.size() / 2));
    const auto cut = read_jtext_file((top / "cut.jtext").string());
    check(!cut && cut.error().code == ErrorCode::FileFormat, "a file cut short");

    write_file(top / "empty.jtext", "");
    const auto empty = read_jtext_file((top / "empty.jtext").string());
    check(!empty && empty.error().code == ErrorCode::FileFormat, "an empty file");

    const auto limited = read_jtext_file(good.string(), JTextLimits{.file_bytes = small_file.size() - 1});
    check(!limited && limited.error().code == ErrorCode::SizeLimit, "one byte over the limit");
    check(read_jtext_file(good.string(), JTextLimits{.file_bytes = small_file.size()}).has_value(), "exactly at the limit");

    // A limit is checked from what the file says before anything is read, so a sparse file of 1 TiB costs nothing.
    const std::filesystem::path huge = top / "huge.jtext";
    write_file(huge, "");
    std::error_code too_big;
    std::filesystem::resize_file(huge, std::uintmax_t{1} << 40U, too_big);
    if (!too_big) {
        const auto refused = read_jtext_file(huge.string());
        check(!refused && refused.error().code == ErrorCode::SizeLimit, "a 1 TiB sparse file");
    }

    const auto no_path = read_jtext_file("");
    check(!no_path, "an empty path");
    const auto nul = read_jtext_file(std::string{"a\0b", 3});
    check(!nul, "a path holding a NUL");
}

}  // namespace

int main() {
    Checker check;
    read_real_files(check);
    return check.finish();
}
