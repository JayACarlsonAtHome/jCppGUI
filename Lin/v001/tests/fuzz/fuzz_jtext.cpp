// File: tests/fuzz/fuzz_jtext.cpp
// Purpose: Fuzzes the jText parser, the real one, and here libFuzzer has coverage of what it is fuzzing. Whatever a file says, an
//          accepted one must hold what the reader promises, and everything the monitor's writer agrees to write must read back
//          as those same values. Anything else aborts.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "c_resource.hpp"
#include "jCppGUI/v001/datagrid.hpp"
#include "jCppGUI/v001/jtext.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

using namespace jgui;

constexpr std::size_t input_limit = std::size_t{1} << 20U;

struct AnyRow {
    std::int64_t number{};
    std::string text{};
    bool flag{};
    Left left{};
    Width width{};
};

// The promise about any text a file gives: it is whole UTF-8, and nothing that could break a line or a display.
void require_clean(std::string_view text) {
    if (!valid_utf8(text) || detail::holds_jtext_control(text)) std::abort();
}

template <class Grid>
void read_as_rows(const JTextSection& section) {
    const auto typed = get_rows_for_grid<Grid>(section);
    if (typed && typed->rows.size() + typed->left_out != section.row_count()) std::abort();
}

struct AnyTag;

// Reads what an accepted file holds, in every way there is. Each name a section or a field goes by must find it again.
void check_accepted(const JTextFile& file) {
    using AnyGrid = DataGrid<AnyTag, AnyRow, NoGridMessage, Column<"number", &AnyRow::number>, Column<"text", &AnyRow::text>, Column<"flag", &AnyRow::flag>, Column<"left", &AnyRow::left>, Column<"width", &AnyRow::width>>;
    for (std::size_t number = 1; number <= jtext_header_values; ++number) require_clean(file.header_value(number));
    for (std::size_t s = 0; s < file.section_count(); ++s) {
        const JTextSection section = file.section_at(s);
        require_clean(section.name());
        if (section.name().empty() || section.field_count() > jtext_most_fields) std::abort();
        const auto found = get_section(file, section.name());
        if (!found || found->name() != section.name()) std::abort();
        for (std::size_t f = 0; f < section.field_count(); ++f) {
            const std::string_view name = section.field_name(f);
            require_clean(name);
            if (name.empty() || get_field_index(section, name) != f) std::abort();
            for (std::size_t r = 0; r < section.row_count(); ++r) {
                const std::string_view cell = section.cell(r, f);
                require_clean(cell);
                if (detail::trimmed_jtext(cell) != cell) std::abort();
                const auto same = get_cell_string(section, r, name);
                if (!same || *same != cell) std::abort();
                static_cast<void>(get_cell_int(section, r, name));
                static_cast<void>(get_cell_bool(section, r, name));
            }
            static_cast<void>(get_top_one_string(section, name));
            static_cast<void>(get_top_one_int(section, name));
        }
        read_as_rows<AnyGrid>(section);
    }
}

// Each line of the input is a value. Whatever the writer takes must come back the same, whichever fallback it chose.
void check_round_trip(std::string_view input) {
    std::vector<std::string_view> values{"1"};
    while (!input.empty() && values.size() < jtext_most_fields) {
        const std::size_t cut = input.find('\n');
        values.push_back(input.substr(0, cut));
        input.remove_prefix(cut == std::string_view::npos ? input.size() : cut + 1);
    }
    std::string rows;
    if (!detail::append_line(rows, 1, values)) return;
    std::string text{"=== jText File ===\n=== Section: S ===\n=== Fields ===\n"};
    for (std::size_t i = 1; i <= values.size(); ++i) text += decimal_text(i, false) + ". /# f" + decimal_text(i, false) + "/String\n";
    text += "=== End Fields ===\n=== Data ===\n" + rows + "=== End Data ===\n=== End Section ===\n=== End File ===\n";
    const auto file = parse_jtext(text);
    if (!file || file->section_count() != 1) std::abort();
    const JTextSection section = file->section_at(0);
    if (section.row_count() != 1) std::abort();
    for (std::size_t i = 0; i < values.size(); ++i)
        if (section.cell(0, i) != values[i]) std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = detail::copy_from_c(data, size, input_limit);
    if (!bytes) return 0;
    std::string text;
    text.reserve(bytes->size());
    for (const std::uint8_t byte : *bytes) text.push_back(static_cast<char>(byte));

    const auto file = parse_jtext(text);
    if (file) check_accepted(*file);
    check_round_trip(text);
    return 0;
}
