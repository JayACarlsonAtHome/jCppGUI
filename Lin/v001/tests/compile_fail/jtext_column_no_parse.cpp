// File: tests/compile_fail/jtext_column_no_parse.cpp
// Purpose: A typed row's column over a member that has no way to be read from text must not compile.
#include "common.hpp"
#include "jCppGUI/v001/jtext.hpp"

struct Opaque {
    int value{};
    friend constexpr auto operator<=>(const Opaque&, const Opaque&) = default;
};

template <>
struct jgui::CellFormat<Opaque> {
    [[nodiscard]] static constexpr std::string_view text(const Opaque&) noexcept { return "opaque"; }
};

struct Row {
    int count{};
    Opaque opaque{};
};

int main() {
    const auto file = jgui::parse_jtext("=== jText File ===\n=== End File ===\n");
    if (!file || file->section_count() == 0) return 0;
#ifdef JGUI_BREAK
    return jgui::get_typed_rows<Row, jgui::Column<"opaque", &Row::opaque>>(file->section_at(0)).has_value() ? 0 : 1;
#else
    return jgui::get_typed_rows<Row, jgui::Column<"count", &Row::count>>(file->section_at(0)).has_value() ? 0 : 1;
#endif
}
