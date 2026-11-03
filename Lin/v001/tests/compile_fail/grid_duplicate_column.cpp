// File: tests/compile_fail/grid_duplicate_column.cpp
// Purpose: Two columns of one grid with one name must not compile.
#include "common.hpp"

struct Row {
    int a;
    int b;
};

int main() {
#ifdef JGUI_BREAK
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Same", &Row::a>, jgui::Column<"Same", &Row::b>> grid;
#else
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"One", &Row::a>, jgui::Column<"Two", &Row::b>> grid;
#endif
    return grid.rows().size() == 0 ? 0 : 1;
}
