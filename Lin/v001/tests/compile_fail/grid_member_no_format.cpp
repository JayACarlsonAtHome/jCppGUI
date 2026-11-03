// File: tests/compile_fail/grid_member_no_format.cpp
// Purpose: A column over a member with no cell format must not compile.
#include <string>

#include "common.hpp"

struct Row {
    int count;
    double rate;
};

int main() {
#ifdef JGUI_BREAK
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Rate", &Row::rate>> grid;
#else
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Count", &Row::count>> grid;
#endif
    return grid.rows().size() == 0 ? 0 : 1;
}
