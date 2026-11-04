// File: tests/fuzz/fuzz_line_diff.cpp
// Purpose: Fuzzes LineDiffer and shown_line, the real ones, on the two halves of the input. What comes out must always be text a window can be given, and every row's numbers must be lines that exist.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "c_resource.hpp"
#include "jCppGUI/v001/line_diff.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t limit = std::size_t{1} << 16U;
    const auto bytes = jgui::detail::copy_from_c(data, size, limit);
    if (!bytes) return 0;
    std::string first;
    std::string second;
    for (std::size_t i = 0; i < bytes->size(); ++i) (i < bytes->size() / 2 ? first : second).push_back(static_cast<char>((*bytes)[i]));
    const std::string shown = jgui::shown_line(first);
    if (!jgui::valid_utf8(shown) || !jgui::text_can_show(shown) || shown.size() > jgui::max_caption_bytes) std::abort();
    const auto line_count = [](const std::string& text) { return std::uint64_t{jgui::detail::lines_of(text).size()}; };
    const std::uint64_t first_lines = line_count(first);
    const std::uint64_t second_lines = line_count(second);
    auto differ = jgui::LineDiffer::start(std::move(first), std::move(second));
    if (!differ) return 0;
    while (!differ->step()) {}
    const jgui::DiffOutcome out = differ->outcome();
    for (const jgui::DiffRow& row : out.rows) {
        if (!jgui::text_can_show(row.text) || row.text.size() > jgui::max_caption_bytes) std::abort();
        if ((row.left.known && (row.left.count == 0 || row.left.count > first_lines)) || (row.right.known && (row.right.count == 0 || row.right.count > second_lines))) std::abort();
    }
    return 0;
}
