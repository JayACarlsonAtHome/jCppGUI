// File: tests/compile_fail/grid_column_wrong_row.cpp
// Purpose: A column over a member of another row type must not compile.
#include "common.hpp"

struct Row {
    int count;
};
struct OtherRow {
    int count;
};

int main() {
#ifdef JGUI_BREAK
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Count", &OtherRow::count>> grid;
#else
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Count", &Row::count>> grid;
#endif
    return grid.rows().size() == 0 ? 0 : 1;
}
