// File: tests/compile_fail/grid_rows_result_ignored.cpp
// Purpose: Rows can be refused, and a caller must look at the answer.
#include <vector>

#include "common.hpp"

struct Row {
    int count;
};

int main() {
    jgui::DataGrid<First, Row, jgui::NoGridMessage, jgui::Column<"Count", &Row::count>> grid;
#ifdef JGUI_BREAK
    grid.set_rows(std::vector<Row>{});
#else
    if (!grid.set_rows(std::vector<Row>{})) return 1;
#endif
    return 0;
}
